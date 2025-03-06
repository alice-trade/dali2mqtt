// main entry point daliMQTT for esp32
#include <cstdio>
#include <esp_err.h>
#include <esp_log.h>
#include "DALI_Runtime.h"
#include "ethernet.h"
#include "mqtt_client.h"
#include "ConfigManager.h" // Or lighting_config.h

extern "C" void app_main(void) {
    ESP_LOGI(TAG, "Starting ESP32 DALI Controller...");

    // Initialize Ethernet
    ESP_ERROR_CHECK(EthernetManager::start());
    ESP_LOGI(TAG, "Ethernet Initialized and Started");

    // Initialize MQTT Client
    ESP_ERROR_CHECK(MqttClient::init(ConfigManager::getMqttBrokerUrl(), ConfigManager::getMqttClientId()));
    ESP_ERROR_CHECK(MqttClient::start());
    ESP_LOGI(TAG, "MQTT Client Initialized and Started");


    // Initialize DALI Module (which includes dalic init and protocol init)
    if (!DALI::init()) {
        ESP_LOGE(TAG, "DALI Module initialization failed!");
    } else {
        ESP_LOGI(TAG, "DALI Module Initialized and Started");
    }

}