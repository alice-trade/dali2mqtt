// src/esp32/modules/config/config_manager.c
#include "ConfigManager.h"
#include <nvs.h>
#include <nvs_flash.h>
#include <esp_log.h>
#include <cstring> // For C string functions

static const char *TAG = "ConfigManager";

// NVS namespace for configurations
static const char *nvsNamespace = "dali_config";

// Configuration keys in NVS
static const char *keyMqttBrokerUrl = "mqtt_broker_url";
static const char *keyMqttClientId = "mqtt_client_id";
static const char *keyDaliRxGpio = "dali_rx_gpio";
static const char *keyDaliTxGpio = "dali_tx_gpio";
static const char *keyStatusPollInterval = "status_poll_interval";
static const char *keyMqttCommandTopicBase = "mqtt_cmd_topic_base";
static const char *keyMqttStatusTopicBase = "mqtt_status_topic_base";

// Default configuration values (C strings now)
static const char *defaultMqttBrokerUrl = "mqtt://your_mqtt_broker:1883";
static const char *defaultMqttClientId = "esp32-dali-controller";
static const int defaultDaliRxGpio = 21;
static const int defaultDaliTxGpio = 22;
static const int defaultStatusPollInterval = 60; // seconds
static const char *defaultMqttCommandTopicBase = "dali/command";
static const char *defaultMqttStatusTopicBase = "dali/status";

static char *mqttBrokerUrl = NULL;
static char *mqttClientId = NULL;
static int daliRxGpioPin = -1;
static int daliTxGpioPin = -1;
static int statusPollIntervalSec = -1;
static char *mqttCommandTopicBase = NULL;
static char *mqttStatusTopicBase = NULL;

static bool loadConfigFromNVS(void) {
    nvs_handle_t nvsHandle;
    esp_err_t err;

    err = nvs_open(nvsNamespace, NVS_READONLY, &nvsHandle);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "NVS open failed, using default config: %s", esp_err_to_name(err));
        return false;
    }

    size_t required_size;
    char *temp_str;

    // MQTT Broker URL
    required_size = 0;
    err = nvs_get_str(nvsHandle, keyMqttBrokerUrl, NULL, &required_size);
    if (err == ESP_OK) {
        temp_str = (char*)malloc(required_size);
        if (temp_str == NULL) goto nvs_error;
        err = nvs_get_str(nvsHandle, keyMqttBrokerUrl, temp_str, &required_size);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "Failed to read MQTT Broker URL from NVS, using default.");
            mqttBrokerUrl = strdup(defaultMqttBrokerUrl);
            free(temp_str);
        } else {
            mqttBrokerUrl = temp_str;
        }
    } else {
        ESP_LOGW(TAG, "MQTT Broker URL not found in NVS, using default.");
        mqttBrokerUrl = strdup(defaultMqttBrokerUrl);
    }

    // MQTT Client ID
    required_size = 0;
    err = nvs_get_str(nvsHandle, keyMqttClientId, NULL, &required_size);
    if (err == ESP_OK) {
        temp_str = (char*)malloc(required_size);
        if (temp_str == NULL) goto nvs_error;
        err = nvs_get_str(nvsHandle, keyMqttClientId, temp_str, &required_size);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "Failed to read MQTT Client ID from NVS, using default.");
            free(mqttBrokerUrl); mqttBrokerUrl = NULL;
            mqttClientId = strdup(defaultMqttClientId);
            free(temp_str);
        } else {
            mqttClientId = temp_str;
        }
    } else {
        ESP_LOGW(TAG, "MQTT Client ID not found in NVS, using default.");
        free(mqttBrokerUrl); mqttBrokerUrl = NULL;
        mqttClientId = strdup(defaultMqttClientId);
    }

    // DALI RX GPIO
    err = nvs_get_i32(nvsHandle, keyDaliRxGpio, &daliRxGpioPin);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "DALI RX GPIO not found in NVS, using default.");
        daliRxGpioPin = defaultDaliRxGpio;
    }

    // DALI TX GPIO
    err = nvs_get_i32(nvsHandle, keyDaliTxGpio, &daliTxGpioPin);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "DALI TX GPIO not found in NVS, using default.");
        daliTxGpioPin = defaultDaliTxGpio;
    }

    // Status Poll Interval
    err = nvs_get_i32(nvsHandle, keyStatusPollInterval, &statusPollIntervalSec);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Status Poll Interval not found in NVS, using default.");
        statusPollIntervalSec = defaultStatusPollInterval;
    }

    // MQTT Command Topic Base
    required_size = 0;
    err = nvs_get_str(nvsHandle, keyMqttCommandTopicBase, NULL, &required_size);
    if (err == ESP_OK) {
        temp_str = (char*)malloc(required_size);
        if (temp_str == NULL) goto nvs_error;
        err = nvs_get_str(nvsHandle, keyMqttCommandTopicBase, temp_str, &required_size);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "Failed to read MQTT Command Topic Base from NVS, using default.");
            free(mqttBrokerUrl); mqttBrokerUrl = NULL;
            free(mqttClientId); mqttClientId = NULL;
            mqttCommandTopicBase = strdup(defaultMqttCommandTopicBase);
            free(temp_str);
        } else {
            mqttCommandTopicBase = temp_str;
        }
    } else {
        ESP_LOGW(TAG, "MQTT Command Topic Base not found in NVS, using default.");
        free(mqttBrokerUrl); mqttBrokerUrl = NULL;
        free(mqttClientId); mqttClientId = NULL;
        mqttCommandTopicBase = strdup(defaultMqttCommandTopicBase);
    }

    // MQTT Status Topic Base
    required_size = 0;
    err = nvs_get_str(nvsHandle, keyMqttStatusTopicBase, NULL, &required_size);
    if (err == ESP_OK) {
        temp_str = (char*)malloc(required_size);
        if (temp_str == NULL) goto nvs_error;
        err = nvs_get_str(nvsHandle, keyMqttStatusTopicBase, temp_str, &required_size);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "Failed to read MQTT Status Topic Base from NVS, using default.");
            free(mqttBrokerUrl); mqttBrokerUrl = NULL;
            free(mqttClientId); mqttClientId = NULL;
            free(mqttCommandTopicBase); mqttCommandTopicBase = NULL;
            mqttStatusTopicBase = strdup(defaultMqttStatusTopicBase);
            free(temp_str);
        } else {
            mqttStatusTopicBase = temp_str;
        }
    } else {
        ESP_LOGW(TAG, "MQTT Status Topic Base not found in NVS, using default.");
        free(mqttBrokerUrl); mqttBrokerUrl = NULL;
        free(mqttClientId); mqttClientId = NULL;
        free(mqttCommandTopicBase); mqttCommandTopicBase = NULL;
        mqttStatusTopicBase = strdup(defaultMqttStatusTopicBase);
    }


    nvs_close(nvsHandle);
    return true;

nvs_error:
    nvs_close(nvsHandle);
    ESP_LOGE(TAG, "Memory allocation error during NVS load.");
    free(mqttBrokerUrl); if(mqttBrokerUrl) mqttBrokerUrl = NULL;
    free(mqttClientId); if(mqttClientId) mqttClientId = NULL;
    free(mqttCommandTopicBase); if(mqttCommandTopicBase) mqttCommandTopicBase = NULL;
    free(mqttStatusTopicBase); if(mqttStatusTopicBase) mqttStatusTopicBase = NULL;
    return false; // Indicate failure
}


esp_err_t config_manager_init(void) {
    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "NVS Flash init failed: %s", esp_err_to_name(ret));
        return ret;
    }

    if (!loadConfigFromNVS()) {
        ESP_LOGI(TAG, "Using default configuration values.");
        mqttBrokerUrl = strdup(defaultMqttBrokerUrl);
        mqttClientId = strdup(defaultMqttClientId);
        daliRxGpioPin = defaultDaliRxGpio;
        daliTxGpioPin = defaultDaliTxGpio;
        statusPollIntervalSec = defaultStatusPollInterval;
        mqttCommandTopicBase = strdup(defaultMqttCommandTopicBase);
        mqttStatusTopicBase = strdup(defaultMqttStatusTopicBase);
         if (!mqttBrokerUrl || !mqttClientId || !mqttCommandTopicBase || !mqttStatusTopicBase) {
            ESP_LOGE(TAG, "Memory allocation error while setting default config.");
            return ESP_FAIL; // Or handle memory allocation failure more gracefully
        }
    }

    ESP_LOGI(TAG, "Configuration initialized");
    return ESP_OK;
}

const char* config_manager_get_mqtt_broker_url(void) {
    return mqttBrokerUrl;
}

const char* config_manager_get_mqtt_client_id(void) {
    return mqttClientId;
}

int config_manager_get_dali_rx_gpio(void) {
    return daliRxGpioPin;
}

int config_manager_get_dali_tx_gpio(void) {
    return daliTxGpioPin;
}

int config_manager_get_status_poll_interval_sec(void) {
    return statusPollIntervalSec;
}

const char* config_manager_get_mqtt_command_topic_base(void) {
    return mqttCommandTopicBase;
}

const char* config_manager_get_mqtt_status_topic_base(void) {
    return mqttStatusTopicBase;
}