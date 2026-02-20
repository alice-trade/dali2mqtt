// Copyright (c) 2026 Alice-Trade Inc.
// SPDX-License-Identifier: GPL-2.0-or-later

#ifndef DALIMQTT_DALIDEVICEIDENTITY_HXX
#define DALIMQTT_DALIDEVICEIDENTITY_HXX

namespace daliMQTT {
    using DaliLongAddress_t = uint32_t;

    inline uint8_t extractShortAddr(const uint16_t internal_addr) {
        return internal_addr & 0xFF;
    }

    struct DeviceIdentity {
        DaliLongAddress_t long_address{0};      // 24-bit DALI Long (random) Address
        uint16_t internal_address{0xFFFF};            // Short addr
        std::string gtin;                       // GTIN
        bool available{false};                  // Runtime Availability flag

        [[nodiscard]] bool is_assigned() const { return extractShortAddr(internal_address) < 64; }
    };
}
#endif //DALIMQTT_DALIDEVICEIDENTITY_HXX