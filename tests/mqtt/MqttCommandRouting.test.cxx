// Copyright (c) 2026 Alice-Trade Inc.
// SPDX-License-Identifier: GPL-3.0-or-later

#include <unity.h>
#include <ArduinoJson.h>
#include <algorithm>
#include <charconv>
#include <string_view>

static void test_mqtt_brightness_clamping() {
    auto parseBrightness = [](const char* json) -> std::optional<uint8_t> {
        JsonDocument doc;
        if (deserializeJson(doc, json) != DeserializationError::Ok) return std::nullopt;
        if (!doc["brightness"].is<int>()) return std::nullopt;
        return static_cast<uint8_t>(std::clamp(doc["brightness"].as<int>(), 0, 254));
    };

    TEST_ASSERT_EQUAL_UINT8(0, parseBrightness(R"({"brightness": 0})").value());
    TEST_ASSERT_EQUAL_UINT8(128, parseBrightness(R"({"brightness": 128})").value());
    TEST_ASSERT_EQUAL_UINT8(254, parseBrightness(R"({"brightness": 254})").value());
    TEST_ASSERT_EQUAL_UINT8(254, parseBrightness(R"({"brightness": 255})").value());
    TEST_ASSERT_EQUAL_UINT8(254, parseBrightness(R"({"brightness": 1000})").value());
    TEST_ASSERT_EQUAL_UINT8(0, parseBrightness(R"({"brightness": -50})").value());
    TEST_ASSERT_FALSE(parseBrightness(R"({"state": "ON"})").has_value());
}

static void test_mqtt_scene_command_parsing() {
    auto parseScene = [](std::string_view payload) -> std::optional<uint8_t> {
        if (payload.starts_with("Scene ")) {
            auto scenePart = payload.substr(6);
            uint8_t sceneId = 0;
            auto [ptr, ec] = std::from_chars(scenePart.data(), scenePart.data() + scenePart.size(), sceneId);
            if (ec == std::errc{} && sceneId < 16) return sceneId;
            return std::nullopt;
        }

        JsonDocument doc;
        if (deserializeJson(doc, payload.data(), payload.size()) == DeserializationError::Ok) {
            if (doc["scene"].is<int>()) {
                int sc = doc["scene"].as<int>();
                if (sc >= 0 && sc < 16) return static_cast<uint8_t>(sc);
            }
        }
        return std::nullopt;
    };

    TEST_ASSERT_EQUAL_UINT8(0, parseScene("Scene 0").value());
    TEST_ASSERT_EQUAL_UINT8(7, parseScene("Scene 7").value());
    TEST_ASSERT_EQUAL_UINT8(15, parseScene("Scene 15").value());
    TEST_ASSERT_FALSE(parseScene("Scene 16").has_value()); // За пределами 0..15

    TEST_ASSERT_EQUAL_UINT8(5, parseScene(R"({"scene": 5})").value());
    TEST_ASSERT_FALSE(parseScene(R"({"scene": 20})").has_value());
    TEST_ASSERT_FALSE(parseScene(R"({"invalid": true})").has_value());
}

static void test_mqtt_group_config_payload_parsing() {
    const char* validJson = R"({
        "long_address": "0A1B2C",
        "group": 3,
        "state": "add"
    })";

    JsonDocument doc;
    TEST_ASSERT_TRUE(deserializeJson(doc, validJson) == DeserializationError::Ok);

    TEST_ASSERT_TRUE(doc["long_address"].is<const char*>());
    TEST_ASSERT_TRUE(doc["group"].is<int>());
    TEST_ASSERT_TRUE(doc["state"].is<const char*>());

    TEST_ASSERT_EQUAL_STRING("0A1B2C", doc["long_address"].as<const char*>());
    TEST_ASSERT_EQUAL_INT(3, doc["group"].as<int>());
    TEST_ASSERT_EQUAL_STRING("add", doc["state"].as<const char*>());

    bool isAdd = (strcmp(doc["state"].as<const char*>(), "add") == 0);
    TEST_ASSERT_TRUE(isAdd);
}

void run_mqtt_command_routing_tests() {
    RUN_TEST(test_mqtt_brightness_clamping);
    RUN_TEST(test_mqtt_scene_command_parsing);
    RUN_TEST(test_mqtt_group_config_payload_parsing);
}