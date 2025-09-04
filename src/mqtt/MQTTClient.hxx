#ifndef DALIMQTT_MQTTCLIENT_HXX
#define DALIMQTT_MQTTCLIENT_HXX

#include "mqtt_client.h"
#include <string>
#include <functional>

namespace daliMQTT
{
    class MQTTClient {
        public:
            MQTTClient(const MQTTClient&) = delete;
            MQTTClient& operator=(const MQTTClient&) = delete;

            static MQTTClient& getInstance();

            void init(const std::string& uri, const std::string& client_id, const std::string& availability_topic);
            void connect() const;
            void disconnect() const;

            void publish(const std::string& topic, const std::string& payload, int qos = 0, bool retain = false) const;
            void subscribe(const std::string& topic, int qos = 0) const;

            // Callbacks
            std::function<void()> onConnected;
            std::function<void()> onDisconnected;
            std::function<void(const std::string& topic, const std::string& data)> onData;

        private:
            MQTTClient() = default;

            static void mqttEventHandler(void* handler_args, esp_event_base_t base, int32_t event_id, void* event_data);

            esp_mqtt_client_handle_t client_handle{nullptr};
        };
} // daliMQTT

#endif //DALIMQTT_MQTTCLIENT_HXX