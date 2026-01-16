#ifndef DALIMQTT_DALIDRIVER_HXX
#define DALIMQTT_DALIDRIVER_HXX

namespace daliMQTT::driver {
    class DaliDriver {
    public:
        struct Config {
            gpio_num_t rx_pin;
            gpio_num_t tx_pin;
            uint32_t bus_timeout_ms = 500;
        };

        // todo (incomplete)

        static DaliDriver& Instance() {
            static DaliDriver instance;
            return instance;
        }

        esp_err_t init(const Config& config);

        // Non-blocking send
        common::DriverStatus sendAsync(uint32_t data, uint8_t bits);

        // Blocking send + wait for reply
        std::optional<uint8_t> sendQuery(uint32_t data, uint8_t bits);

        // Blocking send raw
        common::DriverStatus sendRaw(uint32_t data, uint8_t bits);

        // Get received events
        QueueHandle_t getEventQueue() const { return m_event_queue; }

    private:
        DaliDriver() = default;
        ~DaliDriver() = default;
        DaliDriver(const DaliDriver&) = delete;
        DaliDriver& operator=(const DaliDriver&) = delete;

        void generateManchSymbols(uint32_t data, uint8_t bits, std::vector<rmt_symbol_word_t>& symbols);
        int decodeManchSymbols(std::span<rmt_symbol_word_t> symbols, uint32_t& out_data);

        // ISR
        static void IRAM_ATTR gpio_isr_handler(void* arg);
        static bool IRAM_ATTR rmt_rx_done_callback(rmt_channel_handle_t channel, const rmt_rx_done_event_data_t *ed, void *user_data);

        rmt_channel_handle_t m_tx_channel = nullptr;
        rmt_channel_handle_t m_rx_channel = nullptr;
        rmt_encoder_handle_t m_copy_encoder = nullptr;

        // Synchronization
        Config m_config;
        SemaphoreHandle_t m_mutex = nullptr;
        QueueHandle_t m_event_queue = nullptr;

        struct RMTPulse {
            uint8_t level;
            uint32_t duration_us;
        };

        std::atomic<int64_t> m_last_activity_ts {0};
        static constexpr int64_t BUS_IDLE_TIME_US = 2000;

        // RX Buffer
        static constexpr size_t RX_BUFFER_SIZE = 64;
        std::vector<rmt_symbol_word_t> m_rx_buffer;
    };
}
#endif //DALIMQTT_DALIDRIVER_HXX