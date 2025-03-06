// src/esp32/modules/dali/dali_runtime.c
#include "DALI_Runtime.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <esp_log.h>
#include "config_manager.h"      // C header now
#include "mqtt_client.h"         // C header now
#include "dali_protocol.hpp"
#include "DALI.hpp"
#include <stdio.h>
#include <string.h>

static const char *TAG = "DALI_Module";

// Configuration parameters (loaded from ConfigManager) - global static in C
static int daliRxGpioPin = -1;
static int daliTxGpioPin = -1;
static int statusPollIntervalSec = -1;
static char *mqttCommandTopicBase = NULL;
static char *mqttStatusTopicBase = NULL;

// Task handles - global static in C
static TaskHandle_t mqttCommandHandlerTaskHandle = NULL;
static TaskHandle_t statusPollingTaskHandle = NULL;

// Forward declarations of internal functions/tasks
static void mqttCommandHandlerTask(void *pvParameters);
static void statusPollingTask(void *pvParameters);
static bool handleMqttCommand(const char *topic, const char *payload);
static void publishDaliStatus(uint8_t daliAddress, uint8_t brightness, const char *state);
static bool loadConfiguration(void);


// --- MQTT Command Handling ---
static void mqttMessageHandler(const char *topic, const char *payload) {
    ESP_LOGD(TAG, "MQTT message received on topic: %s, payload: %s", topic, payload);
    if (!handleMqttCommand(topic, payload)) {
        ESP_LOGW(TAG, "Failed to handle MQTT command on topic: %s", topic);
    }
}

static bool handleMqttCommand(const char *topic, const char *payload) {
    // Topic format: <mqttCommandTopicBase>/<daliAddress>/<command>
    // Example: dali/command/10/set_brightness, dali/command/20/turn_on

    if (mqttCommandTopicBase == NULL) return false; // Check if initialized

    if (strncmp(topic, mqttCommandTopicBase, strlen(mqttCommandTopicBase)) != 0) {
        ESP_LOGW(TAG, "Received command on unexpected topic base: %s, expected base: %s", topic, mqttCommandTopicBase);
        return false;
    }

    const char *remainingTopic = topic + strlen(mqttCommandTopicBase);
    if (strncmp(remainingTopic, "/", 1) != 0) { // Must start with '/' after base
        ESP_LOGW(TAG, "Invalid topic format after base: %s", remainingTopic);
        return false;
    }
    remainingTopic++; // Remove leading '/'

    const char *addressEndPtr = strchr(remainingTopic, '/');
    if (addressEndPtr == NULL) {
        ESP_LOGW(TAG, "No command specified after address in topic: %s", topic);
        return false;
    }

    size_t addressLen = addressEndPtr - remainingTopic;
    char addressStr[10]; // Buffer for address string, assuming max 10 digits
    if (addressLen >= sizeof(addressStr)) {
        ESP_LOGE(TAG, "DALI address string too long in topic: %s", topic);
        return false;
    }
    strncpy(addressStr, remainingTopic, addressLen);
    addressStr[addressLen] = '\0';

    const char *commandStr = addressEndPtr + 1;

    uint8_t daliAddress;
    char *endptr;
    long addressVal = strtol(addressStr, &endptr, 10);
    if (*endptr != '\0' || addressVal < 0 || addressVal > 255) {
        ESP_LOGE(TAG, "Invalid DALI address in topic: %s, value: %s", topic, addressStr);
        return false;
    }
    daliAddress = (uint8_t)addressVal;

    ESP_LOGI(TAG, "Parsed MQTT command - Address: %d, Command: %s, Payload: %s", daliAddress, commandStr, payload);

    bool commandSuccess = false;
    if (strcmp(commandStr, "set_brightness") == 0) {
        char *payload_endptr;
        long brightnessVal = strtol(payload, &payload_endptr, 10);
        if (*payload_endptr != '\0' || brightnessVal < 0 || brightnessVal > 255) {
            ESP_LOGE(TAG, "Invalid brightness payload: %s", payload);
            return false;
        }
        commandSuccess = lighting_controller_set_brightness(daliAddress, false, (uint8_t)brightnessVal);
    } else if (strcmp(commandStr, "turn_on") == 0) {
        commandSuccess = lighting_controller_turn_on(daliAddress, false);
    } else if (strcmp(commandStr, "turn_off") == 0) {
        commandSuccess = lighting_controller_turn_off(daliAddress, false);
    } else if (strcmp(commandStr, "query_status") == 0) {
        commandSuccess = lighting_controller_query_and_publish_brightness(daliAddress, false);
    }
    else {
        ESP_LOGW(TAG, "Unknown command: %s", commandStr);
        return false;
    }

    if (!commandSuccess) {
        ESP_LOGE(TAG, "Failed to execute DALI command: %s for address: %d", commandStr, daliAddress);
        return false;
    }
    return true;
}

static void mqttCommandHandlerTask(void *pvParameters) {
    while (true) {
        // MQTT client handles message reception and calls mqttMessageHandler directly
        vTaskDelay(pdMS_TO_TICKS(1000)); // Keep task alive, message handling is event-driven
    }
    vTaskDelete(NULL); // Should not reach here
}


// --- Status Polling ---
static void statusPollingTask(void *pvParameters) {
    while (true) {
        ESP_LOGI(TAG, "Performing DALI status polling...");
        device_config_map_iterator_t iterator = device_config_get_map_iterator();
        while (device_config_map_iterator_has_next(iterator)) {
            device_config_pair_t devicePair = device_config_map_iterator_next(iterator);
            uint8_t daliAddress = devicePair.address;
            if (!lighting_controller_query_and_publish_brightness(daliAddress, false)) {
                ESP_LOGW(TAG, "Status poll or publish failed for DALI address: %d", daliAddress);
            }
            vTaskDelay(pdMS_TO_TICKS(100)); // Small delay between polling each device
        }
        device_config_release_map_iterator(iterator);
        vTaskDelay(pdMS_TO_TICKS(statusPollIntervalSec * 1000)); // Wait for the configured interval
    }
    vTaskDelete(NULL); // Should not reach here
}

static void publishDaliStatus(uint8_t daliAddress, uint8_t brightness, const char *state) {
    if (mqttStatusTopicBase == NULL) return; // Check if initialized

    char brightnessTopic[100]; // Adjust buffer size as needed
    sprintf(brightnessTopic, "%s/%d/brightness", mqttStatusTopicBase, daliAddress);

    char stateTopic[100]; // Adjust buffer size as needed
    sprintf(stateTopic, "%s/%d/state", mqttStatusTopicBase, daliAddress);

    char brightnessPayload[10]; // Buffer for brightness string, assuming max 3 digits + null terminator
    sprintf(brightnessPayload, "%d", brightness);

    if (!mqtt_client_publish(brightnessTopic, brightnessPayload, 0, true)) {
        ESP_LOGW(TAG, "Failed to publish brightness status to MQTT topic: %s", brightnessTopic);
    }
    if (!mqtt_client_publish(stateTopic, state, 0, true)) {
        ESP_LOGW(TAG, "Failed to publish state status to MQTT topic: %s", stateTopic);
    }
}


static bool loadConfiguration(void) {
    daliRxGpioPin = config_manager_get_dali_rx_gpio();
    daliTxGpioPin = config_manager_get_dali_tx_gpio();
    statusPollIntervalSec = config_manager_get_status_poll_interval_sec();
    const char *cmd_topic_base = config_manager_get_mqtt_command_topic_base();
    const char *status_topic_base = config_manager_get_mqtt_status_topic_base();

    if (daliRxGpioPin == -1 || daliTxGpioPin == -1 || statusPollIntervalSec == -1 || cmd_topic_base == NULL || status_topic_base == NULL) {
        ESP_LOGE(TAG, "Failed to load all DALI module configurations. Check NVS settings.");
        return false;
    }

    if (mqttCommandTopicBase) free(mqttCommandTopicBase);
    mqttCommandTopicBase = strdup(cmd_topic_base);
    if (!mqttCommandTopicBase) return false;

    if (mqttStatusTopicBase) free(mqttStatusTopicBase);
    mqttStatusTopicBase = strdup(status_topic_base);
    if (!mqttStatusTopicBase) {
        free(mqttCommandTopicBase);
        mqttCommandTopicBase = NULL;
        return false;
    }


    ESP_LOGI(TAG, "DALI Configuration loaded: RX=%d, TX=%d, PollInterval=%d sec, CommandTopicBase=%s, StatusTopicBase=%s",
             daliRxGpioPin, daliTxGpioPin, statusPollIntervalSec, mqttCommandTopicBase, mqttStatusTopicBase);
    return true;
}


bool dali_runtime_init(void) {
    ESP_LOGI(TAG, "Initializing DALI Module...");

    if (!loadConfiguration()) {
        ESP_LOGE(TAG, "DALI Module configuration failed to load.");
        return false;
    }

    // Initialize DALI hardware layer
    if (dali_init(daliRxGpioPin, daliTxGpioPin) != ESP_OK) {
        ESP_LOGE(TAG, "DALI hardware initialization failed.");
        return false;
    }
    ESP_LOGI(TAG, "DALI hardware initialized.");

    // Initialize DALI Protocol Layer
    if (dali_protocol_init() != ESP_OK) {
        ESP_LOGE(TAG, "DALI protocol layer initialization failed.");
        return false;
    }
    ESP_LOGI(TAG, "DALI protocol layer initialized.");

    // Subscribe to MQTT command topic
    if (!mqtt_client_subscribe(mqttCommandTopicBase, 0, mqttMessageHandler)) { // Subscribe to all subtopics
        ESP_LOGE(TAG, "Failed to subscribe to MQTT command topic: %s/#", mqttCommandTopicBase);
        return false;
    }
    ESP_LOGI(TAG, "Subscribed to MQTT command topic: %s/#", mqttCommandTopicBase);


    // Create MQTT command handler task
    if (xTaskCreatePinnedToCore(mqttCommandHandlerTask, "MQTT_Cmd_Handler", 4096, NULL, 5, &mqttCommandHandlerTaskHandle, APP_CPU_NUM) != pdPASS) {
        ESP_LOGE(TAG, "Failed to create MQTT command handler task.");
        return false;
    }
    ESP_LOGI(TAG, "MQTT command handler task created.");


    // Create status polling task
    if (xTaskCreatePinnedToCore(statusPollingTask, "Status_Polling_Task", 4096, NULL, 4, &statusPollingTaskHandle, PRO_CPU_NUM) != pdPASS) {
        ESP_LOGE(TAG, "Failed to create status polling task.");
        return false;
    }
    ESP_LOGI(TAG, "Status polling task created, interval: %d seconds.", statusPollIntervalSec);


    ESP_LOGI(TAG, "DALI Module initialized successfully.");
    return true;
}


void dali_runtime_mqtt_command_handler_task(void *pvParameters) {
    mqttCommandHandlerTask(pvParameters);
}

void dali_runtime_status_polling_task(void *pvParameters) {
    statusPollingTask(pvParameters);
}