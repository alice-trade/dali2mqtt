// Copyright (c) 2026 Alice-Trade Inc.
// SPDX-License-Identifier: GPL-2.0-or-later

#ifndef DALIMQTT_HA_DISCOVERY_HXX
#define DALIMQTT_HA_DISCOVERY_HXX
#include "dali/DaliCommon.hxx"

namespace daliMQTT
{
    struct DeviceNameEntry {
        DaliLongAddress_t addr;
        std::string name;
    };
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
            std::vector<DeviceNameEntry> device_names;
    };
} // daliMQTT

#endif // DALIMQTT_HA_DISCOVERY_HXX