// Copyright (c) 2026 Alice-Trade Inc.
// SPDX-License-Identifier: GPL-2.0-or-later

#include "dali/driver/DaliDriver.hxx"

namespace daliMQTT::Driver {

    static constexpr char TAG[] = "DaliDriver";

    DaliDriver::DaliDriver() {
        m_tx_queue = xQueueCreate(16, sizeof(DaliMessage));
        m_event_queue = xQueueCreate(64, sizeof(DaliMessage));
        m_rx_buffer = new rmt_symbol_word_t[RX_BUFFER_SIZE];
    }

    DaliDriver::~DaliDriver() {
        if (m_driver_task) vTaskDelete(m_driver_task);
        if (m_tx_channel) { rmt_disable(m_tx_channel); rmt_del_channel(m_tx_channel); }
        if (m_rx_channel) { rmt_disable(m_rx_channel); rmt_del_channel(m_rx_channel); }
        if (m_dali_encoder) rmt_del_encoder(m_dali_encoder);
        if (m_tx_queue) vQueueDelete(m_tx_queue);
        if (m_event_queue) vQueueDelete(m_event_queue);
        delete[] m_rx_buffer;
    }

    esp_err_t DaliDriver::init(const DaliDriverConfig& config) {
        if (m_initialized) return ESP_OK;
        m_config = config;

        ESP_LOGI(TAG, "Init DALI: RX=%d, TX=%d", config.rx_pin, config.tx_pin);

        ESP_RETURN_ON_ERROR(setupTx(), TAG, "TX Setup failed");
        ESP_RETURN_ON_ERROR(setupRx(), TAG, "RX Setup failed");

        xTaskCreate(driverTaskWrapper, "dali_rmt_task", 4096, this, configMAX_PRIORITIES - 1, &m_driver_task);

        rmt_receive_config_t rx_config = {
            .signal_range_min_ns = 50000,   // 50us noise filter
            .signal_range_max_ns = Constants::RX_IDLE_THRESH_NS, // 1.8ms Idle = Stop Condition
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
            .mem_block_symbols = 128,
            .trans_queue_depth = 4,
            .flags = { .invert_out = false, .with_dma = false },
        };
        gpio_set_level(m_config.tx_pin, Constants::RMT_LEVEL_IDLE); // ?

        ESP_RETURN_ON_ERROR(rmt_new_tx_channel(&tx_cfg, &m_tx_channel), TAG, "New TX failed");

        rmt_copy_encoder_config_t enc_cfg = {};
        ESP_RETURN_ON_ERROR(rmt_new_copy_encoder(&enc_cfg, &m_dali_encoder), TAG, "Encoder failed");

        rmt_tx_event_callbacks_t cbs = { .on_trans_done = rmt_tx_done_callback };
        ESP_RETURN_ON_ERROR(rmt_tx_register_event_callbacks(m_tx_channel, &cbs, this), TAG, "TX CB failed");

        ESP_RETURN_ON_ERROR(rmt_enable(m_tx_channel), TAG, "TX Enable failed");
        return ESP_OK;
    }

    esp_err_t DaliDriver::setupRx() {
        rmt_rx_channel_config_t rx_cfg = {
            .gpio_num = m_config.rx_pin,
            .clk_src = RMT_CLK_SRC_DEFAULT,
            .resolution_hz = m_config.resolution_hz,
            .mem_block_symbols = 128,
            .flags = { .with_dma = false },
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
        rmt_symbol_word_t syms[2];
        // 1500us ACTIVE
        syms[0] = make_symbol(1500, Constants::RMT_LEVEL_ACTIVE);
        // Return to IDLE
        syms[1] = make_symbol(Constants::T_TE, Constants::RMT_LEVEL_IDLE);

        const rmt_transmit_config_t tx_conf = { .loop_count = 0 };
        ESP_ERROR_CHECK(rmt_transmit(m_tx_channel, m_dali_encoder, syms, 2, &tx_conf));
        rmt_tx_wait_all_done(m_tx_channel, pdMS_TO_TICKS(10));

        m_last_bus_activity_us = esp_timer_get_time();
        return ESP_OK;
    }

    void DaliDriver::flushRxQueue() const {
        xQueueReset(m_event_queue);
    }


    rmt_symbol_word_t DaliDriver::make_symbol(const uint32_t duration, const uint8_t level) {
        rmt_symbol_word_t sym;
        sym.val = (duration & 0x7FFF) | ((level & 1) << 15);
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
        static_cast<DaliDriver*>(arg)->driverTaskLoop();
        vTaskDelete(nullptr);
    }

    void DaliDriver::driverTaskLoop() {
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
                    } else {
                        m_last_bus_activity_us = esp_timer_get_time();
                    }
                    rmt_receive_config_t rx_config = {
                        .signal_range_min_ns = Constants::RX_MIN_NOISE_FILTER_NS,
                        .signal_range_max_ns = Constants::RX_IDLE_THRESH_NS,
                    };
                    ESP_ERROR_CHECK(rmt_receive(m_rx_channel, m_rx_buffer, RX_BUFFER_SIZE * sizeof(rmt_symbol_word_t), &rx_config));
                }
            }

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
                    xQueueSend(m_event_queue, &err_msg, 0);
                    
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
                    if (wait_us > 0) {
                        esp_rom_delay_us(static_cast<uint32_t>(wait_us));
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
                rmt_transmit_config_t tx_conf = { .loop_count = 0 };
                ESP_ERROR_CHECK(rmt_transmit(m_tx_channel, m_dali_encoder, m_tx_static_buffer, symbols, &tx_conf));
            }
        }
    }

    size_t DaliDriver::encodeFrame(const uint32_t data, const uint8_t bits) {
        size_t count = 0;

        auto push_sym = [&](const uint32_t duration, const uint8_t level) {
            m_tx_static_buffer[count++].val = make_symbol(duration, level).val;
        };

        // Start Bit: Logic 1 (Active -> Idle)
        push_sym(Constants::T_TE, Constants::RMT_LEVEL_ACTIVE);
        push_sym(Constants::T_TE, Constants::RMT_LEVEL_IDLE);

        for (int i = bits - 1; i >= 0; --i) {
            const bool bit = (data >> i) & 1;
            if (bit) { // Logic 1
                push_sym(Constants::T_TE, Constants::RMT_LEVEL_ACTIVE);
                push_sym(Constants::T_TE, Constants::RMT_LEVEL_IDLE);
            } else {   // Logic 0
                push_sym(Constants::T_TE, Constants::RMT_LEVEL_IDLE);
                push_sym(Constants::T_TE, Constants::RMT_LEVEL_ACTIVE);
            }
        }

        // Stop Bit
        push_sym(2 * Constants::T_TE, Constants::RMT_LEVEL_IDLE);

        return count;
    }

    size_t DaliDriver::processRxSymbols(const rmt_symbol_word_t* symbols, size_t count) {
        if (count < 2) return 0;

        size_t idx = 0;
        bool found_start = false;

        auto get_level = [](const rmt_symbol_word_t& s) -> uint8_t {
            return (s.val >> 15) & 1;
        };
        auto get_time = [](const rmt_symbol_word_t& s) -> uint32_t {
            return s.val & 0x7FFF;
        };

        {
            uint32_t t0 = get_time(symbols[0]);
            uint8_t  l0 = get_level(symbols[0]);
            if (l0 == Constants::RMT_LEVEL_ACTIVE && t0 > Constants::T_SYSTEM_FAILURE_MIN) {
                // bus stuck low
                DaliMessage msg{ .type = DaliEventType::BusFailure, .timestamp = esp_timer_get_time() };
                xQueueSend(m_event_queue, &msg, 0);

                std::lock_guard<std::mutex> lock(m_state_mutex);
                if (m_tx_state.active) m_tx_state.active = false;
                return 0;
            }
        }

        for (; idx < count - 1; ++idx) {
            uint32_t t1 = get_time(symbols[idx]);
            uint8_t  l1 = get_level(symbols[idx]);
            uint32_t t2 = get_time(symbols[idx+1]);
            uint8_t  l2 = get_level(symbols[idx+1]);

            if (l1 == Constants::RMT_LEVEL_ACTIVE && t1 >= Constants::T_TE_MIN && t1 <= Constants::T_TE_MAX &&
                l2 == Constants::RMT_LEVEL_IDLE   && t2 >= Constants::T_TE_MIN && t2 <= Constants::T_TE_MAX) {
                found_start = true;
                idx += 2;
                break;
            }
        }

        if (!found_start) {
            std::lock_guard<std::mutex> lock(m_state_mutex);
            if (m_tx_state.active) {
                DaliMessage msg{ .data=0, .length=0, .is_backward=false, .type = DaliEventType::CollisionDetected, .timestamp = esp_timer_get_time() };
                xQueueSend(m_event_queue, &msg, 0);
                m_tx_state.active = false;
                ESP_LOGD(TAG, "Collision: Start bit corrupted");
            }
            return 0;
        }

        uint32_t rx_data = 0;
        uint8_t bits_cnt = 0;
        bool error = false;

        int pending_phase_val = -1;

        while (bits_cnt < 32 && idx < count) {
            uint32_t t = get_time(symbols[idx]);
            uint8_t  l = get_level(symbols[idx]);
            
            if (l == Constants::RMT_LEVEL_IDLE && t > Constants::T_2TE_MAX) {
                break;
            }

            int dur_te = 0;

            if (t >= Constants::T_TE_MIN && t <= Constants::T_TE_MAX) {
                dur_te = 1;
            } else if (t >= Constants::T_2TE_MIN && t <= Constants::T_2TE_MAX) {
                dur_te = 2;
            } else {
                error = true;
                ESP_LOGV(TAG, "Timing Error at idx %d: L=%d T=%d", idx, l, t);
                break;
            }

            uint8_t first_half_level;
            
            if (pending_phase_val != -1) {
                first_half_level = static_cast<uint8_t>(pending_phase_val);
                pending_phase_val = -1;

                if (l == first_half_level) { error = true; break; }
                if (dur_te == 1) {
                    idx++;
                } else { // dur_te == 2
                    pending_phase_val = l;
                    idx++;
                }
            } else {
                first_half_level = l;
                
                if (dur_te == 1) {
                    idx++;
                    if (idx >= count) { error = true; break; }
                    
                    uint32_t t_next = get_time(symbols[idx]);
                    uint8_t  l_next = get_level(symbols[idx]);
                    
                    if (l_next == first_half_level) { error = true; break; }
                    
                    int dur_next = 0;
                    if (t_next >= Constants::T_TE_MIN && t_next <= Constants::T_TE_MAX) dur_next = 1;
                    else if (t_next >= Constants::T_2TE_MIN && t_next <= Constants::T_2TE_MAX) dur_next = 2;
                    else { error = true; break; }
                    
                    if (dur_next == 1) {
                        idx++;
                    } else { // 2
                        pending_phase_val = l_next;
                        idx++;
                    }
                } else { // dur_te == 2
                    error = true; break;
                }
            }

            // Active -> ... = 1
            // Idle -> ... = 0
            bool logic_val = (first_half_level == Constants::RMT_LEVEL_ACTIVE);
            rx_data = (rx_data << 1) | (logic_val ? 1 : 0);
            bits_cnt++;
        }

        if (!error && bits_cnt > 0) {
            DaliMessage msg;
            msg.data = rx_data;
            msg.length = bits_cnt;
            msg.timestamp = esp_timer_get_time();
            msg.is_backward = (bits_cnt == 8);

            std::lock_guard<std::mutex> lock(m_state_mutex);
            if (m_tx_state.active) {
                if (m_tx_state.bits == bits_cnt && m_tx_state.data == rx_data) {
                    msg.type = DaliEventType::TxCompleted;
                    ESP_LOGV(TAG, "TxCompleted:  %06lX", m_tx_state.data);
                } else {
                    msg.type = DaliEventType::CollisionDetected;
                    ESP_LOGD(TAG, "Collision: Exp %06lX Got %06lX", m_tx_state.data, rx_data);
                }
                m_tx_state.active = false;
            } else {
                msg.type = DaliEventType::FrameReceived;
            }

            if (bits_cnt != 8 && bits_cnt != 16 && bits_cnt != 24 && bits_cnt != 25) {
                ESP_LOGD(TAG, "Non-standard frame length: %d", bits_cnt);
            }

            xQueueSend(m_event_queue, &msg, 0);
            return bits_cnt;
        }
        if (error) {
            std::lock_guard<std::mutex> lock(m_state_mutex);
            if (m_tx_state.active) {
                DaliMessage msg{ .type = DaliEventType::CollisionDetected, .timestamp = esp_timer_get_time() };
                xQueueSend(m_event_queue, &msg, 0);
                m_tx_state.active = false;
                ESP_LOGD(TAG, "Collision (Decoding Error)");
            }
            return 0;
        }
        return 0;
    }

} // namespace daliMQTT::Driver