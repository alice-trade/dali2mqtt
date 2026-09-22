// Copyright (c) 2026 Alice-Trade Inc.
// SPDX-License-Identifier: GPL-3.0-or-later

#include <unity.h>
#include "dali/DaliAddress.hxx"
#include "dali/DaliFrame.hxx"
#include "dali/DaliOpCodes.hxx"
#include "utils/DaliLongAddrConversions.hxx"
#include "utils/DaliSensorMath.hxx"
#include <cmath>

using namespace daliMQTT;

static void test_dali_internal_addr_packing() {
    DaliInternalAddr addr0(0, 0);
    TEST_ASSERT_EQUAL_UINT8(0, addr0.bus());
    TEST_ASSERT_EQUAL_UINT8(0, addr0.shortAddr());
    TEST_ASSERT_TRUE(addr0.isAssigned());

    DaliInternalAddr addr63(1, 63);
    TEST_ASSERT_EQUAL_UINT8(1, addr63.bus());
    TEST_ASSERT_EQUAL_UINT8(63, addr63.shortAddr());
    TEST_ASSERT_TRUE(addr63.isAssigned());

    DaliInternalAddr unassigned(0, 64);
    TEST_ASSERT_FALSE(unassigned.isAssigned());

    DaliInternalAddr invalid(0xFF, 0xFF);
    TEST_ASSERT_EQUAL_UINT16(0xFFFF, invalid.value);
    TEST_ASSERT_FALSE(invalid.isAssigned());
}

static void test_dali_long_address_conversions() {
    const DaliLongAddress_t addrMin = 0x000000;
    const DaliLongAddress_t addrMax = 0xFFFFFF;
    const DaliLongAddress_t addrMid = 0x0A1B2C;
    const DaliLongAddress_t addrPadded = 0x00000F;

    auto strMin = utils::longAddressToString(addrMin);
    TEST_ASSERT_EQUAL_STRING("000000", strMin.data());

    auto strMax = utils::longAddressToString(addrMax);
    TEST_ASSERT_EQUAL_STRING("FFFFFF", strMax.data());

    auto strMid = utils::longAddressToString(addrMid);
    TEST_ASSERT_EQUAL_STRING("0A1B2C", strMid.data());

    auto strPadded = utils::longAddressToString(addrPadded);
    TEST_ASSERT_EQUAL_STRING("00000F", strPadded.data());

    TEST_ASSERT_EQUAL_HEX32(addrMin, utils::stringToLongAddress("000000").value());
    TEST_ASSERT_EQUAL_HEX32(addrMax, utils::stringToLongAddress("FFFFFF").value());
    TEST_ASSERT_EQUAL_HEX32(addrMid, utils::stringToLongAddress("0A1B2C").value());
    TEST_ASSERT_EQUAL_HEX32(addrPadded, utils::stringToLongAddress("00000F").value());

    TEST_ASSERT_EQUAL_HEX32(0xABCDEF, utils::stringToLongAddress("abcdef").value());

    TEST_ASSERT_FALSE(utils::stringToLongAddress("").has_value());
    TEST_ASSERT_FALSE(utils::stringToLongAddress("1234567").has_value());
    TEST_ASSERT_FALSE(utils::stringToLongAddress("0A1B2Z").has_value());
    TEST_ASSERT_FALSE(utils::stringToLongAddress("-1").has_value());
}

static void test_dali_part304_sensor_lux_math() {
    TEST_ASSERT_EQUAL_FLOAT(0.0f, utils::rawToLux(0));

    // raw = 1 -> 10^0 = 1 Lux
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 1.0f, utils::rawToLux(1));

    // raw = 41 -> 10^((41-1)/40) = 10^1 = 10 Lux
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 10.0f, utils::rawToLux(41));

    // raw = 81 -> 10^((81-1)/40) = 10^2 = 100 Lux
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 100.0f, utils::rawToLux(81));

    // raw = 161 -> 10^4 = 10000 Lux
    TEST_ASSERT_FLOAT_WITHIN(1.0f, 10000.0f, utils::rawToLux(161));
}

static void test_dali_part303_occupancy_math() {
    TEST_ASSERT_FALSE(utils::isOccupied(0x00));
    TEST_ASSERT_FALSE(utils::isOccupied(0xF0));

    TEST_ASSERT_TRUE(utils::isOccupied(0x01));
    TEST_ASSERT_TRUE(utils::isOccupied(0x02));
    TEST_ASSERT_TRUE(utils::isOccupied(0x03));
    TEST_ASSERT_TRUE(utils::isOccupied(0x55));
}

static void test_dali_raw_frame_properties() {
    DaliRawFrame frameFwd16{.data = 0x0123, .bits = 16, .type = DaliFrameType::Forward16};
    TEST_ASSERT_TRUE(frameFwd16.isValid());
    TEST_ASSERT_FALSE(frameFwd16.isBackward());

    DaliRawFrame frameBack8{.data = 0xFE, .bits = 8, .type = DaliFrameType::Backward8};
    TEST_ASSERT_TRUE(frameBack8.isValid());
    TEST_ASSERT_TRUE(frameBack8.isBackward());

    DaliRawFrame frameCollision{.data = 0, .bits = 0, .type = DaliFrameType::Collision};
    TEST_ASSERT_FALSE(frameCollision.isValid());
    TEST_ASSERT_FALSE(frameCollision.isBackward());

    DaliRawFrame frameNoise{.data = 0, .bits = 0, .type = DaliFrameType::NoiseCorrupted};
    TEST_ASSERT_FALSE(frameNoise.isValid());
}

void run_dali_frame_and_encoding_tests() {
    RUN_TEST(test_dali_internal_addr_packing);
    RUN_TEST(test_dali_long_address_conversions);
    RUN_TEST(test_dali_part304_sensor_lux_math);
    RUN_TEST(test_dali_part303_occupancy_math);
    RUN_TEST(test_dali_raw_frame_properties);
}