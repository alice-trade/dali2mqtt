// main entry point daliMQTT for esp32
#include <cstdio>
#include <esp_log.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "ethernet.h"
#include "mqtt_client.hpp"
#include "mqtt_protocol.hpp"
#include "DALI.hpp"
#include "dali.h"
#include "ConfigManager.hpp" // Or lighting_config.h

static const char *TAG = "app_main";

extern "C" void app_main(void) {
    ESP_LOGI(TAG, "Starting ESP32 DALI Controller...");

    // Initialize configuration
    ESP_ERROR_CHECK(ConfigManager::init());
    ESP_LOGI(TAG, "Config Manager initialized");

    // Initialize Ethernet
    ESP_ERROR_CHECK(EthernetManager::init());
    ESP_ERROR_CHECK(EthernetManager::start());
    ESP_LOGI(TAG, "Ethernet Initialized and Started");

    // Initialize MQTT Client
    ESP_ERROR_CHECK(MqttClient::init(ConfigManager::getMqttBrokerUrl(), ConfigManager::getMqttClientId()));
    ESP_ERROR_CHECK(MqttClient::start());
    ESP_LOGI(TAG, "MQTT Client Initialized and Started");

    // Initialize MQTT Protocol (subscriptions)
    ESP_ERROR_CHECK(MqttProtocol::init());
    ESP_LOGI(TAG, "MQTT Protocol Initialized");

    // Initialize DALI
    ESP_ERROR_CHECK(dali_init(static_cast<gpio_num_t>(ConfigManager::getDaliRxGpio()), static_cast<gpio_num_t>(ConfigManager::getDaliTxGpio())));
    ESP_LOGI(TAG, "DALI Initialized");

    // Initialize DALI Protocol Layer
    ESP_ERROR_CHECK(DaliProtocol::init());
    ESP_LOGI(TAG, "DALI Protocol Layer Initialized");

    // Initialize Device Configuration
    DeviceConfig::init(); // Or LightingConfig::init();
    ESP_LOGI(TAG, "Device Config Initialized");

    // Initialize Lighting Controller
    LightingController::init();
    ESP_LOGI(TAG, "Lighting Controller Initialized");


    while (true) {
        vTaskDelay(pdMS_TO_TICKS(10000)); // 10 seconds delay
        ESP_LOGI(TAG, "System is running...");
        // Add any periodic tasks here, e.g., status checks, etc.
        LightingController::queryAndPublishBrightness(DeviceConfig::getDaliAddress("living_room_light"), false); // Example query
    }
}