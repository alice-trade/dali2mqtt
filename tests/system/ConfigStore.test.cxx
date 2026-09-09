// Copyright (c) 2026 Alice-Trade Inc.
// SPDX-License-Identifier: GPL-3.0-or-later

#include <unity.h>
#include "system/ConfigStore.hxx"

using namespace daliMQTT;

static void test_config_store_init_and_defaults() {
    ConfigStore store;
    TEST_ASSERT_EQUAL(ESP_OK, store.init());
    TEST_ASSERT_EQUAL(ESP_OK, store.load());

    auto cfg = store.get();
    TEST_ASSERT_NOT_NULL(cfg.get());
    TEST_ASSERT_EQUAL_STRING(CONFIG_DALI2MQTT_MQTT_BASE_TOPIC, cfg->mqttBaseTopic.c_str());
    TEST_ASSERT_EQUAL_STRING(CONFIG_DALI2MQTT_WEBUI_DEFAULT_USER, cfg->httpUser.c_str());
    TEST_ASSERT_EQUAL_UINT32(CONFIG_DALI2MQTT_DALI_DEFAULT_POLL_INTERVAL_MS, cfg->daliPollIntervalMs);
    TEST_ASSERT_FALSE(cfg->clientId.empty());
}

static void test_config_store_save_and_reload() {
    ConfigStore store;
    TEST_ASSERT_EQUAL(ESP_OK, store.init());
    TEST_ASSERT_EQUAL(ESP_OK, store.load());

    ConfigStructure customCfg = *store.get();
    customCfg.wifiSsid = "TestEmbeddedSSID";
    customCfg.wifiPass = "SecretEmbeddedPass";
    customCfg.mqttUri = "mqtt://192.168.1.200:1883";
    customCfg.buses[0].rxPin = 21;
    customCfg.buses[0].txPin = 22;
    customCfg.daliPollIntervalMs = 45000;
    customCfg.hassDiscoveryEnabled = true;

    TEST_ASSERT_EQUAL(ESP_OK, store.save(customCfg));

    ConfigStore reloadedStore;
    TEST_ASSERT_EQUAL(ESP_OK, reloadedStore.init());
    TEST_ASSERT_EQUAL(ESP_OK, reloadedStore.load());

    auto loaded = reloadedStore.get();
    TEST_ASSERT_EQUAL_STRING("TestEmbeddedSSID", loaded->wifiSsid.c_str());
    TEST_ASSERT_EQUAL_STRING("SecretEmbeddedPass", loaded->wifiPass.c_str());
    TEST_ASSERT_EQUAL_STRING("mqtt://192.168.1.200:1883", loaded->mqttUri.c_str());
    TEST_ASSERT_EQUAL_INT8(21, loaded->buses[0].rxPin);
    TEST_ASSERT_EQUAL_INT8(22, loaded->buses[0].txPin);
    TEST_ASSERT_EQUAL_UINT32(45000, loaded->daliPollIntervalMs);
    TEST_ASSERT_TRUE(loaded->hassDiscoveryEnabled);
    TEST_ASSERT_TRUE(loaded->configuredFlag);
    TEST_ASSERT_TRUE(reloadedStore.isConfigured());
}

static void test_config_store_factory_reset() {
    ConfigStore store;
    TEST_ASSERT_EQUAL(ESP_OK, store.init());

    ConfigStructure customCfg = *store.get();
    customCfg.wifiSsid = "ToBeDeleted";
    TEST_ASSERT_EQUAL(ESP_OK, store.save(customCfg));

    TEST_ASSERT_EQUAL(ESP_OK, store.factoryReset());

    auto cfg = store.get();
    TEST_ASSERT_TRUE(cfg->wifiSsid.empty());
    TEST_ASSERT_EQUAL_STRING(CONFIG_DALI2MQTT_MQTT_BASE_TOPIC, cfg->mqttBaseTopic.c_str());
}

static void test_config_store_client_id_auto_gen() {
    ConfigStore store;
    TEST_ASSERT_EQUAL(ESP_OK, store.init());

    ConfigStructure customCfg = *store.get();
    customCfg.clientId.clear();
    TEST_ASSERT_EQUAL(ESP_OK, store.save(customCfg));

    ConfigStore reloaded;
    TEST_ASSERT_EQUAL(ESP_OK, reloaded.init());
    TEST_ASSERT_EQUAL(ESP_OK, reloaded.load());

    auto cfg = reloaded.get();
    TEST_ASSERT_FALSE(cfg->clientId.empty());
    TEST_ASSERT_TRUE(cfg->clientId.starts_with("dali_"));
}

void run_config_store_tests() {
    RUN_TEST(test_config_store_init_and_defaults);
    RUN_TEST(test_config_store_save_and_reload);
    RUN_TEST(test_config_store_factory_reset);
    RUN_TEST(test_config_store_client_id_auto_gen);
}