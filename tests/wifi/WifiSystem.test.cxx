//  Copyright (c) 2026 Alice-Trade Inc.
//  SPDX-License-Identifier: GPL-3.0-or-later

#include "network/NetworkPlatform.hxx"
#include "system/SyslogService.hxx"
#include "system/SystemControls.hxx"
#include "system/SystemScheduler.hxx"
#include <unity.h>

using namespace daliMQTT;

static void test_wifi_init_and_defaults() {
    NetworkPlatform wifi;
    TEST_ASSERT_EQUAL(ESP_OK, wifi.init());
    TEST_ASSERT_EQUAL(NetworkStatus::Disconnected, wifi.getStatus());
    TEST_ASSERT_EQUAL_STRING("0.0.0.0", wifi.getIpAddress().c_str());
}

static void test_system_controls_boot_button_init() {
    SystemControls controls;
    TEST_ASSERT_EQUAL(ESP_OK, controls.init(GPIO_NUM_0));

    bool resetTriggered = false;
    controls.setResetCallback([](void* ctx) {
        *static_cast<bool*>(ctx) = true;
    }, &resetTriggered);

    TEST_ASSERT_FALSE(resetTriggered);
}

static void test_syslog_ringbuffer_lifecycle() {
    SyslogService syslog;
    TEST_ASSERT_EQUAL(ESP_OK, syslog.start("127.0.0.1"));
    syslog.stop();
    TEST_ASSERT_TRUE(true);
}

static void test_system_scheduler_intervals() {
    SystemScheduler scheduler;

    int telemCalls = 0;
    int healthCalls = 0;
    int otaCalls = 0;

    scheduler.setTelemetryCallback([](void* ctx) { (*static_cast<int*>(ctx))++; }, &telemCalls, 60);
    scheduler.setBusHealthCallback([](void* ctx) { (*static_cast<int*>(ctx))++; }, &healthCalls, 300);
    scheduler.setOtaCheckCallback([](void* ctx) { (*static_cast<int*>(ctx))++; }, &otaCalls, 86400);

    scheduler.tick(0);
    TEST_ASSERT_EQUAL_INT(0, telemCalls);

    scheduler.tick(60);
    TEST_ASSERT_EQUAL_INT(1, telemCalls);
    TEST_ASSERT_EQUAL_INT(0, healthCalls);

    scheduler.tick(120);
    TEST_ASSERT_EQUAL_INT(2, telemCalls);

    scheduler.tick(300);
    TEST_ASSERT_EQUAL_INT(3, telemCalls);
    TEST_ASSERT_EQUAL_INT(1, healthCalls);
    TEST_ASSERT_EQUAL_INT(0, otaCalls);

    scheduler.tick(86400);
    TEST_ASSERT_EQUAL_INT(4, telemCalls);
    TEST_ASSERT_EQUAL_INT(2, healthCalls);
    TEST_ASSERT_EQUAL_INT(1, otaCalls);
}

void run_wifi_and_system_tests() {
    RUN_TEST(test_wifi_init_and_defaults);
    RUN_TEST(test_system_controls_boot_button_init);
    RUN_TEST(test_syslog_ringbuffer_lifecycle);
    RUN_TEST(test_system_scheduler_intervals);
}