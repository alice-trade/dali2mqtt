#ifndef DALIMQTT_DALICONTROLGEAR_HXX
#define DALIMQTT_DALICONTROLGEAR_HXX
#include "dali/DaliDeviceIdentity.hxx"

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

    struct ControlGear : DeviceIdentity {
        uint8_t current_level{0};               // Current Level
        uint8_t last_level{254};                // Last Level
        uint8_t status_byte{0};                 // Current status
        uint8_t min_level{1};
        uint8_t max_level{254};
        uint8_t power_on_level{254};            // Level after power cycle
        uint8_t system_failure_level{254};

        std::optional<uint8_t> device_type;     // Device Type

        // Color Features
        std::optional<uint16_t> min_mireds{};
        std::optional<uint16_t> max_mireds{};
        std::optional<uint16_t> color_temp{};
        std::optional<DaliRGB> rgb{};
        DaliColorMode active_mode{DaliColorMode::Tc};

        bool supports_rgb{false};
        bool supports_tc{false};

        bool static_data_loaded{false};         // Static data load flag
        bool initial_sync_needed{true};         // First sync flag
        int64_t last_color_poll_ts{0};
    };
}
#endif //DALIMQTT_DALICONTROLGEAR_HXX