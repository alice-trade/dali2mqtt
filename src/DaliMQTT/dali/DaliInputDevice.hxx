// Copyright (c) 2026 Alice-Trade Inc.
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef DALIMQTT_DALIINPUTDEVICE_HXX
#define DALIMQTT_DALIINPUTDEVICE_HXX

#include "dali/DaliDeviceIdentity.hxx"
#include <cstdint>

namespace daliMQTT {

struct InputDevice : DeviceIdentity {
    uint8_t instanceByte{0};
};

} // namespace daliMQTT

#endif // DALIMQTT_DALIINPUTDEVICE_HXX