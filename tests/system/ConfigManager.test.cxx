// Copyright (c) 2026 Alice-Trade Inc.
// SPDX-License-Identifier: GPL-2.0-or-later

#include "unity.h"
#include "system/ConfigManager.hxx"
#include <cstring>
#include <iostream>

using namespace daliMQTT;

static void test_config_init_and_defaults() {
    auto& cm = ConfigManager::Instance();
    TEST_ASSERT_EQUAL(ESP_OK, cm.init());

    TEST_ASSERT_EQUAL(ESP_OK, cm.load());

    AppConfig cfg = cm.getConfig();
    TEST_ASSERT_FALSE(cfg.mqtt_base_topic.empty());
    TEST_ASSERT_EQUAL_STRING("dali_bridge", cfg.mqtt_base_topic.c_str());
}

static void test_config_save_load_cycle() {
    auto& cm = ConfigManager::Instance();
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
    auto& cm = ConfigManager::Instance();

    const char* json = R"({"wifi_ssid": "NEW_WIFI", "mqtt_uri": "mqtt://test"})";
    ConfigUpdateResult result = cm.updateConfigFromJson(json);

    TEST_ASSERT_EQUAL(ConfigUpdateResult::WIFIUpdate, result);

    const AppConfig cfg = cm.getConfig();
    TEST_ASSERT_EQUAL_STRING("NEW_WIFI", cfg.wifi_ssid.c_str());
    TEST_ASSERT_EQUAL_STRING("mqtt://test", cfg.mqtt_uri.c_str());

    const char* mqtt_json = R"({"mqtt_uri": "mqtt://new-broker", "mqtt_user": "admin"})";
    result = cm.updateConfigFromJson(mqtt_json);
    TEST_ASSERT_EQUAL(ConfigUpdateResult::MQTTUpdate, result);

    const char* bad_json = R"({"garbage": 1})";
    result = cm.updateConfigFromJson(bad_json);

    TEST_ASSERT_EQUAL(ConfigUpdateResult::NoUpdate, result);

    result = cm.updateConfigFromJson(mqtt_json);
    TEST_ASSERT_EQUAL(ConfigUpdateResult::NoUpdate, result);
}

void run_config_manager_tests() {
    RUN_TEST(test_config_init_and_defaults);
    RUN_TEST(test_config_save_load_cycle);
    RUN_TEST(test_json_update);
}