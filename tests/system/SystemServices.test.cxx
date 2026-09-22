// Copyright (c) 2026 Alice-Trade Inc.
// SPDX-License-Identifier: GPL-3.0-or-later

#include <unity.h>
#include <ArduinoJson.h>
#include "system/SystemScheduler.hxx"
#include "system/SystemControls.hxx"
#include "system/OtaService.hxx"

using namespace daliMQTT;

static void test_system_scheduler_precision() {
    SystemScheduler scheduler;

    int telemCount = 0;
    int busHealthCount = 0;
    int otaCount = 0;

    scheduler.setTelemetryCallback([](void* ctx) { (*static_cast<int*>(ctx))++; }, &telemCount, 60);
    scheduler.setBusHealthCallback([](void* ctx) { (*static_cast<int*>(ctx))++; }, &busHealthCount, 10);
    scheduler.setOtaCheckCallback([](void* ctx) { (*static_cast<int*>(ctx))++; }, &otaCount, 172800);

    scheduler.tick(0);
    TEST_ASSERT_EQUAL_INT(0, telemCount);
    TEST_ASSERT_EQUAL_INT(0, busHealthCount);
    TEST_ASSERT_EQUAL_INT(0, otaCount);

    scheduler.tick(10);
    TEST_ASSERT_EQUAL_INT(0, telemCount);
    TEST_ASSERT_EQUAL_INT(1, busHealthCount);

    scheduler.tick(59);
    TEST_ASSERT_EQUAL_INT(0, telemCount);
    TEST_ASSERT_EQUAL_INT(1, busHealthCount);

    scheduler.tick(60);
    TEST_ASSERT_EQUAL_INT(1, telemCount);
    TEST_ASSERT_EQUAL_INT(2, busHealthCount);

    scheduler.tick(172800);
    TEST_ASSERT_EQUAL_INT(1, otaCount);
}

static void test_ota_manifest_json_parsing() {
    const char* validGithubJson = R"({
        "tag_name": "v0.16.2",
        "html_url": "https://github.com/company/repo/releases/tag/v0.16.2",
        "assets": [
            {
                "name": "firmware.bin",
                "browser_download_url": "https://github.com/company/repo/releases/download/v0.16.2/firmware.bin"
            }
        ]
    })";

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, validGithubJson);
    TEST_ASSERT_TRUE(err == DeserializationError::Ok);

    std::string_view tag = doc["tag_name"].as<const char*>();
    if (tag.starts_with("v") || tag.starts_with("V")) {
        tag.remove_prefix(1);
    }
    TEST_ASSERT_EQUAL_STRING("0.16.2", std::string(tag).c_str());

    const char* currentVer = "0.15.0";
    TEST_ASSERT_TRUE(std::string(tag) != currentVer);

    JsonDocument badDoc;
    DeserializationError badErr = deserializeJson(badDoc, "{ bad_json: ... ");
    TEST_ASSERT_TRUE(badErr != DeserializationError::Ok);
}

static void test_syslog_packet_framing() {
    constexpr char expectedHeader[] = "<14>dalimqtt: ";
    const char* rawLog = "DALI Light 5 Level Changed to 180\n\r";

    size_t len = strlen(rawLog);
    while (len > 0 && (rawLog[len - 1] == '\n' || rawLog[len - 1] == '\r')) {
        len--;
    }

    char packetBuf[128];
    const int headerLen = snprintf(packetBuf, sizeof(packetBuf), "%s", expectedHeader);
    memcpy(packetBuf + headerLen, rawLog, len);
    packetBuf[headerLen + len] = '\0';

    TEST_ASSERT_EQUAL_STRING("<14>dalimqtt: DALI Light 5 Level Changed to 180", packetBuf);
}

void run_system_services_and_ota_tests() {
    RUN_TEST(test_system_scheduler_precision);
    RUN_TEST(test_ota_manifest_json_parsing);
    RUN_TEST(test_syslog_packet_framing);
}