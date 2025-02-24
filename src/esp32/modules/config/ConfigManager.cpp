//
// Created by danil on 23.02.2025.
//

#include <nvs_flash.h>
#include <esp_log.h>
#include <string>

static const char *TAG = "ConfigManager";

namespace ConfigManager {

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
        // Load configuration from NVS or file (implementation needed)
        ESP_LOGI(TAG, "Configuration initialized");
        return ESP_OK;
    }

    std::string getMqttBrokerUrl() {
        // Implement configuration retrieval logic (e.g., from NVS, file, defaults)
        return "mqtt://your_mqtt_broker:1883"; // Placeholder
    }

    std::string getMqttClientId() {
        return "esp32-dali-controller"; // Placeholder
    }

    int getDaliRxGpio() {
        return 21; // Placeholder - replace with actual GPIO number from config or define
    }

    int getDaliTxGpio() {
        return 22; // Placeholder - replace with actual GPIO number from config or define
    }


} // namespace ConfigManager