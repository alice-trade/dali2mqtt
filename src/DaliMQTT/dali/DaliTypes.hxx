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

    // 24bit long dali addr type
    using DaliLongAddress_t = uint32_t;

    struct DaliDevice {
        DaliLongAddress_t long_address{};     // 24-bit DALI Long (random) Address
        uint8_t short_address{};              // Short addr
        uint8_t current_level{0};             // Current Level
        uint8_t last_level{254};              // Last Level
        uint8_t status_byte{0};               // Current status
        bool lamp_failure{false};             // Lamp failure flag
        std::optional<uint8_t> device_type{}; // Device Type
        std::string gtin{};                   // GTIN
        bool is_present{false};               // Presence flag
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