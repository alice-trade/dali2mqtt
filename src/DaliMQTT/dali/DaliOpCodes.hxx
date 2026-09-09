//  Copyright (c) 2026 Alice-Trade Inc.
//  SPDX-License-Identifier: GPL-3.0-or-later

#ifndef DALIMQTT_DALIOPCODES_HXX
#define DALIMQTT_DALIOPCODES_HXX

#include <cstdint>

namespace daliMQTT {
// IEC 62386-102
enum class OpCode : uint8_t {
    Off = 0x00,
    Up = 0x01,
    Down = 0x02,
    StepUp = 0x03,
    StepDown = 0x04,
    RecallMaxLevel = 0x05,
    RecallMinLevel = 0x06,
    StepDownAndOff = 0x07,
    OnAndStepUp = 0x08,
    EnableDapcSequence = 0x09,

    GoToScene0 = 0x10,
    GoToScene1 = 0x11,
    GoToScene2 = 0x12,
    GoToScene3 = 0x13,
    GoToScene4 = 0x14,
    GoToScene5 = 0x15,
    GoToScene6 = 0x16,
    GoToScene7 = 0x17,
    GoToScene8 = 0x18,
    GoToScene9 = 0x19,
    GoToScene10 = 0x1A,
    GoToScene11 = 0x1B,
    GoToScene12 = 0x1C,
    GoToScene13 = 0x1D,
    GoToScene14 = 0x1E,
    GoToScene15 = 0x1F,

    Reset = 0x20,
    StoreActualLevel = 0x21,
    SetMaxLevel = 0x2A,
    SetMinLevel = 0x2B,
    SetSystemFailureLevel = 0x2C,
    SetPowerOnLevel = 0x2D,
    SetFadeTime = 0x2E,
    SetFadeRate = 0x2F,

    QueryStatus = 0x90,
    QueryControlGear = 0x91,
    QueryLampFailure = 0x92,
    QueryLampPowerOn = 0x93,
    QueryLimitError = 0x94,
    QueryResetState = 0x95,
    QueryMissingShortAddr = 0x96,
    QueryVersionNumber = 0x97,
    QueryContentDtr0 = 0x98,
    QueryDeviceType = 0x99,
    QueryPhysicalMinLevel = 0x9A,
    QueryPowerFailure = 0x9B,
    QueryContentDtr1 = 0x9C,
    QueryContentDtr2 = 0x9D,
    QueryActualLevel = 0xA0,
    QueryMaxLevel = 0xA1,
    QueryMinLevel = 0xA2,
    QueryPowerOnLevel = 0xA3,
    QuerySystemFailureLevel = 0xA4,
    QueryFadeTimeFadeRate = 0xA5,
    QueryGroups0_7 = 0xC0,
    QueryGroups8_15 = 0xC1,
    QueryRandomAddrH = 0xC2,
    QueryRandomAddrM = 0xC3,
    QueryRandomAddrL = 0xC4,
    ReadMemoryLocation = 0xC5
};

} // namespace daliMQTT

#endif // DALIMQTT_DALIOPCODES_HXX
