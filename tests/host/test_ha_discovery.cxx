//  Copyright (c) 2026 Alice-Trade Inc.
//  SPDX-License-Identifier: GPL-3.0-or-later

// tests/host/test_ha_discovery.cxx
#include <catch2/catch_test_macros.hpp>
#include <ArduinoJson.h>
#include "system/ConfigStructure.hxx"
#include "system/ConfigDefaults.icc"
#include "mqtt/HomeAssistantDiscovery.hxx"
#include "mqtt/MqttClient.hxx"
#include "dali/DaliBusEngine.hxx"
#include "dali/DaliDeviceRegistry.hxx"

namespace HostTestUtils {
    struct MqttMsg { std::string topic; std::string payload; int qos; bool retain; };
    std::vector<MqttMsg>& getMqttMessages();
    void clearMqttMessages();
}

using namespace daliMQTT;

TEST_CASE("Home Assistant Discovery Payload Validation", "[mqtt][ha][host]") {
    HostTestUtils::clearMqttMessages();

    ConfigStructure cfg = makeDefaultConfig();
    cfg.mqttUri = "mqtt://127.0.0.1:1883";
    cfg.clientId = "dali_bridge_01";
    cfg.mqttBaseTopic = "dali_home";
    cfg.hassDiscoveryEnabled = true;

    MqttClient mqtt;
    mqtt.init(cfg);
    mqtt.connect();

    RmtDaliTransceiver transceiver;
    DaliBusEngine bus(transceiver, 0);
    DaliDeviceRegistry registry(bus);
    HomeAssistantDiscovery discovery(mqtt, registry);

    SECTION("Light entity with Tunable White (DT8)") {
        ControlGear gear;
        gear.longAddress = 0x123456;
        gear.internalAddress = DaliInternalAddr(0, 5);
        gear.color = ColorFeatures{
            .minMireds = 153,
            .maxMireds = 500,
            .supportsRgb = false,
            .supportsTc = true
        };

        discovery.publishLight(gear, cfg);

        auto& msgs = HostTestUtils::getMqttMessages();
        REQUIRE(msgs.size() == 2);

        CHECK(msgs[0].topic == "homeassistant/light/dali_dali_bridge_01_b0_la_123456/config");
        CHECK(msgs[0].retain == true);

        JsonDocument doc;
        DeserializationError err = deserializeJson(doc, msgs[0].payload);
        REQUIRE(err == DeserializationError::Ok);

        CHECK(doc["schema"] == "json");
        CHECK(doc["brightness"] == true);
        CHECK(doc["state_topic"] == "dali_home/light/123456/state");
        CHECK(doc["command_topic"] == "dali_home/light/123456/set");

        JsonArray modes = doc["supported_color_modes"];
        REQUIRE(modes.size() == 1);
        CHECK(modes[0] == "color_temp");
        CHECK(doc["min_mireds"] == 153);
        CHECK(doc["max_mireds"] == 500);
    }
}