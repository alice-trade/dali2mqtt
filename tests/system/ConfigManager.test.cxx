#include "unity.h"
#include "system/ConfigManager.hxx"
#include <cstring>
#include <iostream>

using namespace daliMQTT;

static void test_config_init_and_defaults() {
    auto& cm = ConfigManager::getInstance();
    TEST_ASSERT_EQUAL(ESP_OK, cm.init());

    TEST_ASSERT_EQUAL(ESP_OK, cm.load());

    AppConfig cfg = cm.getConfig();
    TEST_ASSERT_FALSE(cfg.mqtt_base_topic.empty());
    TEST_ASSERT_EQUAL_STRING("dali_bridge", cfg.mqtt_base_topic.c_str());
}

static void test_config_save_load_cycle() {
    auto& cm = ConfigManager::getInstance();
    AppConfig original = cm.getConfig();

    AppConfig testConfig = original;
    testConfig.wifi_ssid = "TEST_UNIT_SSID";
    testConfig.dali_poll_interval_ms = 12345;

    TEST_ASSERT_EQUAL(ESP_OK, cm.saveMainConfig(testConfig));

    TEST_ASSERT_EQUAL(ESP_OK, cm.load());

    AppConfig loaded = cm.getConfig();
    TEST_ASSERT_EQUAL_STRING("TEST_UNIT_SSID", loaded.wifi_ssid.c_str());
    TEST_ASSERT_EQUAL_UINT32(12345, loaded.dali_poll_interval_ms);

    cm.saveMainConfig(original);
}

static void test_json_update() {
    auto& cm = ConfigManager::getInstance();
    bool reboot = false;

    const char* json = R"({"wifi_ssid": "NEW_WIFI", "mqtt_uri": "mqtt://test"})";
    esp_err_t err = cm.updateConfigFromJson(json, reboot);

    TEST_ASSERT_EQUAL(ESP_OK, err);
    TEST_ASSERT_TRUE(reboot);

    AppConfig cfg = cm.getConfig();
    TEST_ASSERT_EQUAL_STRING("NEW_WIFI", cfg.wifi_ssid.c_str());

    const char* bad_json = R"({"garbage": 1})";

    bool rb2 = false;
    cm.updateConfigFromJson(bad_json, rb2);
    TEST_ASSERT_FALSE(rb2);
}

void run_config_manager_tests() {
    RUN_TEST(test_config_init_and_defaults);
    RUN_TEST(test_config_save_load_cycle);
    RUN_TEST(test_json_update);
}