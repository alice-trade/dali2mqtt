// Copyright (c) 2026 Alice-Trade Inc.
// SPDX-License-Identifier: GPL-3.0-or-later

#include <catch2/catch_test_macros.hpp>
#include "dali/DaliAddress.hxx"
#include "dali/DaliOpCodes.hxx"
#include "dali/DaliSpecialOpCodes.hxx"

using namespace daliMQTT;

constexpr uint8_t makeAddr(DaliAddressType type, uint8_t addr, bool isCmd) noexcept {
    uint8_t byte = 0;
    switch (type) {
        case DaliAddressType::Short:     byte = static_cast<uint8_t>((addr & 0x3F) << 1); break;
        case DaliAddressType::Group:     byte = static_cast<uint8_t>(0x80 | ((addr & 0x0F) << 1)); break;
        case DaliAddressType::Broadcast: byte = 0xFE; break;
        case DaliAddressType::Special:   byte = static_cast<uint8_t>(0xA0 | (addr & 0x1F)); break;
    }
    if (isCmd) byte |= 0x01;
    return byte;
}

constexpr uint16_t makeFrame16(uint8_t addrByte, uint8_t opcodeByte) noexcept {
    return (static_cast<uint16_t>(addrByte) << 8) | opcodeByte;
}

TEST_CASE("DALI Address Byte Encoding", "[dali][protocol]") {
    SECTION("Short Addresses (DACP vs Command)") {
        CHECK(makeAddr(DaliAddressType::Short, 0, false) == 0x00);
        CHECK(makeAddr(DaliAddressType::Short, 0, true) == 0x01);

        CHECK(makeAddr(DaliAddressType::Short, 5, false) == 0x0A);
        CHECK(makeAddr(DaliAddressType::Short, 5, true) == 0x0B);

        CHECK(makeAddr(DaliAddressType::Short, 63, false) == 0x7E);
        CHECK(makeAddr(DaliAddressType::Short, 63, true) == 0x7F);
    }

    SECTION("Group Addresses") {
        CHECK(makeAddr(DaliAddressType::Group, 0, false) == 0x80);
        CHECK(makeAddr(DaliAddressType::Group, 0, true) == 0x81);

        CHECK(makeAddr(DaliAddressType::Group, 15, false) == 0x9E);
        CHECK(makeAddr(DaliAddressType::Group, 15, true) == 0x9F);
    }

    SECTION("Broadcast Addresses") {
        CHECK(makeAddr(DaliAddressType::Broadcast, 0, false) == 0xFE);
        CHECK(makeAddr(DaliAddressType::Broadcast, 0, true) == 0xFF);
    }
}

TEST_CASE("DALI 16-bit Frame Assembly", "[dali][protocol]") {
    SECTION("DACP Frames") {
        CHECK(makeFrame16(makeAddr(DaliAddressType::Short, 0, false), 254) == 0x00FE);

        CHECK(makeFrame16(makeAddr(DaliAddressType::Group, 1, false), 128) == 0x8280);

        CHECK(makeFrame16(makeAddr(DaliAddressType::Broadcast, 0, false), 0) == 0xFE00);
    }

    SECTION("Standard Command Frames") {
        CHECK(makeFrame16(makeAddr(DaliAddressType::Short, 5, true), static_cast<uint8_t>(OpCode::Off)) == 0x0B00);

        CHECK(makeFrame16(makeAddr(DaliAddressType::Short, 5, true), static_cast<uint8_t>(OpCode::RecallMaxLevel)) == 0x0B05);

        CHECK(makeFrame16(makeAddr(DaliAddressType::Broadcast, 0, true), static_cast<uint8_t>(OpCode::StepUp)) == 0xFF03);
    }
}