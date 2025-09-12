#ifndef DALIMQTT_MQTTDISCOVERY_HXX
#define DALIMQTT_MQTTDISCOVERY_HXX
#include <cstdint>
#include <string>

namespace daliMQTT
{
    class MQTTDiscovery
    {
        public:
            MQTTDiscovery();
            void publishAllDevices();

        private:
            void publishLight(const std::string& type, std::uint8_t id);

            std::string base_topic;
            std::string availability_topic;
            std::string device_name;
    };
} // daliMQTT

#endif //DALIMQTT_MQTTDISCOVERY_HXX