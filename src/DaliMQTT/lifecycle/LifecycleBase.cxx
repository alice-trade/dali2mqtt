#include "ConfigManager.hxx"
#include "DaliAPI.hxx"
#include "MQTTCommandProcess.hxx"
#include "DaliGroupManagement.hxx"
#include "DaliSceneManagement.hxx"
#include "Lifecycle.hxx"
#include "MQTTClient.hxx"
#include "DaliDeviceController.hxx"
#include "HADiscovery.hxx"
#include "WebUI.hxx"
#include "Wifi.hxx"
#include "SyslogConfig.hxx"
#include <esp_ota_ops.h>

namespace daliMQTT
{
    static constexpr  char TAG[] = "LifecycleBase";

    void Lifecycle::startProvisioningMode() {
        ESP_LOGI(TAG, "Starting in provisioning mode...");
        auto& wifi = Wifi::getInstance();
        wifi.init();

        wifi.startAP(CONFIG_DALI2MQTT_WIFI_AP_SSID, CONFIG_DALI2MQTT_WIFI_AP_PASS);

        auto& web = WebUI::getInstance();
        web.start();
        const esp_partition_t *running = esp_ota_get_running_partition();
        esp_ota_img_states_t ota_state;
        if (esp_ota_get_state_partition(running, &ota_state) == ESP_OK) {
            if (ota_state == ESP_OTA_IMG_PENDING_VERIFY) {
                ESP_LOGI(TAG, "OTA Update detected. Marking firmware as valid.");
                esp_ota_mark_app_valid_cancel_rollback();
            }
        }
        ESP_LOGI(TAG, "Web server for provisioning is running.");
    }

    void Lifecycle::startNormalMode() {
        ESP_LOGI(TAG, "Starting in normal mode...");
        auto config = ConfigManager::getInstance().getConfig();

        auto& wifi = Wifi::getInstance();
        wifi.init();
        wifi.onConnected = [config]() {
            ESP_LOGI(TAG, "WiFi connected, starting MQTT...");
            if (config.syslog_enabled && !config.syslog_server.empty()) {
                SyslogConfig::getInstance().init(config.syslog_server);
            }
            auto& mqtt = MQTTClient::getInstance();
            MQTTCommandProcess::getInstance().init();

            const std::string availability_topic = std::format("{}{}", config.mqtt_base_topic, CONFIG_DALI2MQTT_MQTT_AVAILABILITY_TOPIC);
            mqtt.init(config.mqtt_uri, config.client_id, availability_topic, config.mqtt_user, config.mqtt_pass);
            mqtt.onConnected = []() { onMqttConnected(); };

            mqtt.connect();
        };
        wifi.onDisconnected = []() {
            ESP_LOGW(TAG, "WiFi disconnected, stopping MQTT.");
            MQTTClient::getInstance().disconnect();
        };

        auto& dali_api = DaliAPI::getInstance();
        dali_api.init(static_cast<gpio_num_t>(CONFIG_DALI2MQTT_DALI_RX_PIN), static_cast<gpio_num_t>(CONFIG_DALI2MQTT_DALI_TX_PIN));

        auto& dali_manager = DaliDeviceController::getInstance();
        dali_manager.init();

        auto& group_manager = DaliGroupManagement::getInstance();
        group_manager.init();

        auto& scene_manager = DaliSceneManagement::getInstance();
        scene_manager.init();

        auto& web = WebUI::getInstance();
        web.start();
        const esp_partition_t *running = esp_ota_get_running_partition();
        esp_ota_img_states_t ota_state;
        if (esp_ota_get_state_partition(running, &ota_state) == ESP_OK) {
            if (ota_state == ESP_OTA_IMG_PENDING_VERIFY) {
                ESP_LOGI(TAG, "OTA Update detected. Marking firmware as valid.");
                esp_ota_mark_app_valid_cancel_rollback();
            }
        }
        wifi.connectToAP(config.wifi_ssid, config.wifi_password);
    }

    void Lifecycle::onMqttConnected() {
        ESP_LOGI(TAG, "MQTT connected successfully.");
        auto config = ConfigManager::getInstance().getConfig();
        auto const& mqtt = MQTTClient::getInstance();

        std::string availability_topic = std::format("{}{}", config.mqtt_base_topic, CONFIG_DALI2MQTT_MQTT_AVAILABILITY_TOPIC);
        mqtt.publish(availability_topic, CONFIG_DALI2MQTT_MQTT_PAYLOAD_ONLINE, 1, true);

        // base/light/LONG_ADDR/set
        std::string light_single_topic = std::format("{}{}", config.mqtt_base_topic, "/light/+/set");
        mqtt.subscribe(light_single_topic);
        ESP_LOGI(TAG, "Subscribed to lights: %s", light_single_topic.c_str());

        // base/light/group/GROUP_ID/set
        std::string light_group_topic = std::format("{}{}", config.mqtt_base_topic, "/light/group/+/set");
        mqtt.subscribe(light_group_topic);
        ESP_LOGI(TAG, "Subscribed to light groups: %s", light_group_topic.c_str());

        // base/light/broadcast/set
        std::string light_broadcast_topic = std::format("{}{}", config.mqtt_base_topic, "/light/broadcast/set");
        mqtt.subscribe(light_broadcast_topic);
        ESP_LOGI(TAG, "Subscribed to broadcast: %s", light_broadcast_topic.c_str());

        // base/config/group/set
        std::string config_group_topic = std::format("{}{}", config.mqtt_base_topic, CONFIG_DALI2MQTT_MQTT_GROUP_SET_SUBTOPIC);
        mqtt.subscribe(config_group_topic);
        ESP_LOGI(TAG, "Subscribed to group config: %s", config_group_topic.c_str());

        // base/scene/set
        std::string scene_cmd_topic = std::format("{}{}", config.mqtt_base_topic, CONFIG_DALI2MQTT_MQTT_SCENE_CMD_SUBTOPIC);
        mqtt.subscribe(scene_cmd_topic);
        ESP_LOGI(TAG, "Subscribed to scenes: %s", scene_cmd_topic.c_str());

        // base/cmd/raw
        std::string debug_topic = std::format("{}/cmd/raw", config.mqtt_base_topic);
        mqtt.subscribe(debug_topic);
        ESP_LOGW(TAG, "DEBUG INTERFACE ENABLED. Subscribed to: %s", debug_topic.c_str());

        MQTTHomeAssistantDiscovery hass_discovery;
        hass_discovery.publishAllDevices();
        ESP_LOGI(TAG, "MQTT discovery messages published.");

        DaliDeviceController::getInstance().start();
    }
} // daliMQTT