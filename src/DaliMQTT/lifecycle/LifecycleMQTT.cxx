#include <cJSON.h>
#include <string_view>
#include <vector>
#include <format>
#include <charconv>
#include <algorithm>
#include <esp_log.h>
#include <ranges>

#include "sdkconfig.h"
#include "ConfigManager.hxx"
#include "MQTTClient.hxx"
#include "DaliAPI.hxx"
#include "DaliDeviceController.hxx"
#include "WebUI.hxx"
#include "MQTTDiscovery.hxx"
#include "Lifecycle.hxx"

namespace daliMQTT {

    static constexpr char  TAG[] = "LifecycleMQTT";

    void Lifecycle::setupAndRunMqtt() {
        const auto config = ConfigManager::getInstance().getConfig();
        auto& mqtt = MQTTClient::getInstance();

        const std::string availability_topic = std::format("{}{}", config.mqtt_base_topic, CONFIG_DALI2MQTT_MQTT_AVAILABILITY_TOPIC);
        mqtt.init(config.mqtt_uri, config.mqtt_client_id, availability_topic);
        mqtt.onConnected = [this]() { this->onMqttConnected(); };
        mqtt.onData = [this](const std::string& t, const std::string& d) { this->onMqttData(t, d); };

        mqtt.connect();
    }

    void Lifecycle::onMqttConnected() {
        ESP_LOGI(TAG, "MQTT connected successfully.");
        auto config = ConfigManager::getInstance().getConfig();
        auto const& mqtt = MQTTClient::getInstance();

        std::string availability_topic = std::format("{}{}", config.mqtt_base_topic, CONFIG_DALI2MQTT_MQTT_AVAILABILITY_TOPIC);
        mqtt.publish(availability_topic, CONFIG_DALI2MQTT_MQTT_PAYLOAD_ONLINE, 1, true);

        std::string cmd_topic = std::format("{}/light/+/+/set", config.mqtt_base_topic);
        mqtt.subscribe(cmd_topic);
        ESP_LOGI(TAG, "Subscribed to: %s", cmd_topic.c_str());

        MQTTDiscovery mqtt_discovery;
        mqtt_discovery.publishAllDevices();
        ESP_LOGI(TAG, "MQTT discovery messages published.");

        DaliDeviceController::getInstance().startPolling();
    }

    void Lifecycle::onMqttData(const std::string& topic, const std::string& data) {
        ESP_LOGD(TAG, "MQTT Rx: %s -> %s", topic.c_str(), data.c_str());

        std::string_view topic_sv(topic);
        auto config = ConfigManager::getInstance().getConfig();

        if (!topic_sv.starts_with(config.mqtt_base_topic)) return;
        topic_sv.remove_prefix(config.mqtt_base_topic.length());

        if (topic_sv.starts_with('/')) {
            topic_sv.remove_prefix(1);
        }

        std::vector<std::string_view> parts;
        for (const auto part : std::views::split(topic_sv, '/')) {
            parts.emplace_back(part.begin(), part.end());
        }

        if (parts.size() != 4 || parts[0] != "light" || parts[3] != "set") return;

        dali_addressType_t addr_type;
        if (parts[1] == "short") addr_type = DALI_ADDRESS_TYPE_SHORT;
        else if (parts[1] == "group") addr_type = DALI_ADDRESS_TYPE_GROUP;
        else return;

        uint8_t id;
        auto [ptr, ec] = std::from_chars(parts[2].data(), parts[2].data() + parts[2].size(), id);
        if (ec != std::errc()) return;

        cJSON* root = cJSON_Parse(data.c_str());
        if (!root) return;

        auto& dali = DaliAPI::getInstance();

        if (cJSON const* state = cJSON_GetObjectItem(root, "state"); state && cJSON_IsString(state)) {
            if (strcmp(state->valuestring, "OFF") == 0) {
                dali.sendCommand(addr_type, id, DALI_COMMAND_OFF);
            }
        }

        if (cJSON* brightness = cJSON_GetObjectItem(root, "brightness"); brightness && cJSON_IsNumber(brightness)) {
             uint8_t level = static_cast<uint8_t>(std::clamp(brightness->valueint, 0, 254));
             dali.sendCommand(addr_type, id, level, false); // DACP command
        }

        cJSON_Delete(root);
    }
}