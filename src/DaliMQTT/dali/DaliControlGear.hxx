// Copyright (c) 2026 Alice-Trade Inc.
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef DALIMQTT_DALICONTROLGEAR_HXX
#define DALIMQTT_DALICONTROLGEAR_HXX

#include "dali/DaliColor.hxx"
#include "dali/DaliDeviceIdentity.hxx"
#include <cstdint>
#include <optional>

namespace daliMQTT {

struct ControlGear : DeviceIdentity {
    uint8_t currentLevel{0};
    uint8_t lastLevel{254};
    uint8_t statusByte{0};
    uint8_t minLevel{1};
    uint8_t maxLevel{254};
    uint8_t powerOnLevel{254};
    uint8_t systemFailureLevel{254};

    std::optional<uint8_t> deviceType;
    std::optional<ColorFeatures> color;

    bool staticDataLoaded{false};
    bool initialSyncNeeded{true};
};

} // namespace daliMQTT

#endif // DALIMQTT_DALICONTROLGEAR_HXX