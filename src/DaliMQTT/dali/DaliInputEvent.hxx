// Copyright (c) 2026 Alice-Trade Inc.
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef DALIMQTT_DALIINPUTEVENT_HXX
#define DALIMQTT_DALIINPUTEVENT_HXX

#include "dali/DaliAddress.hxx"
#include <cstdint>

namespace daliMQTT {

enum class InputAddressType : uint8_t {
    Short,
    Group,
    Instance,
    InstanceGroup,
    Broadcast,
    Unknown
};

struct InputDeviceEvent {
    uint8_t busId{0};
    uint8_t shortAddress{0};
    uint8_t instanceNumber{0};
    uint8_t instanceType{0};
    uint16_t eventCode{0};
    InputAddressType addressType{InputAddressType::Short};
    DaliLongAddress_t longAddress{InvalidLongAddr};
};

using InputEventCallback = void (*)(const InputDeviceEvent& event, void* userCtx);

} // namespace daliMQTT

#endif // DALIMQTT_DALIINPUTEVENT_HXX