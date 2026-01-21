#ifndef DALIMQTT_DALICONTROLGEAR_HXX
#define DALIMQTT_DALICONTROLGEAR_HXX
#include "dali/DaliDeviceIdentity.hxx"
#include "dali/DaliDT8.hxx"

namespace daliMQTT {
    struct ControlGear : DeviceIdentity {
        uint8_t current_level{0};               // Current Level
        uint8_t last_level{254};                // Last Level
        uint8_t status_byte{0};                 // Current status
        uint8_t min_level{1};
        uint8_t max_level{254};
        uint8_t power_on_level{254};            // Level after power cycle
        uint8_t system_failure_level{254};

        std::optional<uint8_t> device_type;     // Device Type
        std::optional<ColorFeatures> color;     // DT8 Fields

        bool static_data_loaded{false};         // Static data load flag
        bool initial_sync_needed{true};         // First sync flag
    };
}
#endif //DALIMQTT_DALICONTROLGEAR_HXX