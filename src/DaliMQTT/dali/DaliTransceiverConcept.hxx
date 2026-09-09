// Copyright (c) 2026 Alice-Trade Inc.
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef DALIMQTT_DALITRANSCEIVERCONCEPT_HXX
#define DALIMQTT_DALITRANSCEIVERCONCEPT_HXX

#include "dali/DaliFrame.hxx"
#include <concepts>
#include <cstdint>
#include <esp_err.h>

namespace daliMQTT {

template <typename T>
concept DaliTransceiverConcept = requires(T t, uint32_t data, uint8_t bits) {
    { t.sendAsync(data, bits) } -> std::same_as<esp_err_t>;
    { t.isInitialized() } -> std::same_as<bool>;
};

} // namespace daliMQTT
#endif // DALIMQTT_DALITRANSCEIVERCONCEPT_HXX
