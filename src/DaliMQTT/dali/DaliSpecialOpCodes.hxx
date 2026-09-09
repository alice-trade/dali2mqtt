//  Copyright (c) 2026 Alice-Trade Inc.
//  SPDX-License-Identifier: GPL-3.0-or-later

#ifndef DALIMQTT_DALISPECIALOPCODES_HXX
#define DALIMQTT_DALISPECIALOPCODES_HXX

#include <cstdint>

namespace daliMQTT {

// IEC 62386-102
enum class SpecialOpCode : uint8_t {
    Terminate = 0xA1,
    Dtr0 = 0xA3,
    Initialise = 0xA5,
    Randomise = 0xA7,
    Compare = 0xA9,
    Withdraw = 0xAB,
    Ping = 0xAF,
    SearchAddrH = 0xB1,
    SearchAddrM = 0xB3,
    SearchAddrL = 0xB5,
    ProgramShortAddr = 0xB7,
    VerifyShortAddr = 0xB9,
    QueryShortAddr = 0xBB,
    PhysicalSelection = 0xBD,
    EnableDeviceTypeX = 0xC1,
    Dtr1 = 0xC3,
    Dtr2 = 0xC5,
    WriteMemoryLocation = 0xC7,
    WriteMemoryLocNoReply = 0xC9
};

} // namespace daliMQTT

#endif // DALIMQTT_DALISPECIALOPCODES_HXX
