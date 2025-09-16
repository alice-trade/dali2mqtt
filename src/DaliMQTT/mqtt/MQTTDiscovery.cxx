#include "sdkconfig.h"
#include "MQTTDiscovery.hxx"
#include "MQTTClient.hxx"
#include "ConfigManager.hxx"


namespace daliMQTT
{
      MQTTDiscovery::MQTTDiscovery() {
        const auto config = ConfigManager::getInstance().getConfig();
        base_topic = config.mqtt_base_topic;
        availability_topic = std::format("{}{}", base_topic, CONFIG_DALI2MQTT_MQTT_AVAILABILITY_TOPIC);
        bridge_public_name = "DALI-MQTT Bridge";

        cJSON* names_root = cJSON_Parse(config.dali_device_identificators.c_str());
        if (cJSON_IsObject(names_root)) {
            cJSON* current_name = nullptr;
            cJSON_ArrayForEach(current_name, names_root) {
                if (cJSON_IsString(current_name) && current_name->valuestring != nullptr) {
                    device_identification[current_name->string] = current_name->valuestring;
                }
            }
        }
        cJSON_Delete(names_root);
    }

    void MQTTDiscovery::publishAllDevices() {
          const auto config = ConfigManager::getInstance().getConfig();
          const auto device_mask = std::bitset<64>(config.dali_devices_mask);

          for (uint8_t i = 0; i < 64; ++i) {
              if (device_mask.test(i)) {
                  publishLight("short", i);
              }
          }
        for (uint8_t i = 0; i < 16; ++i) {
            publishLight("group", i);
        }

        publishSceneSelector();
    }

    void MQTTDiscovery::publishLight(const std::string& type, uint8_t id) {
        const auto& mqtt = MQTTClient::getInstance();

        const std::string object_id = std::format("dali_{}_{}", type, id);
        const std::string discovery_topic = std::format("homeassistant/light/{}/config", object_id);

        // Get readable name or generate default
        std::string readable_name;
        if (type == "short") {
            auto it = device_identification.find(std::to_string(id));
            if (it != device_identification.end() && !it->second.empty()) {
                readable_name = it->second;
            }
        }
        if (readable_name.empty()) {
            readable_name = std::format("DALI Device {} {}", type, id);
        }

        cJSON *root = cJSON_CreateObject();
        if (!root) return;

        cJSON_AddStringToObject(root, "name", readable_name.c_str());
        cJSON_AddStringToObject(root, "unique_id", object_id.c_str());
        cJSON_AddStringToObject(root, "schema", "json");
        cJSON_AddStringToObject(root, "command_topic", std::format("{}/light/{}/{}/set", base_topic, type, id).c_str());
        cJSON_AddStringToObject(root, "state_topic", std::format("{}/light/{}/{}/state", base_topic, type, id).c_str());
        cJSON_AddTrueToObject(root, "brightness");
        cJSON_AddStringToObject(root, "availability_topic", availability_topic.c_str());
        cJSON_AddStringToObject(root, "payload_available", CONFIG_DALI2MQTT_MQTT_PAYLOAD_ONLINE);
        cJSON_AddStringToObject(root, "payload_not_available", CONFIG_DALI2MQTT_MQTT_PAYLOAD_OFFLINE);

        if (cJSON *device = cJSON_CreateObject()) {
            cJSON_AddStringToObject(device, "identifiers", bridge_public_name.c_str());
            cJSON_AddStringToObject(device, "name", bridge_public_name.c_str());
            cJSON_AddStringToObject(device, "model", "ESP32 DALI Bridge");
            cJSON_AddStringToObject(device, "manufacturer", "DIY");
            cJSON_AddItemToObject(root, "device", device);
        }

        if (char *json_payload = cJSON_PrintUnformatted(root)) {
            mqtt.publish(discovery_topic, json_payload, 1, true);
            free(json_payload);
        }

        cJSON_Delete(root);
    }

    void MQTTDiscovery::publishSceneSelector() {
        const auto& mqtt = MQTTClient::getInstance();
        const std::string object_id = "dali_scenes";
        const std::string discovery_topic = std::format("homeassistant/select/{}/config", object_id);

        cJSON *root = cJSON_CreateObject();
        if (!root) return;

        cJSON_AddStringToObject(root, "name", "DALI Scenes");
        cJSON_AddStringToObject(root, "unique_id", object_id.c_str());
        cJSON_AddStringToObject(root, "command_topic", std::format("{}{}", base_topic, CONFIG_DALI2MQTT_MQTT_SCENE_CMD_SUBTOPIC).c_str());

        cJSON* options = cJSON_CreateArray();
        for (int i = 0; i < 16; ++i) {
            cJSON_AddItemToArray(options, cJSON_CreateString(std::format("Scene {}", i).c_str()));
        }
        cJSON_AddItemToObject(root, "options", options);

        cJSON_AddStringToObject(root, "availability_topic", availability_topic.c_str());
        cJSON_AddStringToObject(root, "payload_available", CONFIG_DALI2MQTT_MQTT_PAYLOAD_ONLINE);
        cJSON_AddStringToObject(root, "payload_not_available", CONFIG_DALI2MQTT_MQTT_PAYLOAD_OFFLINE);

        if (cJSON *device = cJSON_CreateObject()) {
            cJSON_AddStringToObject(device, "identifiers", bridge_public_name.c_str());
            cJSON_AddStringToObject(device, "name", bridge_public_name.c_str());
            cJSON_AddItemToObject(root, "device", device);
        }

        if ( char *json_payload = cJSON_PrintUnformatted(root)) {
            mqtt.publish(discovery_topic, json_payload, 1, true);
            free( json_payload);
        }
        cJSON_Delete(root);
    }
} // daliMQTT