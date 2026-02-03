#ifndef DALIMQTT_DALICOMMON_HXX
#define DALIMQTT_DALICOMMON_HXX
#include "dali/DaliControlGear.hxx"
#include "dali/DaliInputDevice.hxx"

namespace daliMQTT
{
    // Dali address Type
    enum class DaliAddressType : uint8_t {
        Short,
        Group,
        Broadcast,
        Special
    };

    // Dali frame structure
    struct dali_frame_t {
        uint32_t data;
        uint8_t length;
        bool is_backward_frame;
    };

    struct DaliPublishState {
        std::optional<uint8_t> level;
        std::optional<uint8_t> status_byte;
        std::optional<uint16_t> color_temp;
        std::optional<DaliRGB> rgb;
        std::optional<DaliColorMode> active_mode;
    };
    using DaliDevice = std::variant<ControlGear, InputDevice>;

    using DaliLongAddrStr = std::array<char, 7>;

    struct GetIdentityVisitor {
        const DeviceIdentity& operator()(const DeviceIdentity& d) const { return d; }
        DeviceIdentity& operator()(DeviceIdentity& d) const { return d; }
    };

    inline const DeviceIdentity& getIdentity(const DaliDevice& dev) {
        return std::visit(GetIdentityVisitor{}, dev);
    }

    inline DeviceIdentity& getIdentity(DaliDevice& dev) {
        return std::visit(GetIdentityVisitor{}, dev);
    }

    struct DaliGroup {
        uint8_t id{};                  // Group ID
        uint8_t current_level{0};      // Current Level
        uint8_t last_level{254};       // Last level
        std::optional<uint16_t> color_temp;
        std::optional<DaliRGB> rgb;
    };
    using DaliLongAddrStr = std::array<char, 7>; // DALI Long Str: 6 hex chars + null
    struct DeferredRequest {
        uint8_t short_address;
        int64_t execute_at_ts; // Timestamp (ms)
    };
} // daliMQTT

#endif //DALIMQTT_DALICOMMON_HXX