// Copyright (c) 2026 Alice-Trade Inc.
// SPDX-License-Identifier: GPL-3.0-or-later

#include <unity.h>
#include "dali/DaliBusEngine.hxx"
#include "dali/DaliDeviceRegistry.hxx"
#include "dali/DaliFrame.hxx"
#include "dali/DaliInputEvent.hxx"
#include "dali/RmtDaliTransceiver.hxx"
#include <memory>

using namespace daliMQTT;

static InputDeviceEvent s_lastEvent{};
static bool s_eventFired = false;

static void testInputCallback(const InputDeviceEvent& ev, void*) {
    s_lastEvent = ev;
    s_eventFired = true;
}

static std::unique_ptr<RmtDaliTransceiver> s_phy;
static std::unique_ptr<DaliBusEngine> s_bus;
static std::unique_ptr<DaliDeviceRegistry> s_registry;

static void setup_input_registry() {
    s_phy = std::make_unique<RmtDaliTransceiver>();
    s_bus = std::make_unique<DaliBusEngine>(*s_phy, 0);
    s_registry = std::make_unique<DaliDeviceRegistry>(*s_bus);
    s_registry->init();
    s_registry->setInputEventCallback(testInputCallback, nullptr);
    s_eventFired = false;
    s_lastEvent = {};
}

static void test_input_frame_ignore_commands() {
    setup_input_registry();

    const uint32_t cmdFrame = (0x01UL << 16) | 0x1234;
    const DaliRawFrame rawFrame{.data = cmdFrame, .bits = 24, .type = DaliFrameType::Forward24};

    s_registry->processInputDeviceFrame(rawFrame);
    TEST_ASSERT_FALSE(s_eventFired);
}

static void test_input_frame_short_addr_type_scheme() {
    setup_input_registry();


    const uint32_t raw = (15UL << 17) | (4UL << 10) | 0x0123;
    const DaliRawFrame rawFrame{.data = raw, .bits = 24, .type = DaliFrameType::Forward24};

    s_registry->processInputDeviceFrame(rawFrame);

    TEST_ASSERT_TRUE(s_eventFired);
    TEST_ASSERT_TRUE(s_lastEvent.addressType == InputAddressType::Short);
    TEST_ASSERT_EQUAL_UINT8(15, s_lastEvent.shortAddress);
    TEST_ASSERT_EQUAL_UINT8(4, s_lastEvent.instanceType);
    TEST_ASSERT_EQUAL_UINT8(0, s_lastEvent.instanceNumber);
    TEST_ASSERT_EQUAL_UINT16(0x0123, s_lastEvent.eventCode);
}

static void test_input_frame_short_addr_number_scheme() {
    setup_input_registry();

    const uint32_t raw = (8UL << 17) | (1UL << 15) | (2UL << 10) | 0x0055;
    const DaliRawFrame rawFrame{.data = raw, .bits = 24, .type = DaliFrameType::Forward24};

    s_registry->processInputDeviceFrame(rawFrame);

    TEST_ASSERT_TRUE(s_eventFired);
    TEST_ASSERT_TRUE(s_lastEvent.addressType == InputAddressType::Short);
    TEST_ASSERT_EQUAL_UINT8(8, s_lastEvent.shortAddress);
    TEST_ASSERT_EQUAL_UINT8(0, s_lastEvent.instanceType);
    TEST_ASSERT_EQUAL_UINT8(2, s_lastEvent.instanceNumber);
    TEST_ASSERT_EQUAL_UINT16(0x0055, s_lastEvent.eventCode);
}

static void test_input_frame_group_scheme() {
    setup_input_registry();

    const uint32_t raw = (1UL << 23) | (5UL << 17) | (3UL << 10) | 0x0001;
    const DaliRawFrame rawFrame{.data = raw, .bits = 24, .type = DaliFrameType::Forward24};

    s_registry->processInputDeviceFrame(rawFrame);

    TEST_ASSERT_TRUE(s_eventFired);
    TEST_ASSERT_TRUE(s_lastEvent.addressType == InputAddressType::Group);
    TEST_ASSERT_EQUAL_UINT8(5, s_lastEvent.shortAddress);
    TEST_ASSERT_EQUAL_UINT8(3, s_lastEvent.instanceType);
}

static void test_input_frame_broadcast_scheme() {
    setup_input_registry();

    const uint32_t raw = (1UL << 23) | (1UL << 22) | (1UL << 15) | (1UL << 10) | 0x0002;
    const DaliRawFrame rawFrame{.data = raw, .bits = 24, .type = DaliFrameType::Forward24};

    s_registry->processInputDeviceFrame(rawFrame);

    TEST_ASSERT_TRUE(s_eventFired);
    TEST_ASSERT_TRUE(s_lastEvent.addressType == InputAddressType::Broadcast);
    TEST_ASSERT_EQUAL_UINT8(0xFF, s_lastEvent.shortAddress);
    TEST_ASSERT_EQUAL_UINT8(1, s_lastEvent.instanceNumber);
}

void run_dali_input_events_tests() {
    RUN_TEST(test_input_frame_ignore_commands);
    RUN_TEST(test_input_frame_short_addr_type_scheme);
    RUN_TEST(test_input_frame_short_addr_number_scheme);
    RUN_TEST(test_input_frame_group_scheme);
    RUN_TEST(test_input_frame_broadcast_scheme);
}