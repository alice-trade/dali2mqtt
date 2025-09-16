#ifndef DALIMQTT_MQTTDISCOVERY_HXX
#define DALIMQTT_MQTTDISCOVERY_HXX

namespace daliMQTT
{
    class MQTTDiscovery
    {
        public:
            MQTTDiscovery();
            void publishAllDevices();

        private:
            void publishLight(const std::string& type, std::uint8_t id);
            void publishSceneSelector();

            std::string base_topic;
            std::string availability_topic;
            std::string bridge_public_name;
            std::unordered_map<std::string, std::string> device_identification;
    };
} // daliMQTT

#endif //DALIMQTT_MQTTDISCOVERY_HXX