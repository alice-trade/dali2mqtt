// Copyright (c) 2026 Alice-Trade Inc.
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef DALIMQTT_DALICOLOR_HXX
#define DALIMQTT_DALICOLOR_HXX

#include <cstdint>
#include <optional>
#include <compare>

namespace daliMQTT {

struct DaliRGB {
    uint8_t r{0};
    uint8_t g{0};
    uint8_t b{0};
    constexpr auto operator<=>(const DaliRGB&) const noexcept = default;
};

enum class DaliColorMode : uint8_t {
    Unknown,
    Tc, ///< Tunable White
    Rgb ///< RGB / RGBW
};

struct ColorFeatures {
    DaliColorMode activeMode{DaliColorMode::Tc};
    std::optional<uint16_t> minMireds;
    std::optional<uint16_t> maxMireds;
    std::optional<uint16_t> currentTc;
    std::optional<DaliRGB> currentRgb;
    bool supportsRgb{false};
    bool supportsTc{false};
};

} // namespace daliMQTT

#endif // DALIMQTT_DALICOLOR_HXX