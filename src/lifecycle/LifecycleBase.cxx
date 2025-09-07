#include "LifecycleBase.hxx"
#include "ConfigManager.hxx"
#include "Wifi.hxx"
#include "MQTTClient.hxx"
#include "DaliAPI.hxx"
#include "WebUI.hxx"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"
#include <string_view>
#include <vector>
#include <format>
#include <charconv>
#include <MQTTDiscovery.hxx>

#include "cJSON.h"

namespace daliMQTT
{
    static const char* TAG = "BridgeLogic";

    LifecycleBase& LifecycleBase::getInstance() {
        static LifecycleBase instance;
        return instance;
    }

    void LifecycleBase::startProvisioningMode() {
        ESP_LOGI(TAG, "Starting in provisioning mode...");
        auto& wifi = Wifi::getInstance();
        wifi.init();

        const std::string ap_ssid = "DALI-Bridge-Setup";
        const std::string ap_pass = "password123";
        wifi.startAP(ap_ssid, ap_pass);

        auto& web = WebUI::getInstance();
        web.start();
        ESP_LOGI(TAG, "Web server for provisioning is running.");
    }

    void LifecycleBase::startNormalMode() {
        ESP_LOGI(TAG, "Starting in normal mode...");
        auto& dali = DaliAPI::getInstance();
        dali.init(static_cast<gpio_num_t>(CONFIG_DALI2MQTT_DALI_RX_PIN), static_cast<gpio_num_t>(CONFIG_DALI2MQTT_DALI_TX_PIN));

        auto& web = WebUI::getInstance();
        web.start();

        auto& wifi = Wifi::getInstance();
        wifi.init();
        wifi.onConnected = [this]() {
            ESP_LOGI(TAG, "WiFi connected, starting MQTT...");
            this->setupAndRunMqtt();
        };
        wifi.onDisconnected = []() {
            ESP_LOGW(TAG, "WiFi disconnected, stopping MQTT.");
            MQTTClient::getInstance().disconnect();
        };

        auto config = ConfigManager::getInstance().getConfig();
        wifi.connectToAP(config.wifi_ssid, config.wifi_password);
    }

    void LifecycleBase::setupAndRunMqtt() {
        const auto config = ConfigManager::getInstance().getConfig();
        auto& mqtt = MQTTClient::getInstance();

        const std::string availability_topic = config.mqtt_base_topic + CONFIG_DALI2MQTT_MQTT_AVAILABILITY_TOPIC;
        mqtt.init(config.mqtt_uri, config.mqtt_client_id, availability_topic);
        mqtt.onConnected = [this]() { this->onMqttConnected(); };
        mqtt.onData = [this](const std::string& t, const std::string& d) { this->onMqttData(t, d); };

        mqtt.connect();
    }

    void LifecycleBase::onMqttConnected() {
        ESP_LOGI(TAG, "MQTT connected successfully.");
        auto config = ConfigManager::getInstance().getConfig();
        auto& mqtt = MQTTClient::getInstance();

        std::string availability_topic = config.mqtt_base_topic + CONFIG_DALI2MQTT_MQTT_AVAILABILITY_TOPIC;
        mqtt.publish(availability_topic, CONFIG_DALI2MQTT_MQTT_PAYLOAD_ONLINE, 1, true);

        std::string cmd_topic = config.mqtt_base_topic + "/light/+/+/set";
        mqtt.subscribe(cmd_topic);
        ESP_LOGI(TAG, "Subscribed to: %s", cmd_topic.c_str());

        MQTTDiscovery MQTTDiscovery;
        MQTTDiscovery.publishAllDevices();
        ESP_LOGI(TAG, "MQTT discovery messages published.");

        if(!dali_poll_task_handle) {
            xTaskCreate(daliPollTask, "dali_poll", CONFIG_DALI2MQTT_DALI_POLL_TASK_STACK_SIZE, this, CONFIG_DALI2MQTT_DALI_POLL_TASK_PRIORITY, &dali_poll_task_handle);
        }
    }

    void LifecycleBase::onMqttData(const std::string& topic, const std::string& data) {
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
        if (cJSON_HasObjectItem(root, "state")) {
            cJSON const* state = cJSON_GetObjectItem(root, "state");
            if (cJSON_IsString(state) && strcmp(state->valuestring, "OFF") == 0) {
                dali.sendCommand(addr_type, id, DALI_COMMAND_OFF);
            }
        }

        if (cJSON_HasObjectItem(root, "brightness")) {
             cJSON const* brightness = cJSON_GetObjectItem(root, "brightness");
             if (cJSON_IsNumber(brightness)) {
                uint8_t level = std::min(254, brightness->valueint);
                dali.sendCommand(addr_type, id, level, false); // DACP command
             }
        }

        cJSON_Delete(root);
    }

     void LifecycleBase::daliPollTask(void* pvParameters) {
        auto config = ConfigManager::getInstance().getConfig();
        auto& dali = DaliAPI::getInstance();
        auto& mqtt = MQTTClient::getInstance();

        ESP_LOGI(TAG, "DALI polling task started.");

        while (true) {
            for (uint8_t i = 0; i < 64; ++i) {
                auto level = dali.sendQuery(DALI_ADDRESS_TYPE_SHORT, i, DALI_COMMAND_QUERY_ACTUAL_LEVEL);
                if (level.has_value() && level.value() != 255) {
                     std::string state_topic = config.mqtt_base_topic + "/light/short/" + std::to_string(i) + "/state";
                    std::string payload = R"({"state":")" +
                                                              std::string(level.value() > 0 ? "ON" : "OFF") +
                                                              R"(","brightness":)" +
                                                              std::to_string(level.value()) +
                                                              "}";
                    mqtt.publish(state_topic, payload);
                 } else if (!level.has_value()) {
                     ESP_LOGD(TAG, "No reply from DALI device %d", i);
                 }
                 vTaskDelay(pdMS_TO_TICKS(50));
            }
            vTaskDelay(pdMS_TO_TICKS(config.dali_poll_interval_ms));
        }
    }
} // daliMQTT