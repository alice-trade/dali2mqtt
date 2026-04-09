// Copyright (c) 2026 Alice-Trade Inc.
// SPDX-License-Identifier: GPL-3.0-or-later

#include "dali/driver/DaliDriver.hxx"

namespace daliMQTT::Driver {

    static constexpr char TAG[] = "DaliDriver";

    DaliDriver::DaliDriver()
        : m_tx_queue(xQueueCreate(16, sizeof(DaliMessage))),
          m_rx_buffer(new rmt_symbol_word_t[RX_BUFFER_SIZE])
    {
    }

    DaliDriver::~DaliDriver() {
        if (m_driver_task) vTaskDelete(m_driver_task);
        if (m_tx_channel) { rmt_disable(m_tx_channel); rmt_del_channel(m_tx_channel); }
        if (m_rx_channel) { rmt_disable(m_rx_channel); rmt_del_channel(m_rx_channel); }
        if (m_dali_encoder) rmt_del_encoder(m_dali_encoder);
        if (m_tx_queue) vQueueDelete(m_tx_queue);
        delete[] m_rx_buffer;
    }

    esp_err_t DaliDriver::init(const DaliDriverConfig& config) {
        if (m_initialized) return ESP_OK;
        m_config = config;

        ESP_LOGI(TAG, "Init DALI: RX=%d, TX=%d", config.rx_pin, config.tx_pin);

        ESP_RETURN_ON_ERROR(setupTx(), TAG, "TX Setup failed");
        ESP_RETURN_ON_ERROR(setupRx(), TAG, "RX Setup failed");

        xTaskCreate(driverTaskWrapper, "dali_rmt_task", 4096, this, 10, &m_driver_task);
        rmt_receive_config_t rx_config = {
            .signal_range_min_ns = Constants::RX_MIN_NOISE_FILTER_NS,
            .signal_range_max_ns = Constants::RX_IDLE_THRESH_NS,
        };
        ESP_ERROR_CHECK(rmt_receive(m_rx_channel, m_rx_buffer, RX_BUFFER_SIZE * sizeof(rmt_symbol_word_t), &rx_config));

        m_initialized = true;
        return ESP_OK;
    }

    esp_err_t DaliDriver::setupTx() {
        rmt_tx_channel_config_t tx_cfg = {
            .gpio_num = m_config.tx_pin,
            .clk_src = RMT_CLK_SRC_DEFAULT,
            .resolution_hz = m_config.resolution_hz,
            .mem_block_symbols = 64,
            .trans_queue_depth = 4,
            .flags = { .invert_out = false, .with_dma = false },
        };
        ESP_RETURN_ON_ERROR(rmt_new_tx_channel(&tx_cfg, &m_tx_channel), TAG, "New TX failed");

        rmt_copy_encoder_config_t enc_cfg = {};
        ESP_RETURN_ON_ERROR(rmt_new_copy_encoder(&enc_cfg, &m_dali_encoder), TAG, "Encoder failed");

        ESP_RETURN_ON_ERROR(rmt_enable(m_tx_channel), TAG, "TX Enable failed");
        return ESP_OK;
    }

    esp_err_t DaliDriver::setupRx() {
        rmt_rx_channel_config_t rx_cfg = {
            .gpio_num = m_config.rx_pin,
            .clk_src = RMT_CLK_SRC_DEFAULT,
            .resolution_hz = m_config.resolution_hz,
            .mem_block_symbols = 64,
            .flags = {
                .invert_in = true,
                .with_dma = false
            }
        };
        ESP_RETURN_ON_ERROR(rmt_new_rx_channel(&rx_cfg, &m_rx_channel), TAG, "New RX failed");

        rmt_rx_event_callbacks_t cbs = { .on_recv_done = rmt_rx_done_callback };
        ESP_RETURN_ON_ERROR(rmt_rx_register_event_callbacks(m_rx_channel, &cbs, this), TAG, "RX CB failed");
        ESP_RETURN_ON_ERROR(rmt_enable(m_rx_channel), TAG, "RX Enable failed");
        return ESP_OK;
    }

    esp_err_t DaliDriver::sendAsync(const uint32_t data, const uint8_t bits) const {
        DaliMessage msg{};
        msg.data = data;
        msg.length = bits;

        if (xQueueSend(m_tx_queue, &msg, pdMS_TO_TICKS(10)) != pdTRUE) {
            return ESP_FAIL;
        }
        return ESP_OK;
    }

    esp_err_t DaliDriver::sendSystemFailureSignal() {
        m_tx_static_buffer[0] = make_symbol(1500, Constants::RMT_LEVEL_ACTIVE, Constants::T_TE, Constants::RMT_LEVEL_IDLE);

        const rmt_transmit_config_t tx_conf = { .loop_count = 0 };
        esp_err_t err = rmt_transmit(m_tx_channel, m_dali_encoder, m_tx_static_buffer, 1 * sizeof(rmt_symbol_word_t), &tx_conf);
        if(err == ESP_OK) {
            rmt_tx_wait_all_done(m_tx_channel, -1);
            m_last_bus_activity_us = esp_timer_get_time();
        }
        return err;
    }

    bool IRAM_ATTR DaliDriver::rmt_rx_done_callback(rmt_channel_handle_t rx_chan, const rmt_rx_done_event_data_t *edata, void *user_ctx) {
        const auto* driver = static_cast<DaliDriver*>(user_ctx);
        BaseType_t high_task_wakeup = pdFALSE;
        xTaskNotifyFromISR(driver->m_driver_task, edata->num_symbols, eSetValueWithOverwrite, &high_task_wakeup);
        return high_task_wakeup == pdTRUE;
    }

    void DaliDriver::driverTaskWrapper(void* arg) {
        static_cast<DaliDriver*>(arg)->driverTask();
        vTaskDelete(nullptr);
    }

    void DaliDriver::driverTask() {
        DaliMessage tx_msg;
        uint32_t notified_symbols = 0;

        while (true) {
            if (xTaskNotifyWait(0, 0xFFFFFFFF, &notified_symbols, pdMS_TO_TICKS(1)) == pdTRUE) {
                if (notified_symbols > 0) {
                    processRxSymbols(m_rx_buffer, notified_symbols);

                    rmt_receive_config_t rx_config = {
                        .signal_range_min_ns = Constants::RX_MIN_NOISE_FILTER_NS,
                        .signal_range_max_ns = Constants::RX_IDLE_THRESH_NS,
                    };
                    rmt_receive(m_rx_channel, m_rx_buffer, RX_BUFFER_SIZE * sizeof(rmt_symbol_word_t), &rx_config);
                }
            }

            bool is_tx_active = false;
            {
                std::lock_guard<std::mutex> lock(m_state_mutex);
                is_tx_active = m_tx_state.active;
                if (is_tx_active && (esp_timer_get_time() - m_tx_state.start_ts > Constants::TX_WATCHDOG_TIMEOUT_US)) {
                    m_tx_state.active = false;
                    is_tx_active = false;

                    DaliMessage err_msg;
                    err_msg.type = DaliEventType::CollisionDetected;
                    if (m_event_cb) m_event_cb(err_msg, m_event_cb_ctx);
                    ESP_LOGD(TAG, "TX Watchdog Timeout (No Echo received)");
                }
            }

            if (!is_tx_active && xQueueReceive(m_tx_queue, &tx_msg, 0) == pdTRUE) {
                int64_t now_us = esp_timer_get_time();
                int64_t silence_duration = now_us - m_last_bus_activity_us;

                if (silence_duration < Constants::DELAY_FORWARD_TO_FORWARD) {
                    int64_t wait_us = Constants::DELAY_FORWARD_TO_FORWARD - silence_duration;
                    if (wait_us > 2000) vTaskDelay(pdMS_TO_TICKS(wait_us / 1000));
                    while ((esp_timer_get_time() - m_last_bus_activity_us) < Constants::DELAY_FORWARD_TO_FORWARD) {
                        esp_rom_delay_us(50);
                    }
                }

                {
                    std::lock_guard<std::mutex> lock(m_state_mutex);
                    m_tx_state.active = true;
                    m_tx_state.data = tx_msg.data;
                    m_tx_state.bits = tx_msg.length;
                    m_tx_state.start_ts = esp_timer_get_time();
                }

                size_t symbols_count = encodeFrame(tx_msg.data, tx_msg.length);
                rmt_transmit_config_t tx_conf = { .loop_count = 0 };
                ESP_ERROR_CHECK(rmt_transmit(m_tx_channel, m_dali_encoder, m_tx_static_buffer, symbols_count * sizeof(rmt_symbol_word_t), &tx_conf));
            }
        }
    }

    size_t DaliDriver::encodeFrame(const uint32_t data, const uint8_t bits) {
        etl::vector<uint8_t, 64> half_bits;

        half_bits.push_back(Constants::RMT_LEVEL_ACTIVE);
        half_bits.push_back(Constants::RMT_LEVEL_IDLE);

        for (int i = bits - 1; i >= 0; --i) {
            if ((data >> i) & 1) {
                half_bits.push_back(Constants::RMT_LEVEL_ACTIVE);
                half_bits.push_back(Constants::RMT_LEVEL_IDLE);
            } else {
                half_bits.push_back(Constants::RMT_LEVEL_IDLE);
                half_bits.push_back(Constants::RMT_LEVEL_ACTIVE);
            }
        }

        half_bits.push_back(Constants::RMT_LEVEL_IDLE);
        half_bits.push_back(Constants::RMT_LEVEL_IDLE);
        half_bits.push_back(Constants::RMT_LEVEL_IDLE);
        half_bits.push_back(Constants::RMT_LEVEL_IDLE);

        size_t count = 0;
        uint8_t current_lvl = half_bits[0];
        uint32_t current_dur = Constants::T_TE;

        bool is_first_part = true;
        uint32_t dur0 = 0;
        uint8_t lvl0 = 0;

        for (size_t i = 1; i < half_bits.size(); i++) {
            if (half_bits[i] == current_lvl) {
                current_dur += Constants::T_TE;
            } else {
                if (is_first_part) {
                    dur0 = current_dur; lvl0 = current_lvl;
                    is_first_part = false;
                } else {
                    m_tx_static_buffer[count++] = make_symbol(dur0, lvl0, current_dur, current_lvl);
                    is_first_part = true;
                }
                current_lvl = half_bits[i];
                current_dur = Constants::T_TE;
            }
        }

        if (is_first_part) {
            m_tx_static_buffer[count++] = make_symbol(current_dur, current_lvl, 0, 0);
        } else {
            m_tx_static_buffer[count++] = make_symbol(dur0, lvl0, current_dur, current_lvl);
        }

        return count;
    }

    size_t DaliDriver::processRxSymbols(const rmt_symbol_word_t* symbols, size_t count) {
        if (!symbols || count == 0) return 0;

        auto report_collision = [&]() -> size_t {
            std::lock_guard<std::mutex> lock(m_state_mutex);
            if (m_tx_state.active) {
                m_tx_state.active = false;
                DaliMessage err_msg;
                err_msg.type = DaliEventType::CollisionDetected;
                if (m_event_cb) m_event_cb(err_msg, m_event_cb_ctx);
            }
            return 0;
        };

        m_last_bus_activity_us = esp_timer_get_time() - (Constants::RX_IDLE_THRESH_NS / 1000);

        etl::vector<uint8_t, 256> half_bits;
        half_bits.reserve(count * 4);

        for(size_t i = 0; i < count; ++i) {
            if (symbols[i].duration0 > 0) {
                uint32_t te = (symbols[i].duration0 + (Constants::T_TE / 2)) / Constants::T_TE;
                te = std::clamp<uint32_t>(te, 1, 4);
                for (uint32_t j = 0; j < te; ++j) {
                    half_bits.push_back(symbols[i].level0);
                }
            }
            if (symbols[i].duration1 > 0) {
                uint32_t te = (symbols[i].duration1 + (Constants::T_TE / 2)) / Constants::T_TE;
                te = std::clamp<uint32_t>(te, 1, 4);
                for (uint32_t j = 0; j < te; ++j) {
                    half_bits.push_back(symbols[i].level1);
                }
            }
        }

        size_t idx = 0;
        while(idx < half_bits.size() && half_bits[idx] == Constants::RMT_LEVEL_IDLE) {
            idx++;
        }

        if (idx + 1 >= half_bits.size() || half_bits[idx] != Constants::RMT_LEVEL_ACTIVE || half_bits[idx+1] != Constants::RMT_LEVEL_IDLE) {
            return report_collision();
        }
        idx += 2;

        uint32_t rx_data = 0;
        int bits_decoded = 0;

        while (idx + 1 < half_bits.size()) {
            if (half_bits[idx] == Constants::RMT_LEVEL_ACTIVE && half_bits[idx+1] == Constants::RMT_LEVEL_IDLE) {
                rx_data = (rx_data << 1) | 1;
                bits_decoded++;
                idx += 2;
            } else if (half_bits[idx] == Constants::RMT_LEVEL_IDLE && half_bits[idx+1] == Constants::RMT_LEVEL_ACTIVE) {
                rx_data = (rx_data << 1);
                bits_decoded++;
                idx += 2;
            } else {
                break;
            }
        }

        if (bits_decoded == 8 || bits_decoded == 16 || bits_decoded == 24) {
            DaliMessage msg;
            msg.data = rx_data;
            msg.length = bits_decoded;
            msg.timestamp = esp_timer_get_time();
            msg.is_backward = (bits_decoded <= 8);

            std::lock_guard<std::mutex> lock(m_state_mutex);
            if (m_tx_state.active) {
                if (m_tx_state.bits == bits_decoded && m_tx_state.data == rx_data) {
                    msg.type = DaliEventType::TxCompleted;
                } else {
                    msg.type = DaliEventType::CollisionDetected;
                    ESP_LOGW(TAG, "Collision: TX 0x%04lX, RX 0x%04lX", m_tx_state.data, rx_data);
                }
                m_tx_state.active = false;
            } else {
                msg.type = DaliEventType::FrameReceived;
            }

            if (m_event_cb) m_event_cb(msg, m_event_cb_ctx);
            return bits_decoded;
        }

        return report_collision();
    }
} // namespace daliMQTT::Driver