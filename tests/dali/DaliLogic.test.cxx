// Copyright (c) 2026 Alice-Trade Inc.
// SPDX-License-Identifier: GPL-2.0-or-later

#include "unity.h"
#include "dali/DaliAdapter.hxx"
#include "utils/DaliLongAddrConversions.hxx"
#include "dali/DaliDeviceController.hxx"

using namespace daliMQTT;

static void test_long_addr_conversion() {
    DaliLongAddress_t addr = 0xAABBCC;
    auto strArr = utils::longAddressToString(addr);
    TEST_ASSERT_EQUAL_STRING("AABBCC", strArr.data());

    addr = 0x000001;
    strArr = utils::longAddressToString(addr);
    TEST_ASSERT_EQUAL_STRING("000001", strArr.data());

    addr = 0xFFFFFF;
    strArr = utils::longAddressToString(addr);
    TEST_ASSERT_EQUAL_STRING("FFFFFF", strArr.data());
}

static void test_string_to_long_addr() {
    auto opt = utils::stringToLongAddress("AABBCC");
    TEST_ASSERT_TRUE(opt.has_value());
    TEST_ASSERT_EQUAL_HEX32(0xAABBCC, opt.value());

    opt = utils::stringToLongAddress("invalid");
    TEST_ASSERT_FALSE(opt.has_value());
}

static void test_dali_driver_init() {
    gpio_num_t rx = static_cast<gpio_num_t>(CONFIG_DALI2MQTT_DALI_RX_PIN);
    gpio_num_t tx = static_cast<gpio_num_t>(CONFIG_DALI2MQTT_DALI_TX_PIN);

    esp_err_t err = DaliAdapter::Instance().init(rx, tx);
    TEST_ASSERT_EQUAL(ESP_OK, err);
    TEST_ASSERT_TRUE(DaliAdapter::Instance().isInitialized());
}

static void test_dali_controller_singleton() {
    auto& ctrl = DaliDeviceController::Instance();
    ctrl.init();
    auto devices = ctrl.getDevices();
    (void)devices;
}

void run_dali_logic_tests() {
    RUN_TEST(test_long_addr_conversion);
    RUN_TEST(test_string_to_long_addr);
    RUN_TEST(test_dali_driver_init);
    RUN_TEST(test_dali_controller_singleton);
}