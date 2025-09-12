#include "unity.h"
#include "DaliMQTT/config/ConfigManager.hxx"
#include "nvs_flash.h"
#include "sdkconfig.h"

using namespace daliMQTT;

// --- Test Group Setup ---
static void setUp(void) {
    // Обеспечиваем чистое состояние для каждого теста
    ESP_ERROR_CHECK(nvs_flash_erase());
    ESP_ERROR_CHECK(nvs_flash_init());
}

static void tearDown(void) {
    // Очистка после теста
    nvs_flash_deinit();
}

// --- Test Cases ---

TEST_CASE("ConfigManager loads default values on first boot", "[config]") {
    setUp();
    auto& configManager = ConfigManager::getInstance();
    TEST_ASSERT_EQUAL(ESP_OK, configManager.init());
    TEST_ASSERT_EQUAL(ESP_OK, configManager.load());

    AppConfig cfg = configManager.getConfig();

    TEST_ASSERT_EQUAL_STRING("", cfg.wifi_ssid.c_str());
    TEST_ASSERT_EQUAL_STRING("", cfg.wifi_password.c_str());
    TEST_ASSERT_EQUAL_STRING("", cfg.mqtt_uri.c_str());
    TEST_ASSERT_EQUAL_STRING(CONFIG_DALI2MQTT_MQTT_BASE_TOPIC, cfg.mqtt_base_topic.c_str());
    TEST_ASSERT_EQUAL_STRING(CONFIG_DALI2MQTT_WEBUI_DEFAULT_USER, cfg.http_user.c_str());
    TEST_ASSERT_EQUAL_UINT32(CONFIG_DALI2MQTT_DALI_DEFAULT_POLL_INTERVAL_MS, cfg.dali_poll_interval_ms);
    TEST_ASSERT_FALSE(configManager.isConfigured());
    tearDown();
}

TEST_CASE("ConfigManager saves and loads configuration correctly", "[config]") {
    setUp();
    auto& configManager = ConfigManager::getInstance();
    TEST_ASSERT_EQUAL(ESP_OK, configManager.init());

    AppConfig test_cfg;
    test_cfg.wifi_ssid = "my_test_ssid";
    test_cfg.wifi_password = "my_test_password";
    test_cfg.mqtt_uri = "mqtt://1.2.3.4";
    test_cfg.mqtt_client_id = "test_client";
    test_cfg.mqtt_base_topic = "test/base";
    test_cfg.http_user = "test_user";
    test_cfg.http_pass = "test_pass";
    test_cfg.dali_poll_interval_ms = 12345;

    configManager.setConfig(test_cfg);
    TEST_ASSERT_EQUAL(ESP_OK, configManager.save());

    // Перезагружаем конфигурацию, чтобы убедиться, что она читается из NVS
    TEST_ASSERT_EQUAL(ESP_OK, configManager.load());
    AppConfig loaded_cfg = configManager.getConfig();

    TEST_ASSERT_EQUAL_STRING(test_cfg.wifi_ssid.c_str(), loaded_cfg.wifi_ssid.c_str());
    TEST_ASSERT_EQUAL_STRING(test_cfg.wifi_password.c_str(), loaded_cfg.wifi_password.c_str());
    TEST_ASSERT_EQUAL_STRING(test_cfg.mqtt_uri.c_str(), loaded_cfg.mqtt_uri.c_str());
    TEST_ASSERT_EQUAL_STRING(test_cfg.mqtt_client_id.c_str(), loaded_cfg.mqtt_client_id.c_str());
    TEST_ASSERT_EQUAL_STRING(test_cfg.mqtt_base_topic.c_str(), loaded_cfg.mqtt_base_topic.c_str());
    TEST_ASSERT_EQUAL_STRING(test_cfg.http_user.c_str(), loaded_cfg.http_user.c_str());
    TEST_ASSERT_EQUAL_STRING(test_cfg.http_pass.c_str(), loaded_cfg.http_pass.c_str());

    // isConfigured() должен стать true после первого сохранения
    TEST_ASSERT_TRUE(configManager.isConfigured());
    tearDown();
}

TEST_CASE("ConfigManager isConfigured flag works as expected", "[config]")
{
    setUp();
    auto& configManager = ConfigManager::getInstance();
    configManager.init();
    configManager.load();

    TEST_ASSERT_FALSE(configManager.isConfigured());

    AppConfig cfg = configManager.getConfig();
    cfg.wifi_ssid = "any_ssid"; // Достаточно для конфигурации
    configManager.setConfig(cfg);
    configManager.save();

    TEST_ASSERT_TRUE(configManager.isConfigured());
    tearDown();
}


// --- Test Runner ---
extern "C" void run_config_manager_tests(void) {
    UNITY_BEGIN();
    RUN_TEST(test_config_manager_loads_default_values_on_first_boot);
    RUN_TEST(test_config_manager_saves_and_loads_configuration_correctly);
    RUN_TEST(test_config_manager_isConfigured_flag_works_as_expected);
    UNITY_END();
}