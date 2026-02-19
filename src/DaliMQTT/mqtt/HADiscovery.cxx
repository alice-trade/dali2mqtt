// Copyright (c) 2026 Alice-Trade Inc.
// SPDX-License-Identifier: GPL-2.0-or-later

#include "mqtt/HADiscovery.hxx"
#include <dali/DaliGroupManagement.hxx>
#include "mqtt/MQTTClient.hxx"
#include "system/ConfigManager.hxx"
#include "dali/DaliDeviceController.hxx"
#include "utils/DaliLongAddrConversions.hxx"
#include "utils/StringUtils.hxx"

namespace daliMQTT
{
    MQTTHomeAssistantDiscovery::MQTTHomeAssistantDiscovery() {
        const auto config = ConfigManager::Instance().getConfig();

        base_topic = config.mqtt_base_topic;
        availability_topic = utils::stringFormat("%s%s", base_topic.c_str(), CONFIG_DALI2MQTT_MQTT_AVAILABILITY_TOPIC);
        bridge_public_name = utils::stringFormat("DALI-MQTT Bridge (%s)", config.client_id.c_str());

        JsonDocument names_root;
        if (!deserializeJson(names_root, config.dali_device_identificators)) {
            if (names_root.is<JsonObject>()) {
                for (JsonPair kv : names_root.as<JsonObject>()) {
                    if (kv.value().is<const char*>()) {
                        device_identification[kv.key().c_str()] = kv.value().as<const char*>();
                    }
                }
            }
        }
    }

    void MQTTHomeAssistantDiscovery::publishAllDevices() {
        auto devices = DaliDeviceController::Instance().getDevices();
        for (const auto& [long_addr,dev] : devices) {
            if (std::holds_alternative<InputDevice>(dev)) continue;

            publishLight(long_addr);
        }

        for (uint8_t i = 0; i < 16; ++i) {
            publishGroup(i);
        }

        publishSceneSelector();
    }

    void MQTTHomeAssistantDiscovery::publishLight(const DaliLongAddress_t long_addr) {
        const auto& mqtt = MQTTClient::Instance();

        const auto addr_str_arr = utils::longAddressToString(long_addr);
        const std::string addr_str(addr_str_arr.data());

        const std::string object_id = utils::stringFormat("dali_light_%s", addr_str.c_str());
        const std::string discovery_topic = utils::stringFormat("homeassistant/light/%s/config", object_id.c_str());
        const std::string device_status_topic = utils::stringFormat("%s/light/%s/status", base_topic.c_str(), addr_str.c_str());

        std::string readable_name;
        const auto it = device_identification.find(addr_str);
        if (it != device_identification.end() && !it->second.empty()) {
            readable_name = it->second;
        }
        if (readable_name.empty()) {
            readable_name = utils::stringFormat("DALI Device %s", addr_str.c_str());
        }

        JsonDocument doc;

        ControlGear dev_copy;
        {
            const auto devices = DaliDeviceController::Instance().getDevices();
            if (devices.contains(long_addr)) {
                if (const auto* gear = std::get_if<ControlGear>(&devices.at(long_addr))) {
                    dev_copy = *gear;
                } else {
                    return;
                }
            } else {
                return;
            }
        }

        doc["name"] = readable_name;
        doc["unique_id"] = object_id;
        doc["schema"] = "json";
        doc["command_topic"] = utils::stringFormat("%s/light/%s/set", base_topic.c_str(), addr_str.c_str());
        doc["state_topic"] = utils::stringFormat("%s/light/%s/state", base_topic.c_str(), addr_str.c_str());
        doc["brightness"] = true;

        if (dev_copy.device_type.has_value() && dev_copy.device_type.value() == 8 && dev_copy.color.has_value()) {
            const auto& c = dev_copy.color.value();
            JsonArray color_modes = doc["supported_color_modes"].to<JsonArray>();
            if (c.supports_tc) {
                color_modes.add("color_temp");
                doc["min_mireds"] = c.min_mireds.value_or(153);
                doc["max_mireds"] = c.max_mireds.value_or(500);
            }
            if (c.supports_rgb) {
                color_modes.add("rgb");
            }
        }

        JsonArray av_list = doc["availability"].to<JsonArray>();

        JsonObject av_bridge = av_list.add<JsonObject>();
        av_bridge["topic"] = availability_topic;
        av_bridge["payload_available"] = CONFIG_DALI2MQTT_MQTT_PAYLOAD_ONLINE;
        av_bridge["payload_not_available"] = CONFIG_DALI2MQTT_MQTT_PAYLOAD_OFFLINE;

        JsonObject av_device = av_list.add<JsonObject>();
        av_device["topic"] = device_status_topic;
        av_device["payload_available"] = CONFIG_DALI2MQTT_MQTT_PAYLOAD_ONLINE;
        av_device["payload_not_available"] = CONFIG_DALI2MQTT_MQTT_PAYLOAD_OFFLINE;

        doc["availability_mode"] = "all";

        JsonObject device = doc["device"].to<JsonObject>();
        device["identifiers"] = bridge_public_name;
        device["name"] = bridge_public_name;
        device["model"] = "ESP32 DALI Bridge";
        device["manufacturer"] = "DALI-MQTT4ESP";
        device["sw_version"] = DALIMQTT_VERSION;

        std::string json_payload;
        serializeJson(doc, json_payload);
        mqtt.publish(discovery_topic, json_payload, 1, true);
    }

    void MQTTHomeAssistantDiscovery::publishGroup(uint8_t group_id) {
        const auto& mqtt = MQTTClient::Instance();
        const auto config = ConfigManager::Instance().getConfig();
        const std::string object_id = utils::stringFormat("dali_group_%s_%d", config.client_id.c_str(), group_id);
        const std::string discovery_topic = utils::stringFormat("homeassistant/light/%s/config", object_id.c_str());
        const std::string readable_name = utils::stringFormat("DALI Group %d", group_id);

        JsonDocument doc;

        doc["name"] = readable_name;
        doc["unique_id"] = object_id;
        doc["schema"] = "json";
        doc["command_topic"] = utils::stringFormat("%s/light/group/%d/set", base_topic.c_str(), group_id);
        doc["state_topic"] = utils::stringFormat("%s/light/group/%d/state", base_topic.c_str(), group_id);
        doc["brightness"] = true;

        bool group_supports_tc = false;
        bool group_supports_rgb = false;

        {
            const auto devices = DaliDeviceController::Instance().getDevices();
            auto assignments = DaliGroupManagement::Instance().getAllAssignments();

            for (const auto& [long_addr, groups] : assignments) {
                if (groups.test(group_id)) {
                    if (devices.contains(long_addr)) {
                        if (auto* gear = std::get_if<ControlGear>(&devices.at(long_addr))) {
                            if (gear->color.has_value())
                            {
                                if (gear->color->supports_tc) group_supports_tc = true;
                                if (gear->color->supports_rgb) group_supports_rgb = true;
                            }
                        }
                    }
                }
            }
        }

        if (group_supports_tc || group_supports_rgb) {
            JsonArray color_modes = doc["supported_color_modes"].to<JsonArray>();
            if (group_supports_tc) {
                color_modes.add("color_temp");
                doc["min_mireds"] = 153;
                doc["max_mireds"] = 500;
            }
            if (group_supports_rgb) {
                color_modes.add("rgb");
            }
        }

        doc["availability_topic"] = availability_topic;
        doc["payload_available"] = CONFIG_DALI2MQTT_MQTT_PAYLOAD_ONLINE;
        doc["payload_not_available"] = CONFIG_DALI2MQTT_MQTT_PAYLOAD_OFFLINE;

        JsonObject device = doc["device"].to<JsonObject>();
        device["identifiers"] = bridge_public_name;
        device["name"] = bridge_public_name;
        device["model"] = "ESP32 DALI Bridge";
        device["manufacturer"] = "DALI-MQTT4ESP";
        device["sw_version"] = DALIMQTT_VERSION;

        std::string json_payload;
        serializeJson(doc, json_payload);
        mqtt.publish(discovery_topic, json_payload, 1, true);
    }

    void MQTTHomeAssistantDiscovery::publishSceneSelector() {
        const auto& mqtt = MQTTClient::Instance();
        const auto config = ConfigManager::Instance().getConfig();
        const std::string object_id = utils::stringFormat("dali_scenes_%s", config.client_id.c_str());
        const std::string discovery_topic = utils::stringFormat("homeassistant/select/%s/config", object_id.c_str());

        JsonDocument doc;

        doc["name"] = "DALI Scenes";
        doc["unique_id"] = object_id;
        doc["command_topic"] = utils::stringFormat("%s%s", base_topic.c_str(), CONFIG_DALI2MQTT_MQTT_SCENE_CMD_SUBTOPIC);

        JsonArray options = doc["options"].to<JsonArray>();
        for (int i = 0; i < 16; ++i) {
            options.add(utils::stringFormat("Scene %d", i));
        }

        doc["availability_topic"] = availability_topic;
        doc["payload_available"] = CONFIG_DALI2MQTT_MQTT_PAYLOAD_ONLINE;
        doc["payload_not_available"] = CONFIG_DALI2MQTT_MQTT_PAYLOAD_OFFLINE;

        JsonObject device = doc["device"].to<JsonObject>();
        device["identifiers"] = bridge_public_name;
        device["name"] = bridge_public_name;
        device["model"] = "ESP32 DALI Bridge";
        device["manufacturer"] = "DALI-MQTT4ESP";
        device["sw_version"] = DALIMQTT_VERSION;

        std::string json_payload;
        serializeJson(doc, json_payload);
        mqtt.publish(discovery_topic, json_payload, 1, true);
    }
} // daliMQTT