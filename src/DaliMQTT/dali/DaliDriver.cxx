#include <esp_check.h>

namespace daliMQTT::driver {

    static const char* TAG = "DaliDriver";

    static constexpr uint32_t RMT_CLK_RES_HZ = 1 * 1000 * 1000;
    static constexpr uint32_t DALI_TE_TICKS = common::TimeElement_Us;

    static inline bool is_within(uint32_t val, uint32_t target, uint32_t percent = 20) {
        uint32_t delta = (target * percent) / 100;
        return (val >= target - delta) && (val <= target + delta);
    }

    esp_err_t DaliDriver::init(const Config& config) {
        m_config = config;
        m_mutex = xSemaphoreCreateMutex();
        m_event_queue = xQueueCreate(16, sizeof(common::DaliResult));
        m_rx_buffer.resize(RX_BUFFER_SIZE);
        
        rmt_tx_channel_config_t tx_chan_config = {
            .gpio_num = config.tx_pin,
            .clk_src = RMT_CLK_SRC_DEFAULT,
            .resolution_hz = RMT_CLK_RES_HZ,
            .mem_block_symbols = 64,
            .trans_queue_depth = 4,
            .flags = { .invert_out = false, .with_dma = false }
            // If optocoupler inverts, set invert_out = true
        };
        ESP_RETURN_ON_ERROR(rmt_new_tx_channel(&tx_chan_config, &m_tx_channel), TAG, "Failed to create TX");

        rmt_copy_encoder_config_t copy_encoder_config = {};
        ESP_RETURN_ON_ERROR(rmt_new_copy_encoder(&copy_encoder_config, &m_copy_encoder), TAG, "Failed encoder");

        ESP_RETURN_ON_ERROR(rmt_enable(m_tx_channel), TAG, "Failed enable TX");

        rmt_rx_channel_config_t rx_chan_config = {
            .gpio_num = config.rx_pin,
            .clk_src = RMT_CLK_SRC_DEFAULT,
            .resolution_hz = RMT_CLK_RES_HZ,
            .mem_block_symbols = 64,
            .flags = { .invert_in = false, .with_dma = false }
        };
        ESP_RETURN_ON_ERROR(rmt_new_rx_channel(&rx_chan_config, &m_rx_channel), TAG, "Failed create RX");
        
        rmt_receive_config_t receive_config = {
            .signal_range_min_ns = 50 * 1000,
            .signal_range_max_ns = 200000 * 1000,
        };
        rmt_rx_event_callbacks_t cbs = {
            .on_recv_done = rmt_rx_done_callback
        };
        ESP_RETURN_ON_ERROR(rmt_register_rx_event_callbacks(m_rx_channel, &cbs, this), TAG, "Failed RX CB");
        ESP_RETURN_ON_ERROR(rmt_enable(m_rx_channel), TAG, "Failed enable RX");

        gpio_config_t io_conf = {
            .pin_bit_mask = (1ULL << config.rx_pin),
            .mode = GPIO_MODE_INPUT,
            .pull_up_en = GPIO_PULLUP_DISABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_ANYEDGE
        };
        gpio_config(&io_conf);
        gpio_install_isr_service(0);
        gpio_isr_handler_add(config.rx_pin, gpio_isr_handler, this);

        rmt_receive(m_rx_channel, m_rx_buffer.data(), m_rx_buffer.size(), &receive_config);

        ESP_LOGI(TAG, "DALI RMT Driver Initialized (TX:%d, RX:%d)", config.tx_pin, config.rx_pin);
        return ESP_OK;
    }

    void IRAM_ATTR DaliDriver::gpio_isr_handler(void* arg) {
        auto* self = static_cast<DaliDriver*>(arg);
        self->m_last_activity_ts.store(esp_timer_get_time(), std::memory_order_relaxed);
    }

    bool IRAM_ATTR DaliDriver::rmt_rx_done_callback(rmt_channel_handle_t channel, const rmt_rx_done_event_data_t *ed, void *user_data) {
        auto* self = static_cast<DaliDriver*>(user_data);
        BaseType_t high_task_wakeup = pdFALSE;
        rmt_receive(self->m_rx_channel, self->m_rx_buffer.data(), self->m_rx_buffer.size(), nullptr);
        return high_task_wakeup == pdTRUE;
    }F

    void DaliDriver::generateManchSymbols(uint32_t data, uint8_t bits, std::vector<rmt_symbol_word_t>& symbols) {
        symbols.clear();
        symbols.reserve(bits * 2 + 4);
        
        symbols.push_back({
            .level0 = 0, .duration0 = DALI_TE_TICKS,
            .level1 = 1, .duration1 = DALI_TE_TICKS
        });

        for (int i = bits - 1; i >= 0; --i) {
            bool bit = (data >> i) & 1;
            if (bit) {
                // Low -> High
                symbols.push_back({
                    .level0 = 0, .duration0 = DALI_TE_TICKS,
                    .level1 = 1, .duration1 = DALI_TE_TICKS
                });
            } else {
                // High -> Low
                symbols.push_back({
                    .level0 = 1, .duration0 = DALI_TE_TICKS,
                    .level1 = 0, .duration1 = DALI_TE_TICKS
                });
            }
        }

        // Stop Bits
        symbols.push_back({
            .level0 = 1, .duration0 = DALI_TE_TICKS * 4, // 4 TE Idle
            .level1 = 1, .duration1 = 0 // End
        });
    }

    common::DriverStatus DaliDriver::sendRaw(uint32_t data, uint8_t bits) {
        std::lock_guard<SemaphoreHandle_t> lock(m_mutex);

        // Collision Avoidance
        int64_t now = esp_timer_get_time();
        int64_t last = m_last_activity_ts.load(std::memory_order_relaxed);
        if ((now - last) < BUS_IDLE_TIME_US) {
             vTaskDelay(pdMS_TO_TICKS(3));
             now = esp_timer_get_time();
             last = m_last_activity_ts.load(std::memory_order_relaxed);
             if ((now - last) < BUS_IDLE_TIME_US) {
                 return common::DriverStatus::BusBusy;
             }
        }
        std::vector<rmt_symbol_word_t> items;
        generateManchSymbols(data, bits, items);
        ESP_ERROR_CHECK(rmt_transmit(m_tx_channel, m_copy_encoder, items.data(), items.size(), &m_tx_config));
        ESP_ERROR_CHECK(rmt_tx_wait_all_done(m_tx_channel, -1));

        return common::DriverStatus::Ok;
    }

    std::optional<uint8_t> DaliDriver::sendQuery(uint32_t data, uint8_t bits) {
        if (sendRaw(data, bits) != common::DriverStatus::Ok) {
            return std::nullopt;
        }

        common::DaliResult rx_res;
        if (xQueueReceive(m_event_queue, &rx_res, pdMS_TO_TICKS(15)) == pdTRUE) {
            if (rx_res.bit_length == 8) {
                return static_cast<uint8_t>(rx_res.data);
            }
        }

        return std::nullopt;
    }

    int DaliDriver::decodeManchSymbols(std::span<rmt_symbol_word_t> symbols, uint32_t& out_data) {
        static std::vector<Pulse> pulses;
        pulses.clear();
        pulses.reserve(64);

        for (const auto& sym : symbols) {
            if (sym.duration0 > 0) pulses.push_back({static_cast<uint8_t>(sym.level0), static_cast<uint32_t>(sym.duration0)});
            else break;

            if (sym.duration1 > 0) pulses.push_back({static_cast<uint8_t>(sym.level1), static_cast<uint32_t>(sym.duration1)});
            else break;
        }

        if (pulses.empty()) return -1;

        constexpr uint32_t HALF_BIT_US = 416;
        constexpr uint32_t FULL_BIT_US = 833;
        constexpr uint32_t SAMPLE_POINT_1 = 208; // 25% of bit window
        constexpr uint32_t SAMPLE_POINT_2 = 624; // 75% of bit window

        out_data = 0;
        int bit_count = 0;

        size_t p_idx = 0;

        if (pulses[p_idx].level == 1) {
            p_idx++;
            if (p_idx >= pulses.size()) return -1;
        }

        const auto& start_low = pulses[p_idx];
        if (start_low.level != 0) return -1;
        if (start_low.duration_us < 290 || start_low.duration_us > 540) {
            // Detection Error: Not a valid Start bit preamble
            return -1;
        }

        uint32_t current_time_us = 0;

        auto get_level_at = [&](uint32_t time_us) -> int {
            uint32_t accum_time = 0;
            for (size_t i = 0; i < pulses.size(); ++i) {
                accum_time += pulses[i].duration_us;
                if (accum_time > time_us) {
                    return pulses[i].level;
                }
            }
            return -1; // End of frame
        };

        current_time_us = FULL_BIT_US;

        while (bit_count < 25) {
            uint32_t t1 = current_time_us + SAMPLE_POINT_1; // 25%
            uint32_t t2 = current_time_us + SAMPLE_POINT_2; // 75%

            int level1 = get_level_at(t1);
            int level2 = get_level_at(t2);
            if (level1 == -1) break;

            // Logic 1: Low -> High
            // Logic 0: High -> Low
            int decoded_bit = -1;

            if (level1 == 0 && level2 == 1) {
                decoded_bit = 1;
            } else if (level1 == 1 && level2 == 0) {
                decoded_bit = 0;
            } else if (level1 == 1 && level2 == 1) {
                // High -> High
                break;
            } else {
                // Low -> Low: Illegal state
                return -2;
            }
            out_data = (out_data << 1) | decoded_bit;
            bit_count++;
            current_time_us += FULL_BIT_US;
        }

        // Validate Stop Bit / Frame
        // DALI allowed lengths: 8 (Back), 16 (Cmd), 24 (Input)
        if (bit_count == 8 || bit_count == 16 || bit_count == 24) {
             return bit_count;
        }

        // Noise or incomplete frame
        return -3;
    }
}