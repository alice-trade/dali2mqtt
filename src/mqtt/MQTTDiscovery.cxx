#include "MQTTDiscovery.hxx"
#include "MQTTClient.hxx"
#include "ConfigManager.hxx"
#include "cJSON.h"
#include <format>
#include "sdkconfig.h"

namespace daliMQTT
{
      MQTTDiscovery::MQTTDiscovery() {
        const auto config = ConfigManager::getInstance().getConfig();
        base_topic = config.mqtt_base_topic;
        availability_topic = base_topic + CONFIG_DALI2MQTT_MQTT_AVAILABILITY_TOPIC;
        device_name = "DALI Bridge";
    }

    void MQTTDiscovery::publishAllDevices() {
        for (uint8_t i = 0; i < 64; ++i) {
            publishLight(R"(short)", i);
        }
        for (uint8_t i = 0; i < 16; ++i) {
            publishLight("group", i);
        }
    }

    void MQTTDiscovery::publishLight(const std::string& type, uint8_t id) {
        const auto& mqtt = MQTTClient::getInstance();

          const std::string id_str = std::to_string(id);
          const std::string object_id = "dali_" + type + "_" + id_str;
          const std::string discovery_topic = "homeassistant/light/" + object_id + "/config";
          std::string name = "DALI " + type + " " + id_str;
          std::string cmd_topic = base_topic + "/light/" + type + "/" + id_str + "/set";
          std::string state_topic = base_topic + "/light/" + type + "/" + id_str + "/state";

        cJSON *root = cJSON_CreateObject();
        cJSON_AddStringToObject(root, "name", std::format("DALI {} {}", type, id).c_str());
        cJSON_AddStringToObject(root, "unique_id", object_id.c_str());
        cJSON_AddStringToObject(root, "schema", "json");
        cJSON_AddStringToObject(root, "command_topic", std::format("{}/light/{}/{}/set", base_topic, type, id).c_str());
        cJSON_AddStringToObject(root, "state_topic", std::format("{}/light/{}/{}/state", base_topic, type, id).c_str());
        cJSON_AddTrueToObject(root, "brightness");
        cJSON_AddStringToObject(root, "availability_topic", availability_topic.c_str());
        cJSON_AddStringToObject(root, "payload_available", CONFIG_DALI2MQTT_MQTT_PAYLOAD_ONLINE);
        cJSON_AddStringToObject(root, "payload_not_available", CONFIG_DALI2MQTT_MQTT_PAYLOAD_OFFLINE);

        cJSON *device = cJSON_CreateObject();
        cJSON_AddStringToObject(device, "identifiers", device_name.c_str());
        cJSON_AddStringToObject(device, "name", device_name.c_str());
        cJSON_AddStringToObject(device, "model", "ESP32 DALI Bridge");
        cJSON_AddStringToObject(device, "manufacturer", "DIY");
        cJSON_AddItemToObject(root, "device", device);

        char *json_payload = cJSON_PrintUnformatted(root);
        mqtt.publish(discovery_topic, json_payload, 1, true);

        cJSON_Delete(root);
        free(json_payload);
    }
} // daliMQTT