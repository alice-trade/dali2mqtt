// Copyright (c) 2026 Alice-Trade Inc.
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef DALIMQTT_DALIGROUPEVENT_HXX
#define DALIMQTT_DALIGROUPEVENT_HXX

#include "dali/DaliColor.hxx"
#include <cstdint>
#include <optional>

namespace daliMQTT {

struct GroupStateChangeEvent {
    uint8_t busId{0};
    uint8_t groupId{0};
    uint8_t level{0};
    std::optional<uint16_t> colorTemp;
    std::optional<DaliRGB> rgb;
};

using GroupStateCallback = void (*)(const GroupStateChangeEvent& event, void* userCtx);

} // namespace daliMQTT

#endif // DALIMQTT_DALIGROUPEVENT_HXX