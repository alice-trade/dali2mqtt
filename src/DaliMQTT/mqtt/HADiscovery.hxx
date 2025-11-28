#ifndef DALIMQTT_HA_DISCOVERY_HXX
#define DALIMQTT_HA_DISCOVERY_HXX
#include "DaliTypes.hxx"

namespace daliMQTT
{
    class MQTTHomeAssistantDiscovery
    {
        public:
            MQTTHomeAssistantDiscovery();
            void publishAllDevices();

        private:
            void publishLight(DaliLongAddress_t long_addr);
            void publishGroup(uint8_t group_id);
            void publishSceneSelector();

            std::string base_topic;
            std::string availability_topic;
            std::string bridge_public_name;
            std::map<std::string, std::string> device_identification;
    };
} // daliMQTT

#endif // DALIMQTT_HA_DISCOVERY_HXX