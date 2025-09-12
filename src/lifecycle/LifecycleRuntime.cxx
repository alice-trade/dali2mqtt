#include <esp_log.h>

#include "ConfigManager.hxx"
#include "sdkconfig.h"
#include "DaliAPI.hxx"
#include "Lifecycle.hxx"
#include "MQTTClient.hxx"
#include "WebUI.hxx"
#include "Wifi.hxx"

namespace daliMQTT {
    static constexpr char  TAG[] = "LifecycleRuntime";

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
}