// Copyright (c) 2026 Alice-Trade Inc.
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef DALIMQTT_DALIDT8OPCODES_HXX
#define DALIMQTT_DALIDT8OPCODES_HXX

#include <cstdint>

namespace daliMQTT {

enum class DT8OpCode : uint8_t {
    SetTempTc = 0xE7,
    SetTempRGB = 0xEB,
    SetTempWAF = 0xEC,
    SetTempRGBWAFDirect = 0xED,
    Activate = 0xE2,
    QueryGearFeaturesStatus = 0xF7,
    QueryColourStatus = 0xF8,
    QueryColourType = 0xF9,
    QueryColourValue = 0xFA
};


} // namespace daliMQTT

#endif // DALIMQTT_DALIDT8OPCODES_HXX