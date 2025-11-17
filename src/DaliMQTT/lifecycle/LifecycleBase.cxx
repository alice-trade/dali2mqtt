#include "ConfigManager.hxx"
#include "DaliAPI.hxx"
#include "DaliGroupManagement.hxx"
#include "DaliSceneManagement.hxx"
#include "Lifecycle.hxx"
#include "MQTTClient.hxx"
#include "DaliDeviceController.hxx"
#include "WebUI.hxx"
#include "Wifi.hxx"
#include "SyslogConfig.hxx"


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
        ESP_LOGI(TAG, "Web server for provisioning is running.");
    }

    void Lifecycle::startNormalMode() {
        ESP_LOGI(TAG, "Starting in normal mode...");
        auto config = ConfigManager::getInstance().getConfig();

        auto& wifi = Wifi::getInstance();
        wifi.init();
        wifi.onConnected = [this, config]() {
            ESP_LOGI(TAG, "WiFi connected, starting MQTT...");
            this->setupAndRunMqtt();
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

        wifi.connectToAP(config.wifi_ssid, config.wifi_password);
    }
} // daliMQTT