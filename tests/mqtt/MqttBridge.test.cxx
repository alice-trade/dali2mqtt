// Copyright (c) 2026 Alice-Trade Inc.
// SPDX-License-Identifier: GPL-3.0-or-later

#include <unity.h>
#include "mqtt/MqttClient.hxx"
#include "mqtt/MqttBridge.hxx"
#include "mqtt/HomeAssistantDiscovery.hxx"
#include "system/Application.hxx"

using namespace daliMQTT;

static void test_mqtt_client_lifecycle() {
    MqttClient client;
    TEST_ASSERT_EQUAL(MqttStatus::Disconnected, client.getStatus());
    TEST_ASSERT_FALSE(client.isConnected());

    ConfigStructure cfg = makeDefaultConfig();
    cfg.mqttUri = "mqtt://127.0.0.1:1883";
    cfg.clientId = "TestClientESP";

    TEST_ASSERT_EQUAL(ESP_OK, client.init(cfg));
    TEST_ASSERT_EQUAL(MqttStatus::Disconnected, client.getStatus());

    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, client.publish("test/topic", "hello", 0, false));

    client.disconnect();
    TEST_ASSERT_EQUAL(MqttStatus::Disconnected, client.getStatus());
}

static void test_mqtt_bridge_queue_bounds() {
    auto ctx = std::make_unique<Application>();
    MqttBridge bridge(ctx->mqttClient, ctx->dali.Registry(), ctx->dali.Bus(), ctx->config, ctx->ota, ctx->network);

    const char* topic = "dali_bridge/light/000001/set";
    const char* payload = R"({"state":"ON","brightness":200})";
    TEST_ASSERT_TRUE(bridge.enqueueIncomingMessage(topic, strlen(topic), payload, strlen(payload)));

    char longTopic[150];
    memset(longTopic, 'a', sizeof(longTopic));
    longTopic[sizeof(longTopic) - 1] = '\0';
    TEST_ASSERT_FALSE(bridge.enqueueIncomingMessage(longTopic, strlen(longTopic), payload, strlen(payload)));

    char longPayload[400];
    memset(longPayload, 'x', sizeof(longPayload));
    longPayload[sizeof(longPayload) - 1] = '\0';
    TEST_ASSERT_FALSE(bridge.enqueueIncomingMessage(topic, strlen(topic), longPayload, strlen(longPayload)));

    TEST_ASSERT_FALSE(bridge.enqueueIncomingMessage(nullptr, 0, nullptr, 0));
}

static void test_ha_discovery_light_payload() {
    auto ctx = std::make_unique<Application>();
    HomeAssistantDiscovery disc(ctx->mqttClient, ctx->dali.Registry());

    ConfigStructure cfg = makeDefaultConfig();
    cfg.clientId = "TestGw";
    cfg.mqttBaseTopic = "dali_home";
    cfg.hassDiscoveryEnabled = true;

    ControlGear gear;
    gear.longAddress = 0x123456;
    gear.internalAddress = DaliInternalAddr(0, 5);
    gear.color = ColorFeatures{.supportsRgb = true, .supportsTc = true};

    disc.publishLight(gear, cfg);
    disc.publishGroup(0, 3, cfg);
    disc.publishSceneSelector(0, cfg);
    disc.publishOtaUpdateEntity(cfg);
}

void run_mqtt_and_bridge_tests() {
    RUN_TEST(test_mqtt_client_lifecycle);
    RUN_TEST(test_mqtt_bridge_queue_bounds);
    RUN_TEST(test_ha_discovery_light_payload);
}