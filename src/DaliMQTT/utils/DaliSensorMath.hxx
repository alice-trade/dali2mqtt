// Copyright (c) 2026 Alice-Trade Inc.
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef DALIMQTT_DALISENSORMATH_HXX
#define DALIMQTT_DALISENSORMATH_HXX

#include <cmath>
#include <cstdint>

namespace daliMQTT::utils {

/**
 * @brief Recalculation of the logarithmic value of sensor Part 304 into lux
 */
inline float rawToLux(const uint8_t raw) noexcept {
    return (raw > 0) ? powf(10.0f, (static_cast<float>(raw) - 1.0f) / 40.0f) : 0.0f;
}

/**
 * @brief Checking the presence status of Part 303 by instance value
 */
constexpr bool isOccupied(const uint8_t instanceValue) noexcept {
  return (instanceValue & 0x03) != 0;
}

} // namespace daliMQTT::utils

#endif // DALIMQTT_DALISENSORMATH_HXX