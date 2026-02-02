#ifndef DALIMQTT_MQTTCLIENT_HXX
#define DALIMQTT_MQTTCLIENT_HXX

#include "mqtt_client.h"

namespace daliMQTT
{
    enum class MqttStatus {
        DISCONNECTED,
        CONNECTING,
        CONNECTED
    };

    class MQTTClient {
        public:
            MQTTClient(const MQTTClient&) = delete;
            MQTTClient& operator=(const MQTTClient&) = delete;

            static MQTTClient& Instance() {
                static MQTTClient instance;
                return instance;
            }
            void init(const std::string& uri, const std::string& client_id, const std::string& availability_topic, const std::string& username, const std::string& password);

            void connect();
            void disconnect();

            [[nodiscard]] MqttStatus getStatus() const;

            void publish(const std::string& topic, const std::string& payload, int qos = 0, bool retain = false) const;
            void subscribe(const std::string& topic, int qos = 0) const;
            void reloadConfig(const std::string& uri, const std::string& client_id, const std::string& username, const std::string& password, const std::string& availability_topic);
            // Callbacks
            std::function<void()> onConnected;
            std::function<void()> onDisconnected;

        private:
            MQTTClient() = default;

            static void mqttEventHandler(void* handler_args, esp_event_base_t base, int32_t event_id, void* event_data);

            esp_mqtt_client_handle_t client_handle{nullptr};
            std::atomic<MqttStatus> status{MqttStatus::DISCONNECTED};
    };
} // daliMQTT

#endif //DALIMQTT_MQTTCLIENT_HXX