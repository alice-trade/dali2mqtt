#ifndef DALIMQTT_DALICOMMANDPROCESSOR_HXX
#define DALIMQTT_DALICOMMANDPROCESSOR_HXX

namespace daliMQTT {
    struct MqttMessage {
        std::string topic;
        std::string payload;
    };

    class DaliCommandProcessor {
        public:
            DaliCommandProcessor(const DaliCommandProcessor&) = delete;
            DaliCommandProcessor& operator=(const DaliCommandProcessor&) = delete;
            static DaliCommandProcessor& getInstance() {
                static DaliCommandProcessor instance;
                return instance;
            }

            void init();
            bool enqueueMqttMessage(const char* topic, int topic_len, const char* data, int data_len) const;

        private:
            DaliCommandProcessor() = default;

            [[noreturn]] static void CommandProcessTask(void* arg);

            QueueHandle_t m_queue{nullptr};
    };
} // daliMQTT

#endif //DALIMQTT_DALICOMMANDPROCESSOR_HXX