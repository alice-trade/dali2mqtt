//
// Created by danil on 23.02.2025.
//

#include "config_manager.hpp"
#include <nvs.h>
#include <nvs_flash.h>
#include <esp_log.h>
#include <string>

static const char *TAG = "ConfigManager";

namespace ConfigManager {

namespace { // Anonymous namespace for private implementation details

    // NVS namespace for configurations
    const char *nvsNamespace = "dali_config";

    // Configuration keys in NVS
    const char *keyMqttBrokerUrl = "mqtt_broker_url";
    const char *keyMqttClientId = "mqtt_client_id";
    const char *keyDaliRxGpio = "dali_rx_gpio";
    const char *keyDaliTxGpio = "dali_tx_gpio";
    const char *keyStatusPollInterval = "status_poll_interval";
    const char *keyMqttCommandTopicBase = "mqtt_cmd_topic_base";
    const char *keyMqttStatusTopicBase = "mqtt_status_topic_base";

    // Default configuration values
    const char *defaultMqttBrokerUrl = "mqtt://your_mqtt_broker:1883";
    const char *defaultMqttClientId = "esp32-dali-controller";
    const int defaultDaliRxGpio = 21;
    const int defaultDaliTxGpio = 22;
    const int defaultStatusPollInterval = 60; // seconds
    const char *defaultMqttCommandTopicBase = "dali/command";
    const char *defaultMqttStatusTopicBase = "dali/status";

    std::string mqttBrokerUrl;
    std::string mqttClientId;
    int daliRxGpioPin;
    int daliTxGpioPin;
    int statusPollIntervalSec;
    std::string mqttCommandTopicBase;
    std::string mqttStatusTopicBase;

    bool loadConfigFromNVS() {
        nvs_handle_t nvsHandle;
        esp_err_t err;

        err = nvs_open(nvsNamespace, NVS_READONLY, &nvsHandle);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "NVS open failed, using default config: %s", esp_err_to_name(err));
            return false; // Use defaults if NVS open fails.
        }

        size_t required_size;

        // MQTT Broker URL
        required_size = 0;
        err = nvs_get_str(nvsHandle, keyMqttBrokerUrl, nullptr, &required_size);
        if (err == ESP_OK) {
            mqttBrokerUrl.resize(required_size);
            err = nvs_get_str(nvsHandle, keyMqttBrokerUrl, mqttBrokerUrl.data(), &required_size);
            if (err != ESP_OK) {
                ESP_LOGW(TAG, "Failed to read MQTT Broker URL from NVS, using default.");
                mqttBrokerUrl = defaultMqttBrokerUrl;
            } else {
                mqttBrokerUrl.resize(required_size - 1); // Remove null terminator
            }
        } else {
            ESP_LOGW(TAG, "MQTT Broker URL not found in NVS, using default.");
            mqttBrokerUrl = defaultMqttBrokerUrl;
        }

        // MQTT Client ID
        required_size = 0;
        err = nvs_get_str(nvsHandle, keyMqttClientId, nullptr, &required_size);
         if (err == ESP_OK) {
            mqttClientId.resize(required_size);
            err = nvs_get_str(nvsHandle, keyMqttClientId, mqttClientId.data(), &required_size);
             if (err != ESP_OK) {
                ESP_LOGW(TAG, "Failed to read MQTT Client ID from NVS, using default.");
                mqttClientId = defaultMqttClientId;
            } else {
                mqttClientId.resize(required_size - 1); // Remove null terminator
            }
        } else {
            ESP_LOGW(TAG, "MQTT Client ID not found in NVS, using default.");
            mqttClientId = defaultMqttClientId;
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
        err = nvs_get_str(nvsHandle, keyMqttCommandTopicBase, nullptr, &required_size);
         if (err == ESP_OK) {
            mqttCommandTopicBase.resize(required_size);
            err = nvs_get_str(nvsHandle, keyMqttCommandTopicBase, mqttCommandTopicBase.data(), &required_size);
             if (err != ESP_OK) {
                ESP_LOGW(TAG, "Failed to read MQTT Command Topic Base from NVS, using default.");
                mqttCommandTopicBase = defaultMqttCommandTopicBase;
            } else {
                mqttCommandTopicBase.resize(required_size - 1); // Remove null terminator
            }
        } else {
            ESP_LOGW(TAG, "MQTT Command Topic Base not found in NVS, using default.");
            mqttCommandTopicBase = defaultMqttCommandTopicBase;
        }

        // MQTT Status Topic Base
        required_size = 0;
        err = nvs_get_str(nvsHandle, keyMqttStatusTopicBase, nullptr, &required_size);
         if (err == ESP_OK) {
            mqttStatusTopicBase.resize(required_size);
            err = nvs_get_str(nvsHandle, keyMqttStatusTopicBase, mqttStatusTopicBase.data(), &required_size);
             if (err != ESP_OK) {
                ESP_LOGW(TAG, "Failed to read MQTT Status Topic Base from NVS, using default.");
                mqttStatusTopicBase = defaultMqttStatusTopicBase;
            } else {
                mqttStatusTopicBase.resize(required_size - 1); // Remove null terminator
            }
        } else {
            ESP_LOGW(TAG, "MQTT Status Topic Base not found in NVS, using default.");
            mqttStatusTopicBase = defaultMqttStatusTopicBase;
        }


        nvs_close(nvsHandle);
        return true;
    }


} // end anonymous namespace


esp_err_t init() {
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
        mqttBrokerUrl = defaultMqttBrokerUrl;
        mqttClientId = defaultMqttClientId;
        daliRxGpioPin = defaultDaliRxGpio;
        daliTxGpioPin = defaultDaliTxGpio;
        statusPollIntervalSec = defaultStatusPollInterval;
        mqttCommandTopicBase = defaultMqttCommandTopicBase;
        mqttStatusTopicBase = defaultMqttStatusTopicBase;
    }

    ESP_LOGI(TAG, "Configuration initialized");
    return ESP_OK;
}

std::string getMqttBrokerUrl() {
    return mqttBrokerUrl;
}

std::string getMqttClientId() {
    return mqttClientId;
}

int getDaliRxGpio() {
    return daliRxGpioPin;
}

int getDaliTxGpio() {
    return daliTxGpioPin;
}

int getStatusPollIntervalSec() {
    return statusPollIntervalSec;
}

std::string getMqttCommandTopicBase() {
    return mqttCommandTopicBase;
}

std::string getMqttStatusTopicBase() {
    return mqttStatusTopicBase;
}


} // namespace ConfigManager