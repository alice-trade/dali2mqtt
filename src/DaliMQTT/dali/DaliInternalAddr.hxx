// Copyright (c) 2026 Alice-Trade Inc.
// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef DALIMQTT_DALIINTERNALADDR_HXX
#define DALIMQTT_DALIINTERNALADDR_HXX

namespace daliMQTT {
    struct DaliInternalAddr {
        uint16_t value{0xFFFF};

        constexpr DaliInternalAddr() = default;
        explicit constexpr DaliInternalAddr(const uint16_t v) : value(v) {}
        constexpr DaliInternalAddr(const uint8_t bus, const uint8_t sa) : value((static_cast<uint16_t>(bus) << 8) | sa) {}

        [[nodiscard]] constexpr uint8_t bus() const {
            return (value >> 8) & 0xFF;
        }
        [[nodiscard]] constexpr uint8_t shortAddr() const {
            return value & 0xFF;
        }
        [[nodiscard]] constexpr bool isAssigned() const {
            return shortAddr() < 64;
        }

        constexpr bool operator==(const DaliInternalAddr& other) const { return value == other.value; }
        constexpr bool operator!=(const DaliInternalAddr& other) const { return value != other.value; }
        constexpr bool operator<(const DaliInternalAddr& other) const { return value < other.value; }
    };

}

#endif //DALIMQTT_DALIINTERNALADDR_HXX