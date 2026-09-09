//  Copyright (c) 2026 Alice-Trade Inc.
//  SPDX-License-Identifier: GPL-3.0-or-later

#ifndef DALIMQTT_DALIADDRESS_HXX
#define DALIMQTT_DALIADDRESS_HXX

#include <cstdint>
#include <compare>

namespace daliMQTT {

enum class DaliAddressType : uint8_t { Short, Group, Broadcast, Special };

/**
 * @brief Internal unified address (bus + short address)
 */
struct DaliInternalAddr {
    uint16_t value{0xFFFF};

    constexpr DaliInternalAddr() noexcept = default;
    explicit constexpr DaliInternalAddr(const uint16_t v) noexcept : value(v) {}
    constexpr DaliInternalAddr(const uint8_t bus, const uint8_t shortAddr) noexcept
        : value(static_cast<uint16_t>((static_cast<uint16_t>(bus) << 8) | shortAddr)) {}

    [[nodiscard]] constexpr uint8_t bus() const noexcept { return static_cast<uint8_t>((value >> 8) & 0xFF); }
    [[nodiscard]] constexpr uint8_t shortAddr() const noexcept { return static_cast<uint8_t>(value & 0xFF); }
    [[nodiscard]] constexpr bool isAssigned() const noexcept { return shortAddr() < 64; }

    constexpr auto operator<=>(const DaliInternalAddr&) const noexcept = default;
};

using DaliLongAddress_t = uint32_t;
inline constexpr DaliLongAddress_t InvalidLongAddr = 0xFFFFFFFF;
inline constexpr DaliLongAddress_t ClashLongAddr   = 0xFFFFFFFE;

} // namespace daliMQTT

#endif // DALIMQTT_DALIADDRESS_HXX
