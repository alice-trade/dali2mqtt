// Copyright (c) 2026 Alice-Trade Inc.
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef DALIMQTT_DALIFRAME_HXX
#define DALIMQTT_DALIFRAME_HXX

#include <cstdint>

namespace daliMQTT {

enum class DaliFrameType : uint8_t {
    Forward16,     ///< Standard 16-bit control/request command
    Forward24,     ///< 24-bit DALI-2 command
    Backward8,     ///< 8-bit ballast response to request
    Collision,     ///< A bus collision has been detected
    NoiseCorrupted,///< The frame was discarded due to a Manchester violation
    TxEchoSuccess, ///< Successful completion of one's own broadcast
    Wakeup 
};

struct DaliRawFrame {
    uint32_t data{0};
    uint8_t bits{0};
    DaliFrameType type{DaliFrameType::Forward16};
    int64_t timestampUs{0};

    [[nodiscard]] constexpr bool isBackward() const noexcept { return type == DaliFrameType::Backward8; }

    [[nodiscard]] constexpr bool isValid() const noexcept {
        return type == DaliFrameType::Forward16 || type == DaliFrameType::Forward24 || type == DaliFrameType::Backward8;
    }
};

} // namespace daliMQTT
#endif // DALIMQTT_DALIFRAME_HXX
