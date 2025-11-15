#ifndef DALIMQTT_MQTTDISCOVERY_HXX
#define DALIMQTT_MQTTDISCOVERY_HXX
#include "DaliTypes.hxx"

namespace daliMQTT
{
    class MQTTDiscovery
    {
        public:
            MQTTDiscovery();
            void publishAllDevices();

        private:
            void publishLight(DaliLongAddress_t long_addr);
            void publishGroup(uint8_t group_id);
            void publishSceneSelector();

            std::string base_topic;
            std::string availability_topic;
            std::string bridge_public_name;
            std::unordered_map<std::string, std::string> device_identification;
    };
} // daliMQTT

#endif //DALIMQTT_MQTTDISCOVERY_HXX