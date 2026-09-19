// Copyright (c) 2026 Alice-Trade Inc.
// SPDX-License-Identifier: GPL-3.0-or-later

#include "mqtt/HomeAssistantDiscovery.hxx"
#include "utils/DaliLongAddrConversions.hxx"
#include <ArduinoJson.h>
#include <esp_log.h>

namespace daliMQTT {

static constexpr char TAG[] = "HADiscovery";

HomeAssistantDiscovery::HomeAssistantDiscovery(MqttClient& client, DaliDeviceRegistry& registry)
    : m_mqtt(client), m_registry(registry) {}

void HomeAssistantDiscovery::publishAll(const ConfigStructure& config) const {
    if (!config.hassDiscoveryEnabled || !m_mqtt.isConnected())
        return;

    ESP_LOGI(TAG, "Publishing Home Assistant MQTT Discovery (Prefix: %s)...", config.hassDiscoveryPrefix.c_str());

    const auto devices = m_registry.getDevicesSnapshot();
    for (const auto& dev : devices) {
        if (const auto* gear = etl::get_if<ControlGear>(&dev)) {
            publishLight(*gear, config);
        }
    }

    for (uint8_t b = 0; b < DaliDeviceRegistry::BUS_COUNT; ++b) {
        for (uint8_t g = 0; g < 16; ++g) {
            publishGroup(b, g, config);
        }
        publishSceneSelector(b, config);
    }

    publishOtaUpdateEntity(config);
    
    ESP_LOGI(TAG, "Home Assistant Discovery published successfully.");
}

void HomeAssistantDiscovery::publishLight(const ControlGear& gear, const ConfigStructure& config) const {
    const auto addrStr = utils::longAddressToString(gear.longAddress);
    const uint8_t busId = gear.internalAddress.bus();
    const uint8_t shortAddr = gear.internalAddress.shortAddr();

    char uniqueId[64];
    snprintf(uniqueId, sizeof(uniqueId), "dali_%s_b%d_la_%s", config.clientId.c_str(), busId, addrStr.data());

    char discTopic[128];
    snprintf(discTopic, sizeof(discTopic), "%s/light/%s/config", config.hassDiscoveryPrefix.c_str(), uniqueId);

    char stateTopic[128], cmdTopic[128], avTopic[128];
    snprintf(stateTopic, sizeof(stateTopic), "%s/light/%s/state", config.mqttBaseTopic.c_str(), addrStr.data());
    snprintf(cmdTopic, sizeof(cmdTopic), "%s/light/%s/set", config.mqttBaseTopic.c_str(), addrStr.data());
    snprintf(avTopic, sizeof(avTopic), "%s%s", config.mqttBaseTopic.c_str(), CONFIG_DALI2MQTT_MQTT_AVAILABILITY_TOPIC);

    JsonDocument doc;
    char nameBuf[64];
    snprintf(nameBuf, sizeof(nameBuf), "DALI Light %s (B%d:SA%d)", addrStr.data(), busId, shortAddr);

    doc["name"] = nameBuf;
    doc["unique_id"] = uniqueId;
    doc["schema"] = "json";
    doc["state_topic"] = stateTopic;
    doc["command_topic"] = cmdTopic;
    doc["brightness"] = true;
    doc["availability_topic"] = avTopic;
    doc["payload_available"] = CONFIG_DALI2MQTT_MQTT_PAYLOAD_ONLINE;
    doc["payload_not_available"] = CONFIG_DALI2MQTT_MQTT_PAYLOAD_OFFLINE;

    if (gear.color.has_value()) {
        const JsonArray modes = doc["supported_color_modes"].to<JsonArray>();
        if (gear.color->supportsTc) {
            modes.add("color_temp");
            doc["min_mireds"] = gear.color->minMireds.value_or(153);
            doc["max_mireds"] = gear.color->maxMireds.value_or(500);
        }
        if (gear.color->supportsRgb) {
            modes.add("rgb");
        }
    }

    const auto devObj = doc["device"].to<JsonObject>();
    char devIdent[64];
    snprintf(devIdent, sizeof(devIdent), "dali_light_%s", addrStr.data());
    devObj["identifiers"].add(devIdent);
    devObj["name"] = nameBuf;
    devObj["via_device"] = config.clientId.c_str();

    char payloadBuf[768];
    serializeJson(doc, payloadBuf, sizeof(payloadBuf));
    m_mqtt.publish(discTopic, payloadBuf, 1, true);

    char faultUniqueId[64];
    snprintf(faultUniqueId, sizeof(faultUniqueId), "dali_%s_b%d_la_%s_fault", config.clientId.c_str(), busId,
             addrStr.data());

    char faultDiscTopic[128];
    snprintf(faultDiscTopic, sizeof(faultDiscTopic), "%s/binary_sensor/%s/config", config.hassDiscoveryPrefix.c_str(), faultUniqueId);

    JsonDocument faultDoc;
    faultDoc["name"] = "Lamp Fault";
    faultDoc["unique_id"] = faultUniqueId;
    faultDoc["device_class"] = "problem";
    faultDoc["entity_category"] = "diagnostic";
    faultDoc["state_topic"] = stateTopic;
    faultDoc["value_template"] = "{{ 'ON' if value_json.lamp_failure else 'OFF' }}";
    faultDoc["payload_on"] = "ON";
    faultDoc["payload_off"] = "OFF";
    faultDoc["device"]["identifiers"].add(devIdent);

    char faultPayload[384];
    serializeJson(faultDoc, faultPayload, sizeof(faultPayload));
    m_mqtt.publish(faultDiscTopic, faultPayload, 1, true);
}

void HomeAssistantDiscovery::publishGroup(const uint8_t busId, const uint8_t groupId,
                                          const ConfigStructure& config) const {
    char uniqueId[64];
    snprintf(uniqueId, sizeof(uniqueId), "dali_%s_b%d_grp_%d", config.clientId.c_str(), busId, groupId);

    char discTopic[128];
    snprintf(discTopic, sizeof(discTopic), "%s/light/%s/config", config.hassDiscoveryPrefix.c_str(), uniqueId);

    char stateTopic[128], cmdTopic[128], avTopic[128];
    snprintf(stateTopic, sizeof(stateTopic), "%s/light/bus/%d/group/%d/state", config.mqttBaseTopic.c_str(), busId,
             groupId);
    snprintf(cmdTopic, sizeof(cmdTopic), "%s/light/bus/%d/group/%d/set", config.mqttBaseTopic.c_str(), busId, groupId);
    snprintf(avTopic, sizeof(avTopic), "%s%s", config.mqttBaseTopic.c_str(), CONFIG_DALI2MQTT_MQTT_AVAILABILITY_TOPIC);

    JsonDocument doc;
    char nameBuf[48];
    snprintf(nameBuf, sizeof(nameBuf), "DALI Bus %d Group %d", busId, groupId);

    doc["name"] = nameBuf;
    doc["unique_id"] = uniqueId;
    doc["schema"] = "json";
    doc["state_topic"] = stateTopic;
    doc["command_topic"] = cmdTopic;
    doc["brightness"] = true;
    doc["availability_topic"] = avTopic;

    const auto devObj = doc["device"].to<JsonObject>();
    devObj["identifiers"].add(config.clientId.c_str());
    devObj["name"] = config.clientId.c_str();
    devObj["model"] = "ESP32 DALI Gateway";

    char payloadBuf[384];
    serializeJson(doc, payloadBuf, sizeof(payloadBuf));
    m_mqtt.publish(discTopic, payloadBuf, 1, true);
}

void HomeAssistantDiscovery::publishSceneSelector(const uint8_t busId, const ConfigStructure& config) const {
    char uniqueId[64];
    snprintf(uniqueId, sizeof(uniqueId), "dali_%s_b%d_scenes", config.clientId.c_str(), busId);

    char discTopic[128];
    snprintf(discTopic, sizeof(discTopic), "%s/select/%s/config", config.hassDiscoveryPrefix.c_str(), uniqueId);

    char cmdTopic[128], avTopic[128];
    snprintf(cmdTopic, sizeof(cmdTopic), "%s/scene/bus/%d/set", config.mqttBaseTopic.c_str(), busId);
    snprintf(avTopic, sizeof(avTopic), "%s%s", config.mqttBaseTopic.c_str(), CONFIG_DALI2MQTT_MQTT_AVAILABILITY_TOPIC);

    JsonDocument doc;
    char nameBuf[48];
    snprintf(nameBuf, sizeof(nameBuf), "DALI Bus %d Scenes", busId);

    doc["name"] = nameBuf;
    doc["unique_id"] = uniqueId;
    doc["command_topic"] = cmdTopic;
    doc["availability_topic"] = avTopic;

    const JsonArray options = doc["options"].to<JsonArray>();
    for (int i = 0; i < 16; ++i) {
        char sName[16];
        snprintf(sName, sizeof(sName), "Scene %d", i);
        options.add(sName);
    }

    doc["device"]["identifiers"].add(config.clientId.c_str());

    char payloadBuf[384];
    serializeJson(doc, payloadBuf, sizeof(payloadBuf));
    m_mqtt.publish(discTopic, payloadBuf, 1, true);
}

void HomeAssistantDiscovery::publishOtaUpdateEntity(const ConfigStructure& config) const {
    char uniqueId[64];
    snprintf(uniqueId, sizeof(uniqueId), "dali_%s_fw_update", config.clientId.c_str());

    char discTopic[128];
    snprintf(discTopic, sizeof(discTopic), "%s/update/%s/config", config.hassDiscoveryPrefix.c_str(), uniqueId);

    char stateTopic[128], cmdTopic[128];
    snprintf(stateTopic, sizeof(stateTopic), "%s/update/state", config.mqttBaseTopic.c_str());
    snprintf(cmdTopic, sizeof(cmdTopic), "%s/update/install", config.mqttBaseTopic.c_str());

    JsonDocument doc;
    doc["name"] = "Firmware Update";
    doc["unique_id"] = uniqueId;
    doc["device_class"] = "firmware";
    doc["state_topic"] = stateTopic;
    doc["command_topic"] = cmdTopic;

    const JsonObject devObj = doc["device"].to<JsonObject>();
    devObj["identifiers"].add(config.clientId.c_str());
    devObj["name"] = config.clientId.c_str();
    devObj["sw_version"] = DALIMQTT_VERSION;
    devObj["model"] = "ESP32 DALI Bridge";
    devObj["manufacturer"] = "Alice-Trade";

    char payloadBuf[384];
    serializeJson(doc, payloadBuf, sizeof(payloadBuf));
    m_mqtt.publish(discTopic, payloadBuf, 1, true);
}
} // namespace daliMQTT