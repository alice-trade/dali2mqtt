// Copyright (c) 2026 Alice-Trade Inc.
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef DALIMQTT_DALILONGADDRCONVERSIONS_HXX
#define DALIMQTT_DALILONGADDRCONVERSIONS_HXX

#include "dali/DaliAddress.hxx"
#include <algorithm>
#include <array>
#include <charconv>
#include <optional>
#include <string_view>

namespace daliMQTT::utils {

using DaliLongAddrStr = std::array<char, 7>;

/**
 * @brief Converting a 24-bit DALI address to a HEX string
 */
inline DaliLongAddrStr longAddressToString(const DaliLongAddress_t addr) noexcept {
    DaliLongAddrStr result{};
    if (auto [ptr, ec] = std::to_chars(result.data(), result.data() + 6, addr & 0xFFFFFF, 16); ec == std::errc()) {
        const size_t len = ptr - result.data();
        if (len < 6) {
            std::move_backward(result.data(), result.data() + len, result.data() + 6);
            std::fill_n(result.data(), (6 - len), '0');
        }
        for (size_t i = 0; i < 6; ++i) {
            if (result[i] >= 'a' && result[i] <= 'f') {
                result[i] -= ('a' - 'A');
            }
        }
    }
    result[6] = '\0';
    return result;
}

/**
 * @brief Parsing HEX string into 24-bit DALI address
 */
inline std::optional<DaliLongAddress_t> stringToLongAddress(const std::string_view s) noexcept {
    if (s.empty() || s.length() > 6)
        return std::nullopt;

    DaliLongAddress_t addr = 0;
    if (auto [ptr, ec] = std::from_chars(s.data(), s.data() + s.size(), addr, 16);
        ec == std::errc() && ptr == s.data() + s.size()) {
        return addr;
    }
    return std::nullopt;
}

} // namespace daliMQTT::utils

#endif // DALIMQTT_DALILONGADDRCONVERSIONS_HXX