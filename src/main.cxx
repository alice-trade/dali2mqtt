#include "esp_log.h"
#include "ConfigManager.hxx"
#include "LifecycleBase.hxx"

static const char* TAG = "MAIN";

extern "C" void app_main(void) {
    ESP_LOGI(TAG, "DALI-to-MQTT Bridge starting...");

    // 1. Инициализируем и загружаем конфигурацию
    auto& config = daliMQTT::ConfigManager::getInstance();
    ESP_ERROR_CHECK(config.init());
    ESP_ERROR_CHECK(config.load());

    // 2. Получаем экземпляр композитора логики
    auto& logic = daliMQTT::LifecycleBase::getInstance();

    // 3. Проверяем, настроена ли система
    if (config.isConfigured()) {
        ESP_LOGI(TAG, "Device is configured. Starting normal mode.");
        logic.startNormalMode();
    } else {
        ESP_LOGI(TAG, "Device is not configured. Starting provisioning mode.");
        logic.startProvisioningMode();
    }

    ESP_LOGI(TAG, "Application setup complete. Logic running in background tasks.");
}