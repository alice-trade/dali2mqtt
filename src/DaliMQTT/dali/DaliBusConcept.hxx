// Copyright (c) 2026 Alice-Trade Inc.
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef DALIMQTT_DALIBUSCONCEPT_HXX
#define DALIMQTT_DALIBUSCONCEPT_HXX

#include "dali/DaliAddress.hxx"
#include "dali/DaliOpCodes.hxx"
#include "dali/DaliSpecialOpCodes.hxx"
#include <concepts>
#include <cstdint>
#include <esp_err.h>
#include <optional>

namespace daliMQTT {

template <typename T>
concept DaliBusConcept =
    requires(T b, DaliAddressType addrType, uint8_t addr, OpCode opcode, SpecialOpCode specialOp, uint8_t level) {
        { b.sendDACP(addrType, addr, level) } -> std::same_as<esp_err_t>;
        { b.sendCommand(addrType, addr, opcode, false) } -> std::same_as<esp_err_t>;
        { b.sendSpecialCommand(specialOp, level, false) } -> std::same_as<esp_err_t>;
        { b.sendInputDeviceCommand(addr, level, level) } -> std::same_as<esp_err_t>;
        { b.query(addrType, addr, opcode) } -> std::same_as<std::optional<uint8_t>>;
        { b.querySpecial(specialOp, level) } -> std::same_as<std::optional<uint8_t>>;
        { b.queryRaw(0U, 16U) } -> std::same_as<std::optional<uint8_t>>;
        { b.setDtr0(level) } -> std::same_as<esp_err_t>;
        { b.setDtr1(level) } -> std::same_as<esp_err_t>;
        { b.readMemoryLocation(addr, level, level) } -> std::same_as<std::optional<uint8_t>>;
        { b.getBusId() } -> std::same_as<uint8_t>;
        { b.isInitialized() } -> std::same_as<bool>;
    };

} // namespace daliMQTT

#endif // DALIMQTT_DALIBUSCONCEPT_HXX