// Copyright (c) 2026 Alice-Trade Inc.
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef DALIMQTT_HOMEASSISTANTDISCOVERY_HXX
#define DALIMQTT_HOMEASSISTANTDISCOVERY_HXX

#include "dali/DaliDeviceRegistry.hxx"
#include "mqtt/MqttClient.hxx"
#include "system/ConfigStructure.hxx"

namespace daliMQTT {

class HomeAssistantDiscovery {
  public:
    HomeAssistantDiscovery(MqttClient& client, DaliDeviceRegistry& registry);
    ~HomeAssistantDiscovery() = default;

    void publishAll(const ConfigStructure& config) const;
    void publishLight(const ControlGear& gear, const ConfigStructure& config) const;
    void publishGroup(uint8_t busId, uint8_t groupId, const ConfigStructure& config) const;
    void publishSceneSelector(uint8_t busId, const ConfigStructure& config) const;
    void publishOtaUpdateEntity(const ConfigStructure& config) const;

  private:
    MqttClient& m_mqtt;
    DaliDeviceRegistry& m_registry;
};

} // namespace daliMQTT

#endif // DALIMQTT_HOMEASSISTANTDISCOVERY_HXX