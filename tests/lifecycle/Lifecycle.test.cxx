#include "unity.h"
#include "DaliMQTT/lifecycle/Lifecycle.hxx"
#include "DaliMQTT/config/ConfigManager.hxx"
#include "DaliMQTT/wifi/Wifi.hxx"
#include "nvs_flash.h"

using namespace daliMQTT;

// --- Test Setup ---
static void lifecycle_setup(void) {
    ESP_ERROR_CHECK(nvs_flash_erase());
    ESP_ERROR_CHECK(nvs_flash_init());
    ConfigManager::getInstance().init();
    ConfigManager::getInstance().load();
    Wifi::getInstance().init();
}

static void lifecycle_teardown(void) {
    Wifi::getInstance().disconnect();
    vTaskDelay(pdMS_TO_TICKS(500));
}

// --- Test Cases ---

TEST_CASE("Lifecycle starts in provisioning mode when not configured", "[lifecycle]") {
    lifecycle_setup();
    
    // Убеждаемся, что система не сконфигурирована
    TEST_ASSERT_FALSE(ConfigManager::getInstance().isConfigured());

    Lifecycle::getInstance().startProvisioningMode();
    vTaskDelay(pdMS_TO_TICKS(1000)); // Даем время на запуск

    // Проверяем побочный эффект: WiFi должен быть в режиме AP
    TEST_ASSERT_EQUAL(Wifi::Status::AP_MODE, Wifi::getInstance().getStatus());

    lifecycle_teardown();
}

TEST_CASE("Lifecycle starts in normal mode when configured", "[lifecycle]") {
    lifecycle_setup();
    
    // Сохраняем минимально необходимую конфигурацию
    AppConfig cfg = ConfigManager::getInstance().getConfig();
    cfg.wifi_ssid = "dummy_ssid"; // Достаточно, чтобы isConfigured() вернул true
    cfg.mqtt_uri = "mqtt://localhost";
    ConfigManager::getInstance().setConfig(cfg);
    ConfigManager::getInstance().save();
    
    // Убеждаемся, что система сконфигурирована
    TEST_ASSERT_TRUE(ConfigManager::getInstance().isConfigured());

    Lifecycle::getInstance().startNormalMode();
    vTaskDelay(pdMS_TO_TICKS(1000)); // Даем время на запуск

    // Проверяем побочный эффект: WiFi должен пытаться подключиться
    // Примечание: он перейдет в DISCONNECTED, если SSID не найден, но начальный статус должен быть CONNECTING
    // Так как тест быстрый, мы можем не успеть поймать CONNECTING, но он точно не должен быть AP_MODE
    TEST_ASSERT_NOT_EQUAL(Wifi::Status::AP_MODE, Wifi::getInstance().getStatus());

    lifecycle_teardown();
}

// --- Test Runner ---
extern "C" void run_lifecycle_tests(void) {
    UNITY_BEGIN();
    RUN_TEST(test_lifecycle_starts_in_provisioning_mode_when_not_configured);
    // Для этого теста нужен доступный SSID, иначе он будет бесконечно переподключаться.
    // Пропустим его в автоматическом режиме, но оставим как пример.
    // RUN_TEST(test_lifecycle_starts_in_normal_mode_when_configured);
    UNITY_END();
}