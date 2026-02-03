// Copyright (c) 2026 Alice-Trade Inc.
// SPDX-License-Identifier: GPL-2.0-or-later

#ifndef DALIMQTT_DALIINPUTDEVICE_HXX
#define DALIMQTT_DALIINPUTDEVICE_HXX
#include "dali/DaliDeviceIdentity.hxx"

namespace daliMQTT {
    struct InputDevice : DeviceIdentity {
        uint8_t instance_byte{0};
    };
}
#endif //DALIMQTT_DALIINPUTDEVICE_HXX