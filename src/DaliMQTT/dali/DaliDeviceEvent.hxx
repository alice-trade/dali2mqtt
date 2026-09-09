// Copyright (c) 2026 Alice-Trade Inc.
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef DALIMQTT_DALIDEVICEEVENT_HXX
#define DALIMQTT_DALIDEVICEEVENT_HXX

#include "dali/DaliAddress.hxx"
#include "dali/DaliColor.hxx"
#include <cstdint>
#include <optional>

namespace daliMQTT {

struct DeviceStateChangeEvent {
    DaliLongAddress_t longAddress{InvalidLongAddr};
    DaliInternalAddr internalAddress{};
    uint8_t level{0};
    uint8_t statusByte{0};
    bool available{true};
    std::optional<uint16_t> colorTemp;
    std::optional<DaliRGB> rgb;
};

using DeviceStateCallback = void (*)(const DeviceStateChangeEvent& event, void* userCtx);

} // namespace daliMQTT

#endif // DALIMQTT_DALIDEVICEEVENT_HXX