// Copyright (c) 2026 Alice-Trade Inc.
// SPDX-License-Identifier: GPL-3.0-or-later

#include "dali/driver/DaliDriver.hxx"

namespace daliMQTT::Driver {

    static constexpr char TAG[] = "DaliDriver";

    DaliDriver::DaliDriver() {
        m_tx_queue = xQueueCreate(16, sizeof(DaliMessage));
        m_rx_buffer = new rmt_symbol_word_t[RX_BUFFER_SIZE];
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

        xTaskCreate(driverTaskWrapper, "dali_rmt_task", 4096, this, 5, &m_driver_task);

        rmt_receive_config_t rx_config = {
            .signal_range_min_ns = Constants::RX_MIN_NOISE_FILTER_NS,
            .signal_range_max_ns = Constants::RX_IDLE_THRESH_NS,
        };
        ESP_ERROR_CHECK(rmt_receive(m_rx_channel, m_rx_buffer, RX_BUFFER_SIZE * sizeof(rmt_symbol_word_t), &rx_config));

        m_initialized = true;
        return ESP_OK;
    }

    esp_err_t DaliDriver::setupTx() {
        gpio_config_t io_conf = {};
        io_conf.pin_bit_mask = (1ULL << m_config.tx_pin);
        io_conf.mode = GPIO_MODE_OUTPUT;
        io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
        io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
        io_conf.intr_type = GPIO_INTR_DISABLE;
        gpio_config(&io_conf);
        gpio_set_level(m_config.tx_pin, Constants::RMT_LEVEL_IDLE);

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

        rmt_tx_event_callbacks_t cbs = { .on_trans_done = rmt_tx_done_callback };
        ESP_RETURN_ON_ERROR(rmt_tx_register_event_callbacks(m_tx_channel, &cbs, this), TAG, "TX CB failed");

        ESP_RETURN_ON_ERROR(rmt_enable(m_tx_channel), TAG, "TX Enable failed");
        return ESP_OK;
    }

    esp_err_t DaliDriver::setupRx() {
        gpio_config_t io_conf = {};
        io_conf.pin_bit_mask = (1ULL << m_config.rx_pin);
        io_conf.mode = GPIO_MODE_INPUT;
        io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
        io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
        gpio_config(&io_conf);

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
        if (m_driver_task) xTaskNotify(m_driver_task, 0, eNoAction);
        return ESP_OK;
    }

    esp_err_t DaliDriver::sendSystemFailureSignal() {
        m_tx_static_buffer[0] = make_symbol(1500, Constants::RMT_LEVEL_ACTIVE, Constants::T_TE, Constants::RMT_LEVEL_IDLE);

        constexpr rmt_transmit_config_t tx_conf = { .loop_count = 0, .flags = { .eot_level = Constants::RMT_LEVEL_IDLE } };
        ESP_ERROR_CHECK(rmt_transmit(m_tx_channel, m_dali_encoder, m_tx_static_buffer, sizeof(rmt_symbol_word_t), &tx_conf));
        rmt_tx_wait_all_done(m_tx_channel, -1);

        m_last_bus_activity_us = esp_timer_get_time();
        return ESP_OK;
    }

    rmt_symbol_word_t DaliDriver::make_symbol(const uint32_t dur0, const uint8_t lvl0, const uint32_t dur1, const uint8_t lvl1) {
        rmt_symbol_word_t sym;
        sym.duration0 = dur0;
        sym.level0 = lvl0;
        sym.duration1 = dur1;
        sym.level1 = lvl1;
        return sym;
    }

    bool IRAM_ATTR DaliDriver::rmt_tx_done_callback(rmt_channel_handle_t tx_chan, const rmt_tx_done_event_data_t *edata, void *user_ctx) {
        return false;
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
        uint32_t notify_val = 0;
        bool last_rx_was_backward = false;

        while (true) {
            if (xTaskNotifyWait(0, 0xFFFFFFFF, &notify_val, pdMS_TO_TICKS(5)) == pdTRUE) {
                if (notify_val > 0) {
                    size_t decoded_bits = processRxSymbols(m_rx_buffer, notify_val);
                    if (decoded_bits > 0) {
                        m_last_bus_activity_us = esp_timer_get_time();
                        last_rx_was_backward = (decoded_bits == 8);
                    }
                    rmt_receive_config_t rx_config = {
                        .signal_range_min_ns = Constants::RX_MIN_NOISE_FILTER_NS,
                        .signal_range_max_ns = Constants::RX_IDLE_THRESH_NS,
                    };
                    ESP_ERROR_CHECK(rmt_receive(m_rx_channel, m_rx_buffer, RX_BUFFER_SIZE * sizeof(rmt_symbol_word_t), &rx_config));
                }
            }

            TickType_t delay_ticks = pdMS_TO_TICKS(1);
            vTaskDelay(delay_ticks > 0 ? delay_ticks : 1);

            bool is_tx_active;
            {
                std::lock_guard<std::mutex> lock(m_state_mutex);
                is_tx_active = m_tx_state.active;
            }

            if (is_tx_active) {
                if ((esp_timer_get_time() - m_tx_state.start_ts) > Constants::TX_WATCHDOG_TIMEOUT_US ) {
                    DaliMessage err_msg;
                    err_msg.type = DaliEventType::CollisionDetected;
                    err_msg.timestamp = esp_timer_get_time();
                    if (m_event_cb) m_event_cb(err_msg, m_event_cb_ctx);

                    std::lock_guard<std::mutex> lock(m_state_mutex);
                    m_tx_state.active = false;
                    is_tx_active = false;
                    ESP_LOGD(TAG, "TX Timeout (No Echo)");
                }
            }

            if (!is_tx_active && xQueueReceive(m_tx_queue, &tx_msg, 0) == pdTRUE) {
                int64_t required_delay_us = last_rx_was_backward ?
                                           Constants::DELAY_BACKWARD_TO_FORWARD :
                                           Constants::DELAY_FORWARD_TO_FORWARD;
                int64_t now_us = esp_timer_get_time();
                int64_t time_since_last_activity = now_us - m_last_bus_activity_us;

                if (time_since_last_activity < required_delay_us) {
                    int64_t wait_us = required_delay_us - time_since_last_activity;
                    if (wait_us > 2000) {
                        vTaskDelay(pdMS_TO_TICKS(wait_us / 1000));
                    }
                    while ((esp_timer_get_time() - m_last_bus_activity_us) < required_delay_us) {
                        esp_rom_delay_us(10);
                    }
                }

                {
                    std::lock_guard<std::mutex> lock(m_state_mutex);
                    m_tx_state.active = true;
                    m_tx_state.data = tx_msg.data;
                    m_tx_state.bits = tx_msg.length;
                    m_tx_state.start_ts = esp_timer_get_time();
                }

                size_t symbols = encodeFrame(tx_msg.data, tx_msg.length);
                rmt_transmit_config_t tx_conf = { .loop_count = 0, .flags = { .eot_level = Constants::RMT_LEVEL_IDLE } };
                ESP_ERROR_CHECK(rmt_transmit(m_tx_channel, m_dali_encoder, m_tx_static_buffer, symbols * sizeof(rmt_symbol_word_t), &tx_conf));
            }
        }
    }

    size_t DaliDriver::encodeFrame(const uint32_t data, const uint8_t bits) {
        size_t count = 0;
        m_tx_static_buffer[count++] = make_symbol(Constants::T_TE, Constants::RMT_LEVEL_ACTIVE, Constants::T_TE, Constants::RMT_LEVEL_IDLE);

        for (int i = bits - 1; i >= 0; --i) {
            const bool bit = (data >> i) & 1;
            if (bit) {
                // 1 active 1 TE > idle 1 TE
                m_tx_static_buffer[count++] = make_symbol(Constants::T_TE, Constants::RMT_LEVEL_ACTIVE, Constants::T_TE, Constants::RMT_LEVEL_IDLE);
            } else {
                // 0 idle 1 TE > active 1 TE
                m_tx_static_buffer[count++] = make_symbol(Constants::T_TE, Constants::RMT_LEVEL_IDLE, Constants::T_TE, Constants::RMT_LEVEL_ACTIVE);
            }
        }

        // Stop Bit
        m_tx_static_buffer[count++] = make_symbol(Constants::T_TE * 4, Constants::RMT_LEVEL_IDLE, 0, Constants::RMT_LEVEL_IDLE);
        return count;
    }

   size_t DaliDriver::processRxSymbols(const rmt_symbol_word_t* symbols, size_t count) {
        if (!symbols || count == 0) return 0;
        int te_count = 0;

        if (m_tx_state.active) { // debug
            char dbg[256] = {0};
            int pos = snprintf(dbg, sizeof(dbg), "RAW RMT (cnt=%d): ", count);
            for(size_t i = 0; i < count && i < 10; i++) {
                pos += snprintf(dbg + pos, sizeof(dbg) - pos, "[%d:%d %d:%d] ",
                    symbols[i].level0, symbols[i].duration0,
                    symbols[i].level1, symbols[i].duration1);
            }
            ESP_LOGW(TAG, "%s", dbg);
        }

        for(size_t i = 0; i < count; ++i) {
            if (symbols[i].duration0 == 0 && symbols[i].duration1 == 0) break;

            const int num_te0 = (symbols[i].duration0 + Constants::T_TE / 2) / Constants::T_TE;
            for(int j = 0; j < num_te0 && te_count < sizeof(m_te_buffer); ++j) m_te_buffer[te_count++] = symbols[i].level0;

            const int num_te1 = (symbols[i].duration1 + Constants::T_TE / 2) / Constants::T_TE;
            for(int j = 0; j < num_te1 && te_count < sizeof(m_te_buffer); ++j) m_te_buffer[te_count++] = symbols[i].level1;
        }

        int idx = 0;
        size_t total_bits_decoded = 0;

        while (idx < te_count) {
            while(idx < te_count && m_te_buffer[idx] == Constants::RMT_LEVEL_IDLE) {
                idx++;
            }

            if (idx >= te_count - 2) break;

            uint32_t rx_data = 0;
            int bits_decoded = 0;

            if (m_te_buffer[idx] == Constants::RMT_LEVEL_ACTIVE && m_te_buffer[idx+1] == Constants::RMT_LEVEL_IDLE) {
                idx += 2;
            } else {
                idx++;
                continue;
            }

            while (idx < te_count - 1) {
                const uint8_t half1 = m_te_buffer[idx];
                const uint8_t half2 = m_te_buffer[idx+1];

                if (half1 == Constants::RMT_LEVEL_ACTIVE && half2 == Constants::RMT_LEVEL_IDLE) {
                    rx_data = (rx_data << 1) | 1;
                    bits_decoded++;
                    idx += 2;
                } else if (half1 == Constants::RMT_LEVEL_IDLE && half2 == Constants::RMT_LEVEL_ACTIVE) {
                    rx_data = (rx_data << 1) | 0;
                    bits_decoded++;
                    idx += 2;
                } else {
                    if (m_tx_state.active) { // debug
                        ESP_LOGE(TAG, "Manchester ERR at bit %d! h1=%d, h2=%d", bits_decoded, half1, half2);
                    }
                    break;
                }

                if (bits_decoded == 24) break;
            }
            if (m_tx_state.active) { // debug
                ESP_LOGW(TAG, "DECODED: %d bits. RX: 0x%06X, Expected TX: 0x%06X",
                         bits_decoded, (unsigned int)rx_data, (unsigned int)m_tx_state.data);
            }
            if (bits_decoded >= 8) {
                DaliMessage msg;
                msg.data = rx_data;
                msg.length = bits_decoded;
                msg.timestamp = esp_timer_get_time();
                msg.is_backward = (bits_decoded <= 8);

                std::lock_guard<std::mutex> lock(m_state_mutex);

                if (m_tx_state.active && bits_decoded == m_tx_state.bits) {
                    if (m_tx_state.data == rx_data) {
                        msg.type = DaliEventType::TxCompleted;
                        m_tx_state.active = false;
                    } else {
                        msg.type = DaliEventType::CollisionDetected;
                        m_tx_state.active = false;
                    }
                } else {
                    msg.type = DaliEventType::FrameReceived;
                }

                if (m_event_cb) m_event_cb(msg, m_event_cb_ctx);
                total_bits_decoded += bits_decoded;
            } else
            {
                std::lock_guard<std::mutex> lock(m_state_mutex);
                if (m_tx_state.active) {
                    DaliMessage err_msg;
                    err_msg.type = DaliEventType::CollisionDetected;
                    m_tx_state.active = false;
                    if (m_event_cb) m_event_cb(err_msg, m_event_cb_ctx);
                }
            }
            while (idx < te_count && m_te_buffer[idx] == Constants::RMT_LEVEL_ACTIVE) idx++;
        }
        return total_bits_decoded;
    }

} // namespace daliMQTT::Driver