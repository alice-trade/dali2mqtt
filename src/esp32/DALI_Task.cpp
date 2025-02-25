#include "DALI.hpp"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "DALI_Module";


void mqttCommandHandlerTask(void *pvParameters) {
    while (true) {
        // MQTT client handles message reception and calls mqttMessageHandler directly
        vTaskDelay(pdMS_TO_TICKS(1000)); // Keep task alive, message handling is event-driven
    }
    vTaskDelete(nullptr); // Should not reach here
}


// --- Status Polling ---
void statusPollingTask(void *pvParameters) {
    while (true) {
        //                ESP_LOGI(TAG, "Performing DALI status polling...");
        for (const auto& devicePair : DeviceConfig::getDeviceNameToAddressMap()) { // Iterate through configured devices
            uint8_t daliAddress = devicePair.second;
            if (!LightingController::queryAndPublishBrightness(daliAddress, false)) {
                //                        ESP_LOGW(TAG, "Status poll or publish failed for DALI address: %d", daliAddress);
            }
            vTaskDelay(pdMS_TO_TICKS(100)); // Small delay between polling each device
        }
        vTaskDelay(pdMS_TO_TICKS(statusPollIntervalSec * 1000)); // Wait for the configured interval
    }
    vTaskDelete(nullptr); // Should not reach here
}

bool init() {
        ESP_LOGI(TAG, "Initializing DALI Module...");

        if (!loadConfiguration()) {
            ESP_LOGE(TAG, "DALI Module configuration failed to load.");
            return false;
        }

        // Initialize DALI hardware layer
        if (dali_init(static_cast<gpio_num_t>(daliRxGpioPin), static_cast<gpio_num_t>(daliTxGpioPin)) != ESP_OK) {
            ESP_LOGE(TAG, "DALI hardware initialization failed.");
            return false;
        }
        ESP_LOGI(TAG, "DALI hardware initialized.");

        // Initialize DALI Protocol Layer
        if (DaliProtocol::init() != ESP_OK) {
            ESP_LOGE(TAG, "DALI protocol layer initialization failed.");
            return false;
        }
        ESP_LOGI(TAG, "DALI protocol layer initialized.");

        // Subscribe to MQTT command topic
        if (!MqttClient::subscribe(mqttCommandTopicBase + "/#", 0, mqttMessageHandler)) { // Subscribe to all subtopics
            ESP_LOGE(TAG, "Failed to subscribe to MQTT command topic: %s/#", mqttCommandTopicBase.c_str());
            return false;
        }
        ESP_LOGI(TAG, "Subscribed to MQTT command topic: %s/#", mqttCommandTopicBase.c_str());


        // Create MQTT command handler task
        if (xTaskCreatePinnedToCore(mqttCommandHandlerTask, "MQTT_Cmd_Handler", 4096, nullptr, 5, &mqttCommandHandlerTaskHandle, APP_CPU_NUM) != pdPASS) {
            ESP_LOGE(TAG, "Failed to create MQTT command handler task.");
            return false;
        }
        ESP_LOGI(TAG, "MQTT command handler task created.");


        // Create status polling task
        if (xTaskCreatePinnedToCore(statusPollingTask, "Status_Polling_Task", 4096, nullptr, 4, &statusPollingTaskHandle, PRO_CPU_NUM) != pdPASS) {
            ESP_LOGE(TAG, "Failed to create status polling task.");
            return false;
        }
        ESP_LOGI(TAG, "Status polling task created, interval: %d seconds.", statusPollIntervalSec);


        ESP_LOGI(TAG, "DALI Module initialized successfully.");
        return true;
    }