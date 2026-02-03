// Copyright (c) 2026 Alice-Trade Inc.
// SPDX-License-Identifier: GPL-2.0-or-later

#ifndef DALIMQTT_DALIDT8_HXX
#define DALIMQTT_DALIDT8_HXX
namespace daliMQTT {

    struct DaliRGB {
        uint8_t r{0};
        uint8_t g{0};
        uint8_t b{0};
        auto operator<=>(const DaliRGB&) const = default;
    };

    enum class DaliColorMode {
        Unknown,
        Tc,     // Tunable White
        Rgb     // RGB / RGBW
    };

    struct ColorFeatures {
        DaliColorMode active_mode{DaliColorMode::Tc};

        std::optional<uint16_t> min_mireds;
        std::optional<uint16_t> max_mireds;

        std::optional<uint16_t> current_tc;
        std::optional<DaliRGB> current_rgb;

        bool supports_rgb{false};
        bool supports_tc{false};

        int64_t last_poll_ts{0};
    };
}

#endif //DALIMQTT_DALIDT8_HXX
