#include <esp_log.h>
#include <DaliMQTT/config/ConfigManager.hxx>
#include <DaliMQTT/lifecycle/Lifecycle.hxx>

static constexpr char  TAG[] = "daliMQTT";
extern "C" void app_main(void) {
    ESP_LOGI("", "DALI-to-MQTT Bridge v.%s", DALIMQTT_VERSION);
    ESP_LOGI(TAG, "DALI-to-MQTT Bridge starting...");

    auto& config = daliMQTT::ConfigManager::getInstance();
    ESP_ERROR_CHECK(config.init());
    ESP_ERROR_CHECK(config.load());

    auto& logic = daliMQTT::Lifecycle::getInstance();

    if (config.isConfigured()) {
        ESP_LOGI(TAG, "Device is configured. Starting normal mode.");
        logic.startNormalMode();
    } else {
        ESP_LOGI(TAG, "Device is not configured. Starting provisioning mode.");
        logic.startProvisioningMode();
    }

    ESP_LOGI(TAG, "Application setup complete. Logic running in background tasks.");
}