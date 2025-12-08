#ifndef DALIMQTT_DALITYPES_HXX
#define DALIMQTT_DALITYPES_HXX

namespace daliMQTT
{
    // Dali address Type
    typedef enum {
        DALI_ADDRESS_TYPE_SHORT,
        DALI_ADDRESS_TYPE_GROUP,
        DALI_ADDRESS_TYPE_BROADCAST,
        DALI_ADDRESS_TYPE_SPECIAL_CMD
    } dali_addressType_t;

    // Dali frame structure
    struct dali_frame_t {
        uint32_t data;
        uint8_t length;
        bool is_backward_frame;
    };
    struct DaliRGB {
        uint8_t r{0};
        uint8_t g{0};
        uint8_t b{0};
        bool operator==(const DaliRGB& other) const {
            return r == other.r && g == other.g && b == other.b;
        }
        bool operator!=(const DaliRGB& other) const {
            return !(*this == other);
        }
    };
    // 24bit long dali addr type
    using DaliLongAddress_t = uint32_t;

    struct DaliDevice {
        DaliLongAddress_t long_address{};     // 24-bit DALI Long (random) Address
        uint8_t short_address{};              // Short addr

        uint8_t current_level{0};             // Current Level
        uint8_t last_level{254};              // Last Level
        uint8_t status_byte{0};               // Current status

        std::optional<uint8_t> device_type{}; // Device Type
        std::string gtin{};                   // GTIN

        std::optional<uint16_t> min_mireds{}; // Min CT
        std::optional<uint16_t> max_mireds{}; // Max CT
        std::optional<uint16_t> color_temp{}; // Current CT in Mireds
        std::optional<DaliRGB> rgb{};         // Current RGB

        bool supports_rgb{false};
        bool supports_tc{false};

        bool available{false};                // Runtime Availability flag
        bool initial_sync_needed{true};       // First sync flag
        bool static_data_loaded{false};       // Static data load flag
    };

    struct DaliGroup {
        uint8_t id{};                    // Group ID
        uint8_t current_level{0};      // Current Level
        uint8_t last_level{254};       // Last level
    };
    using DaliLongAddrStr = std::array<char, 7>; // DALI Long Str: 6 hex chars + null
    struct DeferredRequest {
        uint8_t short_address;
        int64_t execute_at_ts; // Timestamp (ms)
    };
} // daliMQTT

#endif //DALIMQTT_DALITYPES_HXX