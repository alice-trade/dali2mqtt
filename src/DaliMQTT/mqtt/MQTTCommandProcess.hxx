#ifndef DALIMQTT_DALICOMMANDPROCESSOR_HXX
#define DALIMQTT_DALICOMMANDPROCESSOR_HXX

namespace daliMQTT {
    struct MqttMessage {
        std::string topic;
        std::string payload;
    };

    class MQTTCommandProcess {
        public:
            MQTTCommandProcess(const MQTTCommandProcess&) = delete;
            MQTTCommandProcess& operator=(const MQTTCommandProcess&) = delete;
            static MQTTCommandProcess& getInstance() {
                static MQTTCommandProcess instance;
                return instance;
            }

            void init();
            bool enqueueMqttMessage(const char* topic, int topic_len, const char* data, int data_len) const;

        private:
            MQTTCommandProcess() = default;

            [[noreturn]] static void CommandProcessTask(void* arg);

            QueueHandle_t m_queue{nullptr};
    };
} // daliMQTT

#endif //DALIMQTT_DALICOMMANDPROCESSOR_HXX