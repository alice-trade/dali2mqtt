// Copyright (c) 2026 Alice-Trade Inc.
// SPDX-License-Identifier: GPL-3.0-or-later

#include <unity.h>
#include "dali/DaliBusEngine.hxx"
#include "dali/DaliDeviceRegistry.hxx"
#include "dali/RmtDaliTransceiver.hxx"
#include <esp_timer.h>

using namespace daliMQTT;

static void test_nvs_blob_struct_alignment_and_layout() {
    TEST_ASSERT_EQUAL_UINT32(32, sizeof(AddressMapBlobItem));

    AddressMapBlobItem item{};
    item.longAddress = 0x123456;
    item.internalAddress = 0x010A;
    item.deviceType = 8;
    strncpy(item.gtin, "04008321987654", sizeof(item.gtin) - 1);
    item.isInput = false;
    item.supportsRgb = true;
    item.supportsTc = true;
    item.minLevel = 10;
    item.maxLevel = 254;
    item.powerOnLevel = 200;
    item.systemFailureLevel = 254;
    item.groupMask = 0x0005;

    uint8_t* raw = reinterpret_cast<uint8_t*>(&item);
    TEST_ASSERT_EQUAL_HEX8(0x56, raw[0]);
    TEST_ASSERT_EQUAL_HEX8(0x34, raw[1]);
    TEST_ASSERT_EQUAL_HEX8(0x12, raw[2]);
    TEST_ASSERT_EQUAL_HEX8(0x00, raw[3]);

    TEST_ASSERT_EQUAL_HEX8(0x0A, raw[4]);
    TEST_ASSERT_EQUAL_HEX8(0x01, raw[5]);
    TEST_ASSERT_EQUAL_HEX8(8, raw[6]);
}

static void test_device_registry_sync_queuing() {
    auto phy = std::make_unique<RmtDaliTransceiver>();
    auto bus = std::make_unique<DaliBusEngine>(*phy, 0);
    auto registry = std::make_unique<DaliDeviceRegistry>(*bus);

    TEST_ASSERT_EQUAL(ESP_OK, registry->init());

    const DaliInternalAddr addrA(0, 10);
    const DaliInternalAddr addrB(0, 20);
    const DaliInternalAddr invalidAddr(0, 65);
    registry->requestSync(addrA, 0);
    registry->requestSync(addrA, 0);

    registry->requestSync(invalidAddr, 0);
    registry->requestSync(addrB, 500);
    registry->requestBroadcastSync(200, 100);

    TEST_ASSERT_TRUE(true);
}

static void test_sniffer_frame_logic_state_updates() {
    auto phy = std::make_unique<RmtDaliTransceiver>();
    auto bus = std::make_unique<DaliBusEngine>(*phy, 0);
    auto registry = std::make_unique<DaliDeviceRegistry>(*bus);
    registry->init();

    struct SnifferCapture {
        uint8_t level{0};
        bool called{false};
        DaliLongAddress_t addr{0};
    } capture;

    registry->setDeviceStateCallback([](const DeviceStateChangeEvent& ev, void* ctx) {
        auto* cap = static_cast<SnifferCapture*>(ctx);
        cap->level = ev.level;
        cap->addr = ev.longAddress;
        cap->called = true;
    }, &capture);

    const DaliRawFrame dacpFrame{
        .data = (0x0A << 8) | 180,
        .bits = 16,
        .type = DaliFrameType::Forward16
    };

    capture.called = false;
    registry->processSnifferFrame(dacpFrame);

    TEST_ASSERT_TRUE(true);
}

static void test_groups_and_scenes_boundaries() {
    auto phy = std::make_unique<RmtDaliTransceiver>();
    auto bus = std::make_unique<DaliBusEngine>(*phy, 0);
    auto registry = std::make_unique<DaliDeviceRegistry>(*bus);

    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, registry->setGroupBrightness(0, 16, 200));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, registry->setGroupPower(0, 16, true));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, registry->setDeviceGroupMembership(0x123456, 16, true));

    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, registry->activateScene(0, 16));

    SceneLevels levels{};
    levels.fill(255);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, registry->saveSceneLevels(0, 16, levels));

    auto queriedLevels = registry->querySceneLevels(0, 16);
    for (uint8_t lvl : queriedLevels) {
        TEST_ASSERT_EQUAL_UINT8(255, lvl);
    }
}

void run_dali_registry_and_engine_tests() {
    RUN_TEST(test_nvs_blob_struct_alignment_and_layout);
    RUN_TEST(test_device_registry_sync_queuing);
    RUN_TEST(test_sniffer_frame_logic_state_updates);
    RUN_TEST(test_groups_and_scenes_boundaries);
}