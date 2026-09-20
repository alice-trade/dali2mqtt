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
concept DaliBusConcept = requires(
    const T b,
    DaliAddressType addrType,
    uint8_t shortAddr,
    uint8_t addr,
    uint8_t instance,
    uint8_t level,
    uint8_t data,
    uint8_t bank,
    uint8_t offset,
    uint8_t length,
    uint8_t* buffer,
    uint32_t rawData,
    OpCode opcode,
    SpecialOpCode specialOp
) {
    { b.sendDAPC(addrType, addr, level) } -> std::same_as<esp_err_t>;
    { b.sendGearCommand(addrType, addr, opcode, false) } -> std::same_as<esp_err_t>;
    { b.sendGearSpecial(specialOp, data, false) } -> std::same_as<esp_err_t>;
    { b.queryGear(addrType, addr, opcode) } -> std::same_as<std::optional<uint8_t>>;
    { b.queryGearSpecial(specialOp, data) } -> std::same_as<std::optional<uint8_t>>;

    { b.sendDeviceCommand(shortAddr, data, false) } -> std::same_as<esp_err_t>;
    { b.sendInstanceCommand(shortAddr, instance, data, false) } -> std::same_as<esp_err_t>;
    { b.sendDeviceSpecial(data, data, false) } -> std::same_as<esp_err_t>;
    { b.queryDevice(shortAddr, data) } -> std::same_as<std::optional<uint8_t>>;
    { b.queryInstance(shortAddr, instance, data) } -> std::same_as<std::optional<uint8_t>>;

    { b.queryRaw(rawData, 16U) } -> std::same_as<std::optional<uint8_t>>;
    { b.setDtr0(data) } -> std::same_as<esp_err_t>;
    { b.setDtr1(data) } -> std::same_as<esp_err_t>;
    { b.setDtr2(data) } -> std::same_as<esp_err_t>;
    { b.readMemoryLocation(shortAddr, bank, offset) } -> std::same_as<std::optional<uint8_t>>;
    { b.readMemoryBlock(shortAddr, bank, offset, buffer, length) } -> std::same_as<bool>;

    { b.lockBus() } -> std::same_as<void>;
    { b.unlockBus() } -> std::same_as<void>;
    { b.getBusId() } -> std::same_as<uint8_t>;
    { b.isInitialized() } -> std::same_as<bool>;
};

} // namespace daliMQTT

#endif // DALIMQTT_DALIBUSCONCEPT_HXX