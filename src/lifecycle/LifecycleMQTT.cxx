#include "ConfigManager.hxx"
#include "MQTTClient.hxx"
#include "DaliAPI.hxx"
#include "WebUI.hxx"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"
#include <string_view>
#include <vector>
#include <format>
#include <charconv>
#include <algorithm>
#include <esp_log.h>
#include <MQTTDiscovery.hxx>
#include "cJSON.h"
#include "Lifecycle.hxx"

namespace daliMQTT {

    static inline auto TAG = "LifecycleMQTT";

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

        if(!dali_poll_task_handle) {
            xTaskCreate(daliPollTask, "dali_poll", CONFIG_DALI2MQTT_DALI_POLL_TASK_STACK_SIZE, this, CONFIG_DALI2MQTT_DALI_POLL_TASK_PRIORITY, &dali_poll_task_handle);
        }
    }

    void Lifecycle::onMqttData(const std::string& topic, const std::string& data) {
        ESP_LOGD(TAG, "MQTT Rx: %s -> %s", topic.c_str(), data.c_str());

        std::string_view topic_sv(topic);
        auto config = ConfigManager::getInstance().getConfig();

        if (!topic_sv.starts_with(config.mqtt_base_topic)) return;
        topic_sv.remove_prefix(config.mqtt_base_topic.length());

        std::vector<std::string_view> parts;
        size_t start = 0;
        while(start < topic_sv.length()) {
            size_t end = topic_sv.find('/', start + 1);
            if (end == std::string_view::npos) {
                parts.push_back(topic_sv.substr(start + 1));
                break;
            }
            parts.push_back(topic_sv.substr(start + 1, end - start - 1));
            start = end;
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

        if (cJSON* state = cJSON_GetObjectItem(root, "state"); state && cJSON_IsString(state)) {
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

     void Lifecycle::daliPollTask([[maybe_unused]] void* pvParameters) {
        auto config = ConfigManager::getInstance().getConfig();
        auto& dali = DaliAPI::getInstance();
        auto const& mqtt = MQTTClient::getInstance();

        ESP_LOGI(TAG, "DALI polling task started.");

        while (true) {
            for (uint8_t i = 0; i < 64; ++i) {
                if (auto level_opt = dali.sendQuery(DALI_ADDRESS_TYPE_SHORT, i, DALI_COMMAND_QUERY_ACTUAL_LEVEL); level_opt && level_opt.value() != 255) {
                    uint8_t level = level_opt.value();
                    std::string state_topic = std::format("{}/light/short/{}/state", config.mqtt_base_topic, i);
                    std::string payload = std::format(R"({{"state":"{}","brightness":{}}})", (level > 0 ? "ON" : "OFF"), level);
                    mqtt.publish(state_topic, payload);
                 } else if (!level_opt) {
                     ESP_LOGD(TAG, "No reply from DALI device %d", i);
                 }
                 vTaskDelay(pdMS_TO_TICKS(CONFIG_DALI2MQTT_DALI_POLL_DELAY_MS));
            }
            vTaskDelay(pdMS_TO_TICKS(config.dali_poll_interval_ms));
        }
    }
}