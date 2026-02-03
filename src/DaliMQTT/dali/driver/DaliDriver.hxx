#ifndef DALIMQTT_DRIVER_DALIDRIVER_HXX
#define DALIMQTT_DRIVER_DALIDRIVER_HXX

namespace daliMQTT::Driver {

    /**
     * @brief Type of event received from the bus or transmission status.
     */
    enum class DaliEventType {
        FrameReceived,      // Frame successfully received and decoded
        FrameError,         // Decoding or timing error detected
        TxCompleted,        // Transmission successful
        CollisionDetected   // Collision found
    };

    /**
     * @brief Raw DALI message structure for communication between Adapter and Driver.
     */
    struct DaliMessage {
        uint32_t data{0};       // Raw data
        uint8_t length{0};      // Bit count (8, 16, or 24)
        bool is_backward{false};// true if it's a backward frame (8-bit response)
        DaliEventType type{DaliEventType::FrameReceived};
        int64_t timestamp{0};  // Reception timestamp (esp_timer_get_time)
    };

    /**
     * @brief Driver configuration settings.
     */
    struct DaliDriverConfig {
        gpio_num_t rx_pin;
        gpio_num_t tx_pin;
        uint32_t resolution_hz{1000000};
    };

    class DaliDriver {
        public:
            DaliDriver();
            ~DaliDriver();

            DaliDriver(const DaliDriver&) = delete;
            DaliDriver& operator=(const DaliDriver&) = delete;

            /**
             * @brief Initialize RMT channels, internal queues, and background tasks.
             * @return ESP_OK on success, or appropriate error code.
             */
            esp_err_t init(const DaliDriverConfig& config);

            /**
             * @brief Asynchronously send a DALI message.
             * Places the message into the TX queue.
             *
             * @param data Raw payload
             * @param bits Bit length
             * @return ESP_OK if queued, ESP_FAIL if the queue is full.
             */
            esp_err_t sendAsync(uint32_t data, uint8_t bits) const;

            /**
             * @brief Returns the handle to the event queue.
             * The DaliAdapter should monitor this queue for incoming frames and errors.
             */
            [[nodiscard]] QueueHandle_t getEventQueue() const { return m_event_queue; }

            /**
             * @brief Clears the RX event queue.
             * Useful before sending a Query to ensure the next received item is a fresh response.
             */
            void flushRxQueue() const;

        private:
            struct Constants {
                static constexpr uint32_t DALI_BIT_TIME_US = 834;
                static constexpr uint32_t DALI_HALF_BIT_TIME_US = 417;

                static constexpr uint32_t T_TE = 417;        // Half bit time (1/2400 s)
                static constexpr uint32_t T_TE_MIN = 300;
                static constexpr uint32_t T_TE_MAX = 525;
                static constexpr uint32_t T_2TE_MIN = 700;
                static constexpr uint32_t T_2TE_MAX = 960;

                static constexpr uint8_t RMT_LEVEL_IDLE = 0;
                static constexpr uint8_t RMT_LEVEL_ACTIVE = 1;

                static constexpr uint32_t RX_IDLE_THRESH_NS = 1800000;
                static constexpr uint32_t TX_WATCHDOG_TIMEOUT_US = 50'000;
            };


            bool m_initialized{false};
            DaliDriverConfig m_config{};

            rmt_channel_handle_t m_tx_channel{nullptr};
            rmt_channel_handle_t m_rx_channel{nullptr};
            rmt_encoder_handle_t m_dali_encoder{nullptr};

            QueueHandle_t m_tx_queue{nullptr};
            QueueHandle_t m_event_queue{nullptr};
            TaskHandle_t m_driver_task{nullptr};


            struct TxState {
                bool active{false};
                uint32_t data{0};
                uint8_t bits{0};
                int64_t start_ts{0};
            } m_tx_state;

            std::mutex m_state_mutex;

            static constexpr size_t RX_BUFFER_SIZE = 128;
            rmt_symbol_word_t* m_rx_buffer{nullptr};

            static bool rmt_rx_done_callback(rmt_channel_handle_t rx_chan, const rmt_rx_done_event_data_t *edata, void *user_ctx);
            static bool rmt_tx_done_callback(rmt_channel_handle_t tx_chan, const rmt_tx_done_event_data_t *edata, void *user_ctx);

            /**
             * @brief ISR callback triggered when RMT RX completes reception.
             * Decodes RMT symbols into DALI bits and pushes results to m_event_queue.
             */
            void processRxSymbols(const rmt_symbol_word_t* symbols, size_t count);

            /**
             * @brief Configure RMT TX channel and Manchester encoder parameters.
             */
            esp_err_t setupTx();

            /**
             * @brief Configure RMT RX channel and timing filters.
             */
            esp_err_t setupRx();

            static void driverTaskWrapper(void* arg);
            [[noreturn]] void driverTaskLoop();
            static rmt_symbol_word_t make_symbol(uint32_t duration, uint8_t level);
            /**
             * @brief Helper to encode DALI frame to RMT symbols
             */
            std::vector<rmt_symbol_word_t> encodeFrame(uint32_t data, uint8_t bits) const;
    };
} // namespace daliMQTT::Driver

#endif // DALIMQTT_DRIVER_DALIDRIVER_HXX