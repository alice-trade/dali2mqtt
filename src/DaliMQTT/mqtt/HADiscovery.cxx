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

        char av_topic[128];
        snprintf(av_topic, sizeof(av_topic), "%s%s", base_topic.c_str(), CONFIG_DALI2MQTT_MQTT_AVAILABILITY_TOPIC);
        availability_topic = av_topic;

        char bridge_name[128];
        snprintf(bridge_name, sizeof(bridge_name), "DALI-MQTT Bridge (%s)", config.client_id.c_str());
        bridge_public_name = bridge_name;

        JsonDocument names_root;
        if (!deserializeJson(names_root, config.dali_device_identificators)) {
            if (names_root.is<JsonObject>()) {
                auto obj = names_root.as<JsonObject>();
                device_names.reserve(obj.size());

                for (JsonPair kv : obj) {
                    if (kv.value().is<const char*>()) {
                        auto addr_opt = utils::stringToLongAddress(kv.key().c_str());
                        if (addr_opt) {
                            device_names.push_back({
                                .addr = *addr_opt,
                                .name = kv.value().as<std::string>()
                            });
                        }
                    }
                }
            }
        }
    }

    void MQTTHomeAssistantDiscovery::publishAllDevices() {
        auto devices = DaliDeviceController::Instance().getDevices();
        for (const auto& dev : devices) {
            if (std::holds_alternative<InputDevice>(dev)) continue;
            publishLight(getIdentity(dev).long_address);
        }

        for (uint8_t b = 0; b < Constants::MaxBuses; ++b) {
            auto* adapter = DaliDeviceController::Instance().getAdapter(b);
            if (adapter && adapter->isInitialized()) {
                for (uint8_t i = 0; i < 16; ++i) {
                    publishGroup(b, i);
                }
                publishSceneSelector(b);
            }
        }
    }

    void MQTTHomeAssistantDiscovery::publishLight(const DaliLongAddress_t long_addr) {
        const auto& mqtt = MQTTClient::Instance();
        const auto addr_str_arr = utils::longAddressToString(long_addr);
        const char* addr_str = addr_str_arr.data();
        const char* readable_name_ptr = nullptr;

        auto it = std::ranges::find_if(device_names, [long_addr](const DeviceNameEntry& entry) {
            return entry.addr == long_addr;
        });

        char name_buffer[64];
        if (it != device_names.end() && !it->name.empty()) {
            readable_name_ptr = it->name.c_str();
        } else {
            snprintf(name_buffer, sizeof(name_buffer), "DALI Device %s", addr_str);
            readable_name_ptr = name_buffer;
        }

        char object_id[64];
        snprintf(object_id, sizeof(object_id), "dali_light_%s", addr_str);

        char discovery_topic[128];
        snprintf(discovery_topic, sizeof(discovery_topic), "homeassistant/light/%s/config", object_id);

        char device_status_topic[128];
        snprintf(device_status_topic, sizeof(device_status_topic), "%s/light/%s/status", base_topic.c_str(), addr_str);
        JsonDocument doc;

        ControlGear dev_copy;
        bool found = false;
        {
            const auto devices = DaliDeviceController::Instance().getDevices();
            auto dev_it = std::find_if(devices.begin(), devices.end(), [&](const DaliDevice& d) {
                return getIdentity(d).long_address == long_addr;
            });

            if (dev_it != devices.end()) {
                if (const auto* gear = std::get_if<ControlGear>(&(*dev_it))) {
                    dev_copy = *gear;
                    found = true;
                }
            }
        }
        if (!found) return;

        doc["name"] = readable_name_ptr;
        doc["unique_id"] = object_id;
        doc["schema"] = "json";

        char cmd_topic[128], state_topic[128];
        snprintf(cmd_topic, sizeof(cmd_topic), "%s/light/%s/set", base_topic.c_str(), addr_str);
        snprintf(state_topic, sizeof(state_topic), "%s/light/%s/state", base_topic.c_str(), addr_str);

        doc["command_topic"] = cmd_topic;
        doc["state_topic"] = state_topic;
        doc["brightness"] = true;

        if (dev_copy.device_type.has_value() && dev_copy.device_type.value() == 8 && dev_copy.color.has_value()) {
            const auto& c = dev_copy.color.value();
            auto color_modes = doc["supported_color_modes"].to<JsonArray>();
            if (c.supports_tc) {
                color_modes.add("color_temp");
                doc["min_mireds"] = c.min_mireds.value_or(153);
                doc["max_mireds"] = c.max_mireds.value_or(500);
            }
            if (c.supports_rgb) {
                color_modes.add("rgb");
            }
        }

        auto av_list = doc["availability"].to<JsonArray>();

        auto av_bridge = av_list.add<JsonObject>();
        av_bridge["topic"] = availability_topic;
        av_bridge["payload_available"] = CONFIG_DALI2MQTT_MQTT_PAYLOAD_ONLINE;
        av_bridge["payload_not_available"] = CONFIG_DALI2MQTT_MQTT_PAYLOAD_OFFLINE;

        auto av_device = av_list.add<JsonObject>();
        av_device["topic"] = device_status_topic;
        av_device["payload_available"] = CONFIG_DALI2MQTT_MQTT_PAYLOAD_ONLINE;
        av_device["payload_not_available"] = CONFIG_DALI2MQTT_MQTT_PAYLOAD_OFFLINE;

        doc["availability_mode"] = "all";

        auto device = doc["device"].to<JsonObject>();
        device["identifiers"] = bridge_public_name;
        device["name"] = bridge_public_name;
        device["model"] = "ESP32 DALI Bridge";
        device["manufacturer"] = "DALI-MQTT4ESP";
        device["sw_version"] = DALIMQTT_VERSION;

        std::string json_payload;
        serializeJson(doc, json_payload);
        mqtt.publish(discovery_topic, json_payload.c_str(), 1, true);
    }

    void MQTTHomeAssistantDiscovery::publishGroup(uint8_t bus_id, uint8_t group_id) const {
        const auto& mqtt = MQTTClient::Instance();
        const auto config = ConfigManager::Instance().getConfig();

        char object_id[64];
        snprintf(object_id, sizeof(object_id), "dali_b%d_group_%s_%d", bus_id, config.client_id.c_str(), group_id);

        char discovery_topic[128];
        snprintf(discovery_topic, sizeof(discovery_topic), "homeassistant/light/%s/config", object_id);

        JsonDocument doc;
        char readable_name[32];
        snprintf(readable_name, sizeof(readable_name), "DALI Bus %d Group %d", bus_id, group_id);
        doc["name"] = readable_name;
        doc["unique_id"] = object_id;
        doc["schema"] = "json";

        char cmd_topic[128], state_topic[128];
        snprintf(cmd_topic, sizeof(cmd_topic), "%s/light/bus/%d/group/%d/set", base_topic.c_str(), bus_id, group_id);
        snprintf(state_topic, sizeof(state_topic), "%s/light/bus/%d/group/%d/state", base_topic.c_str(), bus_id, group_id);

        doc["command_topic"] = cmd_topic;
        doc["state_topic"] = state_topic;
        doc["brightness"] = true;

        bool group_supports_tc = false;
        bool group_supports_rgb = false;

        {
            const auto devices = DaliDeviceController::Instance().getDevices();
            auto assignments = DaliGroupManagement::Instance().getAllAssignments();
            for (const auto& pair : assignments) {
                if (pair.second.test(group_id)) {
                    auto dev_it = std::find_if(devices.begin(), devices.end(), [&](const DaliDevice& d) {
                        return getIdentity(d).long_address == pair.first;
                    });
                    if (dev_it != devices.end()) {
                        if (auto* gear = std::get_if<ControlGear>(&(*dev_it))) {
                            if (gear->internal_address.bus() == bus_id) {
                                if (gear->color.has_value()) {
                                    if (gear->color->supports_tc) group_supports_tc = true;
                                    if (gear->color->supports_rgb) group_supports_rgb = true;
                                }
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
        mqtt.publish(discovery_topic, json_payload.c_str(), 1, true);
    }

    void MQTTHomeAssistantDiscovery::publishSceneSelector(uint8_t bus_id) const {
        const auto& mqtt = MQTTClient::Instance();
        const auto config = ConfigManager::Instance().getConfig();

        char object_id[64];
        snprintf(object_id, sizeof(object_id), "dali_b%d_scenes_%s", bus_id, config.client_id.c_str());

        char discovery_topic[128];
        snprintf(discovery_topic, sizeof(discovery_topic), "homeassistant/select/%s/config", object_id);

        JsonDocument doc;

        char readable_name[32];
        snprintf(readable_name, sizeof(readable_name), "DALI Bus %d Scenes", bus_id);
        doc["name"] = readable_name;
        doc["unique_id"] = object_id;

        char cmd_topic[128];
        snprintf(cmd_topic, sizeof(cmd_topic), "%s/scene/bus/%d/set", base_topic.c_str(), bus_id);
        doc["command_topic"] = cmd_topic;

        JsonArray options = doc["options"].to<JsonArray>();
        for (int i = 0; i < 16; ++i) {
            char scene_name[16];
            snprintf(scene_name, sizeof(scene_name), "Scene %d", i);
            options.add(scene_name);
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
        mqtt.publish(discovery_topic, json_payload.c_str(), 1, true);
    }
} // daliMQTT
