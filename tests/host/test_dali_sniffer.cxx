// host/test_dali_sniffer.cxx
// Copyright (c) 2026 Alice-Trade Inc.
// SPDX-License-Identifier: GPL-3.0-or-later

#include <catch2/catch_test_macros.hpp>
#include "dali/DaliBusEngine.hxx"
#include "dali/DaliDeviceRegistry.hxx"
#include "dali/RmtDaliTransceiver.hxx"
#include "utils/NvsHandle.hxx"

namespace HostTestUtils { void resetNvs(); }

using namespace daliMQTT;

namespace {
struct __attribute__((packed)) TestNvsBlobItem {
    DaliLongAddress_t longAddress{0};
    uint16_t internalAddress{0};
    uint8_t deviceType{0xFF};
    char gtin[16]{0};
    bool isInput{false};
    bool supportsRgb{false};
    bool supportsTc{false};
    uint8_t minLevel{1};
    uint8_t maxLevel{254};
    uint8_t powerOnLevel{254};
    uint8_t systemFailureLevel{254};
    uint8_t _padding[2]{0, 0};
};
}

TEST_CASE("DALI Sniffer Frame Parsing & State Synchronization", "[dali][sniffer][host]") {
    HostTestUtils::resetNvs();

    {
        NvsHandle nvs("dali_reg", NVS_READWRITE);
        std::vector<TestNvsBlobItem> items(2);

        items[0].longAddress = 0x000010;
        items[0].internalAddress = DaliInternalAddr(0, 10).value;
        items[0].minLevel = 1;
        items[0].maxLevel = 254;

        items[1].longAddress = 0x000011;
        items[1].internalAddress = DaliInternalAddr(0, 11).value;
        items[1].minLevel = 1;
        items[1].maxLevel = 254;

        nvs_set_blob(nvs.get(), "addr_map", items.data(), items.size() * sizeof(TestNvsBlobItem));
        nvs_commit(nvs.get());
    }

    RmtDaliTransceiver transceiver;
    DaliBusEngine bus(transceiver, 0);
    DaliDeviceRegistry registry(bus);

    REQUIRE(registry.init() == ESP_OK);
    CHECK(registry.getDevicesSnapshot().size() == 2);

    GroupAssignments groups;
    groups[0x000010].set(3);
    registry.setAllGroupAssignments(groups);

    struct DeviceEvent { DaliLongAddress_t addr; uint8_t level; };
    std::vector<DeviceEvent> devEvents;
    registry.setDeviceStateCallback([](const DeviceStateChangeEvent& ev, void* ctx) {
        static_cast<std::vector<DeviceEvent>*>(ctx)->push_back({ev.longAddress, ev.level});
    }, &devEvents);

    struct GroupEvent { uint8_t group; uint8_t level; };
    std::vector<GroupEvent> grpEvents;
    registry.setGroupStateCallback([](const GroupStateChangeEvent& ev, void* ctx) {
        static_cast<std::vector<GroupEvent>*>(ctx)->push_back({ev.groupId, ev.level});
    }, &grpEvents);

    SECTION("Direct Arc Power (DACP) to Single Device (SA 10 -> 180)") {
        DaliRawFrame frame{
            .data = (0x14 << 8) | 180,
            .bits = 16,
            .type = DaliFrameType::Forward16
        };

        registry.processSnifferFrame(frame);

        REQUIRE(devEvents.size() == 1);
        CHECK(devEvents[0].addr == 0x000010);
        CHECK(devEvents[0].level == 180);
    }

    SECTION("24-bit Input Device Event (Button press on SA 5)") {
        struct InputEventRecord { uint8_t sa; uint8_t code; };
        std::vector<InputEventRecord> inputEvents;
        registry.setInputEventCallback([](const InputDeviceEvent& ev, void* ctx) {
            static_cast<std::vector<InputEventRecord>*>(ctx)->push_back({ev.shortAddress, ev.eventCode});
        }, &inputEvents);

        DaliRawFrame frame{
            .data = (0x0A << 16) | (0x01 << 8) | 0x02,
            .bits = 24,
            .type = DaliFrameType::Forward24
        };

        registry.processInputDeviceFrame(frame);

        REQUIRE(inputEvents.size() == 1);
        CHECK(inputEvents[0].sa == 5);
        CHECK(inputEvents[0].code == 0x02);
    }

    SECTION("Group 3 Command RecallMax affects Group and member Device") {
        DaliRawFrame frame{
            .data = 0x8705,
            .bits = 16,
            .type = DaliFrameType::Forward16
        };

        registry.processSnifferFrame(frame);

        REQUIRE(grpEvents.size() == 1);
        CHECK(grpEvents[0].group == 3);
        CHECK(grpEvents[0].level == 254);
        CHECK(registry.getGroupState(0, 3).currentLevel == 254);

        REQUIRE(devEvents.size() == 1);
        CHECK(devEvents[0].addr == 0x000010);
        CHECK(devEvents[0].level == 254);
    }

    SECTION("Broadcast Off turns OFF all devices") {
        DaliRawFrame frame{
            .data = 0xFF00,
            .bits = 16,
            .type = DaliFrameType::Forward16
        };

        registry.processSnifferFrame(frame);

        REQUIRE(devEvents.size() == 2);
        CHECK(devEvents[0].level == 0);
        CHECK(devEvents[1].level == 0);
    }
}