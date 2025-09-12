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
        availability_topic = std::format("{}{}", base_topic, CONFIG_DALI2MQTT_MQTT_AVAILABILITY_TOPIC);
        device_name = CONFIG_MDNS_INSTANCE;
    }

    void MQTTDiscovery::publishAllDevices() {
        for (uint8_t i = 0; i < 64; ++i) {
            publishLight("short", i);
        }
        for (uint8_t i = 0; i < 16; ++i) {
            publishLight("group", i);
        }
    }

    void MQTTDiscovery::publishLight(const std::string& type, uint8_t id) {
        const auto& mqtt = MQTTClient::getInstance();

        const std::string object_id = std::format("dali_{}_{}", type, id);
        const std::string discovery_topic = std::format("homeassistant/light/{}/config", object_id);

        cJSON *root = cJSON_CreateObject();
        if (!root) return;

        cJSON_AddStringToObject(root, "name", std::format("DALI {} {}", type, id).c_str());
        cJSON_AddStringToObject(root, "unique_id", object_id.c_str());
        cJSON_AddStringToObject(root, "schema", "json");
        cJSON_AddStringToObject(root, "command_topic", std::format("{}/light/{}/{}/set", base_topic, type, id).c_str());
        cJSON_AddStringToObject(root, "state_topic", std::format("{}/light/{}/{}/state", base_topic, type, id).c_str());
        cJSON_AddTrueToObject(root, "brightness");
        cJSON_AddStringToObject(root, "availability_topic", availability_topic.c_str());
        cJSON_AddStringToObject(root, "payload_available", CONFIG_DALI2MQTT_MQTT_PAYLOAD_ONLINE);
        cJSON_AddStringToObject(root, "payload_not_available", CONFIG_DALI2MQTT_MQTT_PAYLOAD_OFFLINE);

        if (cJSON *device = cJSON_CreateObject()) {
            cJSON_AddStringToObject(device, "identifiers", device_name.c_str());
            cJSON_AddStringToObject(device, "name", device_name.c_str());
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
} // daliMQTT