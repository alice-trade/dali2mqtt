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
        // SSID и пароль для точки доступа можно также вынести в sdkconfig
        wifi.startAP("DALI-Bridge-Setup", "password");

        auto& web = WebUI::getInstance();
        web.start();
        ESP_LOGI(TAG, "Web server for provisioning is running.");
    }

    void LifecycleBase::startNormalMode() {
        ESP_LOGI(TAG, "Starting in normal mode...");

        // Инициализация DALI
        auto& dali = DaliAPI::getInstance();
        dali.init(static_cast<gpio_num_t>(CONFIG_DALI2MQTT_DALI_RX_PIN), static_cast<gpio_num_t>(CONFIG_DALI2MQTT_DALI_TX_PIN));

        // Настройка WiFi
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

        // Публикуем статус online
        std::string availability_topic = config.mqtt_base_topic + CONFIG_DALI2MQTT_MQTT_AVAILABILITY_TOPIC;
        mqtt.publish(availability_topic, CONFIG_DALI2MQTT_MQTT_PAYLOAD_ONLINE, 1, true);

        // Подписываемся на топики команд
        std::string cmd_topic = config.mqtt_base_topic + "/light/+/+/set";
        mqtt.subscribe(cmd_topic);
        ESP_LOGI(TAG, "Subscribed to: %s", cmd_topic.c_str());

        // Здесь можно запустить HASS Discovery
        // ...

        // Запускаем задачу опроса DALI
        xTaskCreate(daliPollTask, "dali_poll", CONFIG_DALI2MQTT_DALI_POLL_TASK_STACK_SIZE, this, CONFIG_DALI2MQTT_DALI_POLL_TASK_PRIORITY, nullptr);
    }

    void LifecycleBase::onMqttData(const std::string& topic, const std::string& data) {
        ESP_LOGI(TAG, "Received MQTT message. Topic: %s, Data: %s", topic.c_str(), data.c_str());
        // Здесь должна быть логика парсинга топика и данных для отправки команд в DALI
        // Пример: dali_bridge/light/short/5/set
        //           [0]     [1]   [2]  [3]  [4]
    }

    void LifecycleBase::daliPollTask(void* pvParameters) {
        auto* logic = static_cast<LifecycleBase*>(pvParameters);
        auto config = ConfigManager::getInstance().getConfig();
        auto& dali = DaliAPI::getInstance();
        auto& mqtt = MQTTClient::getInstance();

        ESP_LOGI(TAG, "DALI polling task started.");

        while (true) {
            // Опрашиваем устройства (здесь пример для одного устройства)
            for (uint8_t i = 0; i < 64; ++i) {
                if (auto level = dali.sendQuery(DALI_ADDRESS_TYPE_SHORT, i, DALI_COMMAND_QUERY_ACTUAL_LEVEL); level.has_value()) {
                     ESP_LOGI(TAG, "DALI device %d level: %d", i, level.value());
                     // Публикуем состояние в MQTT
                     std::string state_topic = config.mqtt_base_topic + "/light/short/" + std::to_string(i) + "/state";
                    std::string payload = std::string(R"({"state":")") + (level.value() > 0 ? "ON" : "OFF") +
                                          R"(", "brightness":)" + std::to_string(level.value()) + "}";                     mqtt.publish(state_topic, payload, 0, false);
                 }

            }
            vTaskDelay(pdMS_TO_TICKS(config.dali_poll_interval_ms));
        }
    }
} // daliMQTT