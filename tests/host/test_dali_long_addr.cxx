// Copyright (c) 2026 Alice-Trade Inc.
// SPDX-License-Identifier: GPL-3.0-or-later

#include <catch2/catch_test_macros.hpp>
#include "utils/DaliLongAddrConversions.hxx"

using namespace daliMQTT::utils;

TEST_CASE("DALI Long Address to String Conversion", "[utils][dali]") {
    SECTION("Standard 24-bit Addresses") {
        CHECK(std::string_view(longAddressToString(0xAABBCC).data()) == "AABBCC");
        CHECK(std::string_view(longAddressToString(0x123456).data()) == "123456");
        CHECK(std::string_view(longAddressToString(0x000001).data()) == "000001");
        CHECK(std::string_view(longAddressToString(0xFFFFFF).data()) == "FFFFFF");
        CHECK(std::string_view(longAddressToString(0x000000).data()) == "000000");
    }

    SECTION("Zero Padding for Small Numbers") {
        CHECK(std::string_view(longAddressToString(0x05).data()) == "000005");
        CHECK(std::string_view(longAddressToString(0xFF).data()) == "0000FF");
        CHECK(std::string_view(longAddressToString(0x1ABC).data()) == "001ABC");
    }

    SECTION("Masking Higher 8 bits") {
        CHECK(std::string_view(longAddressToString(0xFFAABBCC).data()) == "AABBCC");
    }
}

TEST_CASE("String to DALI Long Address Parsing", "[utils][dali]") {
    SECTION("Valid Uppercase and Lowercase Hex") {
        auto addr1 = stringToLongAddress("AABBCC");
        REQUIRE(addr1.has_value());
        CHECK(*addr1 == 0xAABBCC);

        auto addr2 = stringToLongAddress("aabbcc");
        REQUIRE(addr2.has_value());
        CHECK(*addr2 == 0xAABBCC);

        auto addr3 = stringToLongAddress("000001");
        REQUIRE(addr3.has_value());
        CHECK(*addr3 == 0x000001);
    }

    SECTION("Invalid Strings") {
        CHECK_FALSE(stringToLongAddress("").has_value());
        CHECK_FALSE(stringToLongAddress("AABBCCD").has_value());
        CHECK_FALSE(stringToLongAddress("GHIJKL").has_value());
        CHECK_FALSE(stringToLongAddress("AABB-C").has_value());
    }

    SECTION("Round-Trip Conversion Consistency") {
        const uint32_t testAddresses[] = {0x000000, 0x000001, 0x0000FF, 0x123456, 0xAABBCC, 0xFFFFFF};
        for (uint32_t original : testAddresses) {
            auto str = longAddressToString(original);
            auto parsed = stringToLongAddress(str.data());
            REQUIRE(parsed.has_value());
            CHECK(*parsed == original);
        }
    }
}