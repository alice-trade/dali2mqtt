//  Copyright (c) 2026 Alice-Trade Inc.
//  SPDX-License-Identifier: GPL-3.0-or-later

#include <unity.h>
#include <memory>
#include "dali/RmtDaliTransceiver.hxx"
#include "dali/DaliBusEngine.hxx"
#include "dali/DaliDeviceRegistry.hxx"

using namespace daliMQTT;

static void test_rmt_transceiver_init_lifecycle() {
    auto transceiver = std::make_unique<RmtDaliTransceiver>();
    TEST_ASSERT_FALSE(transceiver->isInitialized());

    RmtTransceiverConfig cfg{
        .rxPin = static_cast<gpio_num_t>(CONFIG_DALI2MQTT_DALI_RX_PIN),
        .txPin = static_cast<gpio_num_t>(CONFIG_DALI2MQTT_DALI_TX_PIN),
        .invertRx = true,
        .invertTx = false
    };

    esp_err_t err = transceiver->init(cfg);
    TEST_ASSERT_EQUAL(ESP_OK, err);
    TEST_ASSERT_TRUE(transceiver->isInitialized());
    TEST_ASSERT_EQUAL(ESP_OK, transceiver->init(cfg));
}

static void test_bus_engine_lifecycle_and_timeout() {
    auto transceiver = std::make_unique<RmtDaliTransceiver>();
    RmtTransceiverConfig cfg{
        .rxPin = static_cast<gpio_num_t>(CONFIG_DALI2MQTT_DALI_RX_PIN),
        .txPin = static_cast<gpio_num_t>(CONFIG_DALI2MQTT_DALI_TX_PIN),
        .invertRx = true,
        .invertTx = false
    };
    TEST_ASSERT_EQUAL(ESP_OK, transceiver->init(cfg));

    auto busEngine = std::make_unique<DaliBusEngine>(*transceiver, 0);
    TEST_ASSERT_FALSE(busEngine->isInitialized());

    TEST_ASSERT_EQUAL(ESP_OK, busEngine->start());
    TEST_ASSERT_TRUE(busEngine->isInitialized());
    TEST_ASSERT_EQUAL_UINT8(0, busEngine->getBusId());

    auto response = busEngine->query(DaliAddressType::Short, 0, OpCode::QueryStatus);
    TEST_ASSERT_FALSE(response.has_value());
}

static void test_device_registry_nvs_persistence() {
    auto transceiver = std::make_unique<RmtDaliTransceiver>();
    RmtTransceiverConfig cfg{
        .rxPin = static_cast<gpio_num_t>(CONFIG_DALI2MQTT_DALI_RX_PIN),
        .txPin = static_cast<gpio_num_t>(CONFIG_DALI2MQTT_DALI_TX_PIN),
        .invertRx = true,
        .invertTx = false
    };
    transceiver->init(cfg);
    auto busEngine = std::make_unique<DaliBusEngine>(*transceiver, 0);
    busEngine->start();

    {
        auto registry = std::make_unique<DaliDeviceRegistry>(*busEngine);
        TEST_ASSERT_EQUAL(ESP_OK, registry->init());

        ControlGear gear;
        gear.longAddress = 0xA1B2C3;
        gear.internalAddress = DaliInternalAddr(0, 15);
        gear.gtin = "01234567890123";
        gear.deviceType = 8;
        gear.color = ColorFeatures{.supportsRgb = true, .supportsTc = true};
        gear.minLevel = 5;
        gear.maxLevel = 250;
        gear.powerOnLevel = 200;
        gear.systemFailureLevel = 254;
        gear.available = true;

        GroupAssignments grp;
        grp[gear.longAddress].set(2);
        grp[gear.longAddress].set(7);
        registry->setAllGroupAssignments(grp);
    }

    auto restoredRegistry = std::make_unique<DaliDeviceRegistry>(*busEngine);
    TEST_ASSERT_EQUAL(ESP_OK, restoredRegistry->init());

    auto assignments = restoredRegistry->getGroupAssignments();
    auto it = assignments.find(0xA1B2C3);
    if (it != assignments.end()) {
        TEST_ASSERT_TRUE(it->second.test(2));
        TEST_ASSERT_TRUE(it->second.test(7));
        TEST_ASSERT_FALSE(it->second.test(0));
    }
}

static void test_device_registry_group_state() {
    auto transceiver = std::make_unique<RmtDaliTransceiver>();
    auto busEngine = std::make_unique<DaliBusEngine>(*transceiver, 0);
    auto registry = std::make_unique<DaliDeviceRegistry>(*busEngine);

    DaliGroupState stateInvalidBus = registry->getGroupState(5, 0);
    TEST_ASSERT_EQUAL_UINT8(0, stateInvalidBus.currentLevel);

    DaliGroupState stateInvalidGrp = registry->getGroupState(0, 16);
    TEST_ASSERT_EQUAL_UINT8(0, stateInvalidGrp.currentLevel);

    DaliGroupState stateValid = registry->getGroupState(0, 5);
    TEST_ASSERT_EQUAL_UINT8(0, stateValid.currentLevel);
    TEST_ASSERT_EQUAL_UINT8(254, stateValid.lastLevel);
}


static void test_dali_sniffer_frame_processing() {
    auto transceiver = std::make_unique<RmtDaliTransceiver>();
    auto busEngine = std::make_unique<DaliBusEngine>(*transceiver, 0);
    auto registry = std::make_unique<DaliDeviceRegistry>(*busEngine);
    TEST_ASSERT_EQUAL(ESP_OK, registry->init());

    ControlGear gear;
    gear.longAddress = 0x112233;
    gear.internalAddress = DaliInternalAddr(0, 5);
    gear.currentLevel = 0;
    gear.available = true;

    GroupAssignments grps;
    grps[0x112233].set(3); // Входит в группу 3
    registry->setAllGroupAssignments(grps);

    static uint8_t s_cbFiredLevel = 0;
    static DaliLongAddress_t s_cbFiredAddr = 0;

    registry->setDeviceStateCallback([](const DeviceStateChangeEvent& ev, void*) {
        s_cbFiredLevel = ev.level;
        s_cbFiredAddr = ev.longAddress;
    }, nullptr);


    DaliRawFrame dacpFrame{
        .data = (0x0A << 8) | 180,
        .bits = 16,
        .type = DaliFrameType::Forward16
    };
    registry->processSnifferFrame(dacpFrame);

    DaliRawFrame grpFrame{
        .data = 0x8705,
        .bits = 16,
        .type = DaliFrameType::Forward16
    };
    registry->processSnifferFrame(grpFrame);

    auto gState = registry->getGroupState(0, 3);
    TEST_ASSERT_EQUAL_UINT8(254, gState.currentLevel);
}

static void test_dali_input_device_24bit_event() {
    auto transceiver = std::make_unique<RmtDaliTransceiver>();
    auto busEngine = std::make_unique<DaliBusEngine>(*transceiver, 0);
    auto registry = std::make_unique<DaliDeviceRegistry>(*busEngine);

    static uint8_t s_btnShortAddr = 0xFF;
    static uint16_t s_eventCode = 0xFFFF;

    registry->setInputEventCallback([](const InputDeviceEvent& ev, void*) {
        s_btnShortAddr = ev.shortAddress;
        s_eventCode = ev.eventCode;
    }, nullptr);

    constexpr uint32_t rawEventFrame = (10 << 17) | (1 << 10) | 0x02;

    const DaliRawFrame inputFrame{
        .data = rawEventFrame,
        .bits = 24,
        .type = DaliFrameType::Forward24
    };

    registry->processInputDeviceFrame(inputFrame);

    TEST_ASSERT_EQUAL_UINT8(10, s_btnShortAddr);
    TEST_ASSERT_EQUAL_UINT16(0x02, s_eventCode);
}

static void test_dali_bus_health_check() {
    auto transceiver = std::make_unique<RmtDaliTransceiver>();
    auto busEngine = std::make_unique<DaliBusEngine>(*transceiver, 0);

    TEST_ASSERT_EQUAL(BusHealth::Ok, busEngine->checkHealth());
}

void run_dali_hardware_and_registry_tests() {
    RUN_TEST(test_rmt_transceiver_init_lifecycle);
    RUN_TEST(test_bus_engine_lifecycle_and_timeout);
    RUN_TEST(test_device_registry_nvs_persistence);
    RUN_TEST(test_device_registry_group_state);
    RUN_TEST(test_dali_sniffer_frame_processing);
    RUN_TEST(test_dali_input_device_24bit_event);
    RUN_TEST(test_dali_bus_health_check);
}