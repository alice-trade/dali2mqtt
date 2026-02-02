#ifndef DALIMQTT_DALIDEVICEIDENTITY_HXX
#define DALIMQTT_DALIDEVICEIDENTITY_HXX

namespace daliMQTT {
    using DaliLongAddress_t = uint32_t;

    struct DeviceIdentity {
        DaliLongAddress_t long_address{0};      // 24-bit DALI Long (random) Address
        uint8_t short_address{0xFF};            // Short addr
        std::string gtin;                       // GTIN
        bool available{false};                  // Runtime Availability flag

        [[nodiscard]] bool is_assigned() const { return short_address < 64; }
    };
}
#endif //DALIMQTT_DALIDEVICEIDENTITY_HXX