// Copyright (c) 2026 Alice-Trade Inc.
// SPDX-License-Identifier: GPL-3.0-or-later

#include <catch2/catch_test_macros.hpp>
#include "dali/DaliDevice.hxx"
#include "dali/DaliControlGear.hxx"
#include "dali/DaliInputDevice.hxx"

using namespace daliMQTT;

TEST_CASE("DALI Internal Address Representation", "[domain][address]") {
    DaliInternalAddr addr0(0, 5);
    CHECK(addr0.bus() == 0);
    CHECK(addr0.shortAddr() == 5);
    CHECK(addr0.isAssigned() == true);

    DaliInternalAddr addr1(1, 63);
    CHECK(addr1.bus() == 1);
    CHECK(addr1.shortAddr() == 63);
    CHECK(addr1.isAssigned() == true);

    DaliInternalAddr unassigned(0, 255);
    CHECK(unassigned.isAssigned() == false);
}

TEST_CASE("DaliDevice Variant & Identity Polymorphism", "[domain][device]") {
    SECTION("ControlGear Device") {
        ControlGear gear;
        gear.longAddress = 0xAABBCC;
        gear.internalAddress = DaliInternalAddr(0, 10);
        gear.gtin = "12345678901234";
        gear.currentLevel = 200;
        gear.available = true;

        DaliDevice dev(gear);

        CHECK(getIdentity(dev).longAddress == 0xAABBCC);
        CHECK(getIdentity(dev).internalAddress.bus() == 0);
        CHECK(getIdentity(dev).internalAddress.shortAddr() == 10);
        CHECK(getIdentity(dev).gtin == "12345678901234");
        CHECK(getIdentity(dev).available == true);

        const auto* ptr = etl::get_if<ControlGear>(&dev);
        REQUIRE(ptr != nullptr);
        CHECK(ptr->currentLevel == 200);
    }

    SECTION("InputDevice Device") {
        InputDevice input;
        input.longAddress = 0x112233;
        input.internalAddress = DaliInternalAddr(1, 2);
        input.instanceByte = 1;
        input.available = true;

        DaliDevice dev(input);

        CHECK(getIdentity(dev).longAddress == 0x112233);
        CHECK(getIdentity(dev).internalAddress.bus() == 1);
        CHECK(getIdentity(dev).internalAddress.shortAddr() == 2);
        CHECK(getIdentity(dev).available == true);

        const auto* ptr = etl::get_if<InputDevice>(&dev);
        REQUIRE(ptr != nullptr);
        CHECK(ptr->instanceByte == 1);
    }
}