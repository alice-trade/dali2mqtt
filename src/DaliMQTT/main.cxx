#include <esp_err.h>
#include <esp_log.h>
#include <string>
#include <mutex>
#include <nvs_flash.h>

#include <DaliMQTT/config/ConfigManager.hxx>
#include <DaliMQTT/lifecycle/Lifecycle.hxx>

static constexpr char  TAG[] = "daliMQTT";
extern "C" void app_main(void) {
    ESP_LOGI("", "DALI-to-MQTT Bridge v.%s (configured at: %s)", DALIMQTT_VERSION, DALIMQTT_CONFIGURED_TIMESTAMP);
    ESP_LOGI(TAG, "DALI-to-MQTT Bridge starting...");

    auto& config = daliMQTT::ConfigManager::getInstance();
    ESP_ERROR_CHECK(config.init());
    ESP_ERROR_CHECK(config.load());


    if (config.isConfigured()) {
        ESP_LOGI(TAG, "Device is configured. Starting normal mode.");
        daliMQTT::Lifecycle::startNormalMode();
    } else {
        ESP_LOGI(TAG, "Device is not configured. Starting provisioning mode.");
        daliMQTT::Lifecycle::startProvisioningMode();
    }

    ESP_LOGI(TAG, "Application setup complete. Logic running in background tasks.");
}