#include "HADiscovery.hxx"
#include "MQTTClient.hxx"
#include "ConfigManager.hxx"
#include "DaliDeviceController.hxx"
#include "utils/DaliLongAddrConversions.hxx"
#include "utils/StringUtils.hxx"

namespace daliMQTT
{
    MQTTHomeAssistantDiscovery::MQTTHomeAssistantDiscovery() {
        const auto config = ConfigManager::getInstance().getConfig();

        base_topic = config.mqtt_base_topic;
        availability_topic = utils::stringFormat("%s%s", base_topic.c_str(), CONFIG_DALI2MQTT_MQTT_AVAILABILITY_TOPIC);
        bridge_public_name = utils::stringFormat("DALI-MQTT Bridge (%s)", config.client_id.c_str());

        cJSON* names_root = cJSON_Parse(config.dali_device_identificators.c_str());
        if (cJSON_IsObject(names_root)) {
            const cJSON* current_name = nullptr;
            const auto addr_str = utils::longAddressToString(0);
            cJSON_ArrayForEach(current_name, names_root) {
                if (cJSON_IsString(current_name) && current_name->valuestring != nullptr) {
                    std::string key(current_name->string, strnlen(current_name->string, addr_str.size()));
                    device_identification[key] = current_name->valuestring;
                }
            }
        }
        cJSON_Delete(names_root);
    }

    void MQTTHomeAssistantDiscovery::publishAllDevices() {
        auto devices = DaliDeviceController::getInstance().getDevices();
        for (const auto& device : devices | std::views::values) {
            publishLight(device.long_address);
        }

        for (uint8_t i = 0; i < 16; ++i) {
            publishGroup(i);
        }

        publishSceneSelector();
    }

    void MQTTHomeAssistantDiscovery::publishLight(const DaliLongAddress_t long_addr) {
        const auto& mqtt = MQTTClient::getInstance();

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

        cJSON* root = cJSON_CreateObject();
        if (!root) return;

        DaliDevice dev_copy; // FIXME???
        {
            auto devices = DaliDeviceController::getInstance().getDevices();
            if (devices.contains(long_addr)) {
                dev_copy = devices.at(long_addr);
            }
        }

        cJSON_AddStringToObject(root, "name", readable_name.c_str());
        cJSON_AddStringToObject(root, "unique_id", object_id.c_str());
        cJSON_AddStringToObject(root, "schema", "json");
        cJSON_AddStringToObject(root, "command_topic", utils::stringFormat("%s/light/%s/set", base_topic.c_str(), addr_str.c_str()).c_str());
        cJSON_AddStringToObject(root, "state_topic", utils::stringFormat("%s/light/%s/state", base_topic.c_str(), addr_str.c_str()).c_str());
        cJSON_AddTrueToObject(root, "brightness");

        if (dev_copy.device_type.has_value() && dev_copy.device_type.value() == 8) {
            cJSON* color_modes = cJSON_CreateArray();
            cJSON_AddItemToArray(color_modes, cJSON_CreateString("color_temp"));
            cJSON_AddItemToArray(color_modes, cJSON_CreateString("rgb"));
            cJSON_AddItemToObject(root, "supported_color_modes", color_modes);
            cJSON_AddNumberToObject(root, "min_mireds", 153); // ~6500K
            cJSON_AddNumberToObject(root, "max_mireds", 500); // ~2000K
        }

        cJSON* av_list = cJSON_CreateArray();
        cJSON* av_bridge = cJSON_CreateObject();
        cJSON_AddStringToObject(av_bridge, "topic", availability_topic.c_str());
        cJSON_AddStringToObject(av_bridge, "payload_available", CONFIG_DALI2MQTT_MQTT_PAYLOAD_ONLINE);
        cJSON_AddStringToObject(av_bridge, "payload_not_available", CONFIG_DALI2MQTT_MQTT_PAYLOAD_OFFLINE);
        cJSON_AddItemToArray(av_list, av_bridge);

        cJSON* av_device = cJSON_CreateObject();
        cJSON_AddStringToObject(av_device, "topic", device_status_topic.c_str());
        cJSON_AddStringToObject(av_device, "payload_available", CONFIG_DALI2MQTT_MQTT_PAYLOAD_ONLINE);
        cJSON_AddStringToObject(av_device, "payload_not_available", CONFIG_DALI2MQTT_MQTT_PAYLOAD_OFFLINE);
        cJSON_AddItemToArray(av_list, av_device);

        cJSON_AddItemToObject(root, "availability", av_list);
        cJSON_AddStringToObject(root, "availability_mode", "all");

        cJSON* device = cJSON_CreateObject();
        if (device) {
            cJSON_AddStringToObject(device, "identifiers", bridge_public_name.c_str());
            cJSON_AddStringToObject(device, "name", bridge_public_name.c_str());
            cJSON_AddStringToObject(device, "model", "ESP32 DALI Bridge");
            cJSON_AddStringToObject(device, "manufacturer", "DIY");
            cJSON_AddItemToObject(root, "device", device);
        }

        if (char* json_payload = cJSON_PrintUnformatted(root)) {
            mqtt.publish(discovery_topic, json_payload, 1, true);
            free(json_payload);
        }

        cJSON_Delete(root);
    }

    void MQTTHomeAssistantDiscovery::publishGroup(uint8_t group_id) {
        const auto& mqtt = MQTTClient::getInstance();
        const auto config = ConfigManager::getInstance().getConfig();
        const std::string object_id = utils::stringFormat("dali_group_%s_%d", config.client_id.c_str(), group_id);
        const std::string discovery_topic = utils::stringFormat("homeassistant/light/%s/config", object_id.c_str());
        const std::string readable_name = utils::stringFormat("DALI Group %d", group_id);

        cJSON* root = cJSON_CreateObject();
        if (!root) return;

        cJSON_AddStringToObject(root, "name", readable_name.c_str());
        cJSON_AddStringToObject(root, "unique_id", object_id.c_str());
        cJSON_AddStringToObject(root, "schema", "json");
        cJSON_AddStringToObject(root, "command_topic",
                                utils::stringFormat("%s/light/group/%d/set", base_topic.c_str(), group_id).c_str());
        cJSON_AddStringToObject(root, "state_topic",
                                utils::stringFormat("%s/light/group/%d/state", base_topic.c_str(), group_id).c_str());

        cJSON_AddTrueToObject(root, "brightness");
        cJSON_AddStringToObject(root, "availability_topic", availability_topic.c_str());
        cJSON_AddStringToObject(root, "payload_available", CONFIG_DALI2MQTT_MQTT_PAYLOAD_ONLINE);
        cJSON_AddStringToObject(root, "payload_not_available", CONFIG_DALI2MQTT_MQTT_PAYLOAD_OFFLINE);

        cJSON* device = cJSON_CreateObject();
        if (device) {
            cJSON_AddStringToObject(device, "identifiers", bridge_public_name.c_str());
            cJSON_AddStringToObject(device, "name", bridge_public_name.c_str());
            cJSON_AddStringToObject(device, "model", "ESP32 DALI Bridge");
            cJSON_AddStringToObject(device, "manufacturer", "DIY");
            cJSON_AddItemToObject(root, "device", device);
        }

        if (char* json_payload = cJSON_PrintUnformatted(root)) {
            mqtt.publish(discovery_topic, json_payload, 1, true);
            free(json_payload);
        }

        cJSON_Delete(root);
    }

    void MQTTHomeAssistantDiscovery::publishSceneSelector() {
        const auto& mqtt = MQTTClient::getInstance();
        const auto config = ConfigManager::getInstance().getConfig();
        const std::string object_id = utils::stringFormat("dali_scenes_%s", config.client_id.c_str());
        const std::string discovery_topic = utils::stringFormat("homeassistant/select/%s/config", object_id.c_str());

        cJSON* root = cJSON_CreateObject();
        if (!root) return;

        cJSON_AddStringToObject(root, "name", "DALI Scenes");
        cJSON_AddStringToObject(root, "unique_id", object_id.c_str());
        cJSON_AddStringToObject(root, "command_topic",
                                utils::stringFormat("%s%s", base_topic.c_str(), CONFIG_DALI2MQTT_MQTT_SCENE_CMD_SUBTOPIC).c_str());

        cJSON* options = cJSON_CreateArray();
        for (int i = 0; i < 16; ++i) {
            cJSON_AddItemToArray(options, cJSON_CreateString(utils::stringFormat("Scene %d", i).c_str()));
        }
        cJSON_AddItemToObject(root, "options", options);

        cJSON_AddStringToObject(root, "availability_topic", availability_topic.c_str());
        cJSON_AddStringToObject(root, "payload_available", CONFIG_DALI2MQTT_MQTT_PAYLOAD_ONLINE);
        cJSON_AddStringToObject(root, "payload_not_available", CONFIG_DALI2MQTT_MQTT_PAYLOAD_OFFLINE);

        cJSON* device = cJSON_CreateObject();
        if (device) {
            cJSON_AddStringToObject(device, "identifiers", bridge_public_name.c_str());
            cJSON_AddStringToObject(device, "name", bridge_public_name.c_str());
            cJSON_AddItemToObject(root, "device", device);
        }

        if (char* json_payload = cJSON_PrintUnformatted(root)) {
            mqtt.publish(discovery_topic, json_payload, 1, true);
            free(json_payload);
        }
        cJSON_Delete(root);
    }
} // daliMQTT