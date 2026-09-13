// Copyright (c) 2026 Alice-Trade Inc.
// SPDX-License-Identifier: GPL-3.0-or-later

#include "dali/DaliDeviceRegistry.hxx"
#include "dali/DaliDT8OpCodes.hxx"
#include "utils/NvsHandle.hxx"
#include <algorithm>
#include <cstring>
#include <esp_log.h>
#include <esp_timer.h>

namespace daliMQTT {

static constexpr char TAG[] = "DaliRegistry";
#ifndef DALIMQTT_REG_NVS_NAMESPACE
#define DALIMQTT_REG_NVS_NAMESPACE "dali_reg"
#endif
static constexpr char NVS_NAMESPACE[] = DALIMQTT_REG_NVS_NAMESPACE;
static constexpr char NVS_MAP_KEY[] = "addr_map";

DaliDeviceRegistry::DaliDeviceRegistry(DaliBusEngine& busEngine) : m_bus(busEngine) {
    m_internalToLongMap.fill(InvalidLongAddr);
}

DaliDeviceRegistry::~DaliDeviceRegistry() {
    m_bus.setSnifferCallback(nullptr, nullptr);
    if (m_pollTaskHandle)
        vTaskDelete(m_pollTaskHandle);
}

esp_err_t DaliDeviceRegistry::init() {
    ESP_LOGI(TAG, "Initializing DALI Device Registry...");

    m_bus.setSnifferCallback(snifferCallbackEntry, this);

    if (!loadAddressMapFromNvs()) {
        ESP_LOGI(TAG, "Address map not found in NVS. Full scan will be performed.");
    }

    return ESP_OK;
}

void DaliDeviceRegistry::start() {
    if (!m_pollTaskHandle) {
        xTaskCreate(pollTaskRunner, "dali_poll_task", 4096, this, 3, &m_pollTaskHandle);
    }
}

void DaliDeviceRegistry::snifferCallbackEntry(const DaliRawFrame& frame, uint8_t, void* ctx) {
    auto* self = static_cast<DaliDeviceRegistry*>(ctx);
    if (frame.bits == 16) {
        self->processSnifferFrame(frame);
    } else if (frame.bits == 24) {
        self->processInputDeviceFrame(frame);
    }
}

void DaliDeviceRegistry::notifyDeviceChange(const ControlGear& gear) const {
    if (!m_deviceStateCb)
        return;

    DeviceStateChangeEvent ev{.longAddress = gear.longAddress,
                              .internalAddress = gear.internalAddress,
                              .level = gear.currentLevel,
                              .statusByte = gear.statusByte,
                              .available = gear.available};
    if (gear.color.has_value()) {
        ev.colorTemp = gear.color->currentTc;
        ev.rgb = gear.color->currentRgb;
    }
    m_deviceStateCb(ev, m_deviceStateCtx);
}

void DaliDeviceRegistry::notifyGroupChange(const uint8_t busId, const uint8_t groupId,
                                           const DaliGroupState& state) const {
    if (!m_groupStateCb)
        return;

    const GroupStateChangeEvent ev{.busId = busId,
                                   .groupId = groupId,
                                   .level = state.currentLevel,
                                   .colorTemp = state.colorTemp,
                                   .rgb = state.rgb};
    m_groupStateCb(ev, m_groupStateCtx);
}

void DaliDeviceRegistry::notifyInputEvent(const InputDeviceEvent& ev) const {
    if (m_inputEventCb) {
        m_inputEventCb(ev, m_inputEventCtx);
    }
}

void DaliDeviceRegistry::processSnifferFrame(const DaliRawFrame& frame) {
    std::lock_guard<std::mutex> snifferLock(m_snifferMutex);

    const uint8_t addrByte = static_cast<uint8_t>((frame.data >> 8) & 0xFF);
    const uint8_t dataByte = static_cast<uint8_t>(frame.data & 0xFF);

    if ((addrByte & 0xE0) == 0xA0 || (addrByte & 0xE0) == 0xC0)
        return;

    const bool isCommand = (addrByte & 0x01) == 0x01;
    const bool isBroadcast = (addrByte == 0xFE || addrByte == 0xFF);
    const uint8_t busId = m_bus.getBusId();

    std::optional<uint8_t> targetGroup;
    std::optional<uint8_t> targetShort;

    if (!isBroadcast) {
        if ((addrByte & 0xE0) == 0x80)
            targetGroup = (addrByte >> 1) & 0x0F;
        else if ((addrByte & 0x80) == 0x00)
            targetShort = (addrByte >> 1) & 0x3F;
    }

    if (targetGroup.has_value()) {
        const uint8_t gid = *targetGroup;
        DaliGroupState updatedState;
        {
            std::lock_guard<std::mutex> lock(m_registryMutex);
            auto& gState = m_groupStates[busId][gid];
            if (!isCommand) {
                gState.currentLevel = dataByte;
                if (dataByte > 0)
                    gState.lastLevel = dataByte;
            } else {
                const auto op = static_cast<OpCode>(dataByte);
                if (op == OpCode::Off || op == OpCode::StepDownAndOff)
                    gState.currentLevel = 0;
                else if (op == OpCode::RecallMaxLevel)
                    gState.currentLevel = 254;
                else if (op == OpCode::RecallMinLevel)
                    gState.currentLevel = 1;
            }
            updatedState = gState;
        }
        notifyGroupChange(busId, gid, updatedState);
    }

    {
        std::lock_guard<std::mutex> lock(m_registryMutex);
        m_snifferEventsScratchpad.clear();

        for (auto& dev : m_devices) {
            auto* gear = etl::get_if<ControlGear>(&dev);
            if (!gear || gear->internalAddress.bus() != busId)
                continue;

            bool affected = isBroadcast;
            if (!affected && targetShort.has_value()) {
                if (gear->internalAddress.shortAddr() == *targetShort)
                    affected = true;
            }
            if (!affected && targetGroup.has_value()) {
                auto it = m_groupAssignments.find(gear->longAddress);
                if (it != m_groupAssignments.end() && it->second.test(*targetGroup))
                    affected = true;
            }

            if (!affected)
                continue;

            bool notify = false;
            if (!isCommand) {
                gear->currentLevel = dataByte;
                if (dataByte > 0)
                    gear->lastLevel = dataByte;
                notify = true;
            } else {
                const auto op = static_cast<OpCode>(dataByte);
                if (op == OpCode::Off || op == OpCode::StepDownAndOff) {
                    gear->currentLevel = 0;
                    notify = true;
                } else if (op == OpCode::RecallMaxLevel) {
                    gear->currentLevel = gear->maxLevel;
                    notify = true;
                } else if (op == OpCode::RecallMinLevel) {
                    gear->currentLevel = gear->minLevel;
                    notify = true;
                } else {
                    requestSync(gear->internalAddress, 300);
                }
            }

            if (notify && !m_snifferEventsScratchpad.full()) {
                DeviceStateChangeEvent ev{.longAddress = gear->longAddress,
                                          .internalAddress = gear->internalAddress,
                                          .level = gear->currentLevel,
                                          .statusByte = gear->statusByte,
                                          .available = gear->available};
                if (gear->color.has_value()) {
                    ev.colorTemp = gear->color->currentTc;
                    ev.rgb = gear->color->currentRgb;
                }
                m_snifferEventsScratchpad.push_back(ev);
            }
        }
    }

    if (m_deviceStateCb) {
        for (const auto& ev : m_snifferEventsScratchpad) {
            m_deviceStateCb(ev, m_deviceStateCtx);
        }
    }
}

void DaliDeviceRegistry::processInputDeviceFrame(const DaliRawFrame& frame) const {
    const uint32_t raw = frame.data;
    if ((raw & (1UL << 16)) != 0) {
        return; // Command frame, not an event
    }

    InputDeviceEvent ev{};
    ev.busId = m_bus.getBusId();
    ev.eventCode = static_cast<uint16_t>(raw & 0x03FF);
    const bool bit23 = (raw & (1UL << 23)) != 0;
    const bool bit22 = (raw & (1UL << 22)) != 0;
    const bool bit15 = (raw & (1UL << 15)) != 0;
    const uint8_t instanceField = static_cast<uint8_t>((raw >> 10) & 0x1F);

    if (!bit23) {
        ev.addressType = InputAddressType::Short;
        ev.shortAddress = static_cast<uint8_t>((raw >> 17) & 0x3F);
        const auto longAddrOpt = getLongAddress(DaliInternalAddr(ev.busId, ev.shortAddress), true);
        if (longAddrOpt.has_value()) {
            ev.longAddress = *longAddrOpt;
        }
        if (!bit15) {
            ev.instanceType = instanceField;
            ev.instanceNumber = 0;
        } else {
            ev.instanceNumber = instanceField;
            ev.instanceType = 0;
        }
    } else {
        if (!bit22 && !bit15) {
            ev.addressType = InputAddressType::Group;
            ev.shortAddress = static_cast<uint8_t>((raw >> 17) & 0x1F);
            ev.instanceType = instanceField;
        } else if (!bit22 && bit15) {
            ev.addressType = InputAddressType::Instance;
            ev.instanceType = static_cast<uint8_t>((raw >> 17) & 0x1F);
            ev.instanceNumber = instanceField;
        } else if (bit22 && !bit15) {
            ev.addressType = InputAddressType::InstanceGroup;
            ev.shortAddress = static_cast<uint8_t>((raw >> 17) & 0x1F);
            ev.instanceType = instanceField;
        } else {
            ev.addressType = InputAddressType::Broadcast;
            ev.shortAddress = 0xFF;
            ev.instanceNumber = instanceField;
        }
    }

    notifyInputEvent(ev);
}

esp_err_t DaliDeviceRegistry::setBrightness(const DaliLongAddress_t longAddr, const uint8_t level) {
    const auto intAddrOpt = getInternalAddress(longAddr);
    if (!intAddrOpt)
        return ESP_ERR_NOT_FOUND;

    const esp_err_t err = m_bus.sendDACP(DaliAddressType::Short, intAddrOpt->shortAddr(), level);
    if (err == ESP_OK) {
        ControlGear copyGear;
        bool found = false;
        {
            std::lock_guard<std::mutex> lock(m_registryMutex);
            for (auto& dev : m_devices) {
                if (getIdentity(dev).longAddress == longAddr) {
                    if (auto* gear = etl::get_if<ControlGear>(&dev)) {
                        gear->currentLevel = level;
                        if (level > 0)
                            gear->lastLevel = level;
                        copyGear = *gear;
                        found = true;
                    }
                    break;
                }
            }
        }
        if (found)
            notifyDeviceChange(copyGear);
    }
    return err;
}

esp_err_t DaliDeviceRegistry::setPower(const DaliLongAddress_t longAddr, const bool on) {
    const auto intAddrOpt = getInternalAddress(longAddr);
    if (!intAddrOpt)
        return ESP_ERR_NOT_FOUND;

    if (!on) {
        const esp_err_t err = m_bus.sendCommand(DaliAddressType::Short, intAddrOpt->shortAddr(), OpCode::Off);
        if (err == ESP_OK) {
            ControlGear copyGear;
            bool found = false;
            {
                std::lock_guard<std::mutex> lock(m_registryMutex);
                for (auto& dev : m_devices) {
                    if (getIdentity(dev).longAddress == longAddr) {
                        if (auto* gear = etl::get_if<ControlGear>(&dev)) {
                            gear->currentLevel = 0;
                            copyGear = *gear;
                            found = true;
                        }
                        break;
                    }
                }
            }
            if (found)
                notifyDeviceChange(copyGear);
        }
        return err;
    }

    uint8_t restoreLevel = 254;
    {
        std::lock_guard<std::mutex> lock(m_registryMutex);
        const auto it = std::ranges::find_if(
            m_devices, [longAddr](const auto& dev) { return getIdentity(dev).longAddress == longAddr; });

        if (it != m_devices.end()) {
            if (const auto* gear = etl::get_if<ControlGear>(&(*it))) {
                restoreLevel = (gear->lastLevel > 0) ? gear->lastLevel : gear->maxLevel;
            }
        }
    }
    return setBrightness(longAddr, restoreLevel);
}

esp_err_t DaliDeviceRegistry::setColorTemp(const DaliLongAddress_t longAddr, const uint16_t mireds) {
    const auto intAddrOpt = getInternalAddress(longAddr);
    if (!intAddrOpt)
        return ESP_ERR_NOT_FOUND;

    const uint8_t shortAddr = intAddrOpt->shortAddr();
    DaliBusLock bus_lock(m_bus);

    m_bus.setDtr1(static_cast<uint8_t>((mireds >> 8) & 0xFF));
    m_bus.setDtr0(static_cast<uint8_t>(mireds & 0xFF));
    m_bus.sendSpecialCommand(SpecialOpCode::EnableDeviceTypeX, 8);
    m_bus.sendCommand(DaliAddressType::Short, shortAddr, static_cast<OpCode>(DT8OpCode::SetTempTc));
    m_bus.sendSpecialCommand(SpecialOpCode::EnableDeviceTypeX, 8);
    const esp_err_t err =
        m_bus.sendCommand(DaliAddressType::Short, shortAddr, static_cast<OpCode>(DT8OpCode::Activate));

    if (err == ESP_OK) {
        ControlGear copyGear;
        bool found = false;
        {
            std::lock_guard<std::mutex> lock(m_registryMutex);
            for (auto& dev : m_devices) {
                if (getIdentity(dev).longAddress == longAddr) {
                    if (auto* gear = etl::get_if<ControlGear>(&dev)) {
                        if (!gear->color.has_value())
                            gear->color = ColorFeatures();
                        gear->color->currentTc = mireds;
                        copyGear = *gear;
                        found = true;
                    }
                    break;
                }
            }
        }
        if (found)
            notifyDeviceChange(copyGear);
    }
    return err;
}

esp_err_t DaliDeviceRegistry::setRgb(const DaliLongAddress_t longAddr, const uint8_t r, const uint8_t g,
                                     const uint8_t b) {
    const auto intAddrOpt = getInternalAddress(longAddr);
    if (!intAddrOpt)
        return ESP_ERR_NOT_FOUND;

    const uint8_t shortAddr = intAddrOpt->shortAddr();
    DaliBusLock bus_lock(m_bus);

    m_bus.setDtr2(b);
    m_bus.setDtr1(g);
    m_bus.setDtr0(r);

    m_bus.sendSpecialCommand(SpecialOpCode::EnableDeviceTypeX, 8);
    m_bus.sendCommand(DaliAddressType::Short, shortAddr, static_cast<OpCode>(DT8OpCode::SetTempRGB));

    m_bus.sendSpecialCommand(SpecialOpCode::EnableDeviceTypeX, 8);
    const esp_err_t err =
        m_bus.sendCommand(DaliAddressType::Short, shortAddr, static_cast<OpCode>(DT8OpCode::Activate));

    if (err == ESP_OK) {
        ControlGear copyGear;
        bool found = false;
        {
            std::lock_guard lock(m_registryMutex);
            for (auto& dev : m_devices) {
                if (getIdentity(dev).longAddress == longAddr) {
                    if (auto* gear = etl::get_if<ControlGear>(&dev)) {
                        if (!gear->color.has_value())
                            gear->color = ColorFeatures();
                        gear->color->currentRgb = DaliRGB{r, g, b};
                        copyGear = *gear;
                        found = true;
                    }
                    break;
                }
            }
        }
        if (found)
            notifyDeviceChange(copyGear);
    }
    return err;
}

esp_err_t DaliDeviceRegistry::setRgbwaf(const DaliLongAddress_t longAddr,
                                        const uint8_t r, const uint8_t g, const uint8_t b,
                                        const uint8_t w, const uint8_t a, const uint8_t f) {
    const auto intAddrOpt = getInternalAddress(longAddr);
    if (!intAddrOpt) return ESP_ERR_NOT_FOUND;

    const uint8_t shortAddr = intAddrOpt->shortAddr();
    DaliBusLock bus_lock(m_bus);

    m_bus.setDtr2(b);
    m_bus.setDtr1(g);
    m_bus.setDtr0(r);
    m_bus.sendSpecialCommand(SpecialOpCode::EnableDeviceTypeX, 8);
    m_bus.sendCommand(DaliAddressType::Short, shortAddr, static_cast<OpCode>(DT8OpCode::SetTempRGB));

    m_bus.setDtr2(f);
    m_bus.setDtr1(a);
    m_bus.setDtr0(w);
    m_bus.sendSpecialCommand(SpecialOpCode::EnableDeviceTypeX, 8);
    m_bus.sendCommand(DaliAddressType::Short, shortAddr, static_cast<OpCode>(DT8OpCode::SetTempWAF));

    m_bus.sendSpecialCommand(SpecialOpCode::EnableDeviceTypeX, 8);
    const esp_err_t err = m_bus.sendCommand(DaliAddressType::Short, shortAddr, static_cast<OpCode>(DT8OpCode::Activate));

    if (err == ESP_OK) {
        ControlGear copyGear;
        bool found = false;
        {
            std::lock_guard lock(m_registryMutex);
            for (auto& dev : m_devices) {
                if (getIdentity(dev).longAddress == longAddr) {
                    if (auto* gear = etl::get_if<ControlGear>(&dev)) {
                        if (!gear->color.has_value()) gear->color = ColorFeatures();
                        gear->color->currentRgb = DaliRGB{r, g, b};
                        copyGear = *gear;
                        found = true;
                    }
                    break;
                }
            }
        }
        if (found) notifyDeviceChange(copyGear);
    }
    return err;
}

esp_err_t DaliDeviceRegistry::setGroupBrightness(const uint8_t busId, const uint8_t groupId, const uint8_t level) {
    if (groupId >= 16 || busId >= BUS_COUNT)
        return ESP_ERR_INVALID_ARG;
    const esp_err_t err = m_bus.sendDACP(DaliAddressType::Group, groupId, level);
    if (err == ESP_OK) {
        DaliGroupState updatedState;
        {
            std::lock_guard<std::mutex> lock(m_registryMutex);
            m_groupStates[busId][groupId].currentLevel = level;
            if (level > 0)
                m_groupStates[busId][groupId].lastLevel = level;
            updatedState = m_groupStates[busId][groupId];
        }
        notifyGroupChange(busId, groupId, updatedState);
    }
    return err;
}

esp_err_t DaliDeviceRegistry::setGroupPower(const uint8_t busId, const uint8_t groupId, const bool on) {
    if (groupId >= 16 || busId >= BUS_COUNT)
        return ESP_ERR_INVALID_ARG;
    if (!on) {
        const esp_err_t err = m_bus.sendCommand(DaliAddressType::Group, groupId, OpCode::Off);
        if (err == ESP_OK) {
            DaliGroupState updatedState;
            {
                std::lock_guard<std::mutex> lock(m_registryMutex);
                m_groupStates[busId][groupId].currentLevel = 0;
                updatedState = m_groupStates[busId][groupId];
            }
            notifyGroupChange(busId, groupId, updatedState);
        }
        return err;
    }
    uint8_t restoreLevel = 254;
    {
        std::lock_guard<std::mutex> lock(m_registryMutex);
        restoreLevel = m_groupStates[busId][groupId].lastLevel;
        if (restoreLevel == 0)
            restoreLevel = 254;
    }
    return setGroupBrightness(busId, groupId, restoreLevel);
}

esp_err_t DaliDeviceRegistry::setDeviceGroupMembership(const DaliLongAddress_t longAddr, const uint8_t groupId,
                                                       const bool assigned) {
    if (groupId >= 16)
        return ESP_ERR_INVALID_ARG;
    const auto intAddrOpt = getInternalAddress(longAddr);
    if (!intAddrOpt)
        return ESP_ERR_NOT_FOUND;

    const uint8_t shortAddr = intAddrOpt->shortAddr();
    const auto opcode = assigned ? static_cast<OpCode>(0x60 + groupId) : static_cast<OpCode>(0x70 + groupId);

    const esp_err_t err = m_bus.sendCommand(DaliAddressType::Short, shortAddr, opcode, true);
    if (err == ESP_OK) {
        std::lock_guard<std::mutex> lock(m_registryMutex);
        m_groupAssignments[longAddr].set(groupId, assigned);
        m_nvsDirty = true;
        m_lastNvsDirtyTsMs = esp_timer_get_time() / 1000;
    }
    return err;
}

esp_err_t DaliDeviceRegistry::setAllGroupAssignments(const GroupAssignments& newAssignments) {
    std::lock_guard<std::mutex> lock(m_registryMutex);
    m_groupAssignments = newAssignments;
    m_nvsDirty = true;
    m_lastNvsDirtyTsMs = esp_timer_get_time() / 1000;
    return ESP_OK;
}

esp_err_t DaliDeviceRegistry::refreshGroupAssignmentsFromBus() {
    ESP_LOGI(TAG, "Refreshing group assignments from DALI bus...");

    GroupAssignments freshAssignments;

    for (uint8_t sa = 0; sa < 64; ++sa) {
        const auto g0_7 = m_bus.query(DaliAddressType::Short, sa, OpCode::QueryGroups0_7);
        const auto g8_15 = m_bus.query(DaliAddressType::Short, sa, OpCode::QueryGroups8_15);

        if (g0_7.has_value() && g8_15.has_value()) {
            const uint16_t mask = (static_cast<uint16_t>(*g8_15) << 8) | *g0_7;
            const auto longAddrOpt = getLongAddress(DaliInternalAddr(m_bus.getBusId(), sa));
            if (longAddrOpt.has_value()) {
                freshAssignments[*longAddrOpt] = GroupMask(mask);
            }
        }
        vTaskDelay(pdMS_TO_TICKS(15));
    }

    {
        std::lock_guard<std::mutex> lock(m_registryMutex);
        m_groupAssignments = freshAssignments;
        m_nvsDirty = true;
        m_lastNvsDirtyTsMs = esp_timer_get_time() / 1000;
    }
    return ESP_OK;
}

esp_err_t DaliDeviceRegistry::activateScene(const uint8_t, const uint8_t sceneId) const {
    if (sceneId >= 16)
        return ESP_ERR_INVALID_ARG;
    const auto op = static_cast<OpCode>(static_cast<uint8_t>(OpCode::GoToScene0) + sceneId);
    return m_bus.sendCommand(DaliAddressType::Broadcast, 0, op, false);
}

esp_err_t DaliDeviceRegistry::saveSceneLevels(const uint8_t, const uint8_t sceneId, const SceneLevels& levels) const {
    if (sceneId >= 16)
        return ESP_ERR_INVALID_ARG;

    for (uint8_t sa = 0; sa < 64; ++sa) {
        const uint8_t lvl = levels[sa];
        if (lvl != 255) {
            DaliBusLock lock(m_bus);
            m_bus.setDtr0(lvl);
            const auto storeOp = static_cast<OpCode>(0x40 + sceneId);
            m_bus.sendCommand(DaliAddressType::Short, sa, storeOp, true);
        }
        vTaskDelay(pdMS_TO_TICKS(15));
    }
    return ESP_OK;
}

SceneLevels DaliDeviceRegistry::querySceneLevels(const uint8_t, const uint8_t sceneId) const {
    SceneLevels levels{};
    levels.fill(255);
    if (sceneId >= 16)
        return levels;

    for (uint8_t sa = 0; sa < 64; ++sa) {
        const auto queryOp = static_cast<OpCode>(0xB0 + sceneId);
        const auto res = m_bus.query(DaliAddressType::Short, sa, queryOp);
        if (res.has_value()) {
            levels[sa] = *res;
        }
        vTaskDelay(pdMS_TO_TICKS(15));
    }
    return levels;
}

void DaliDeviceRegistry::requestSync(const DaliInternalAddr addr, const uint32_t delayMs) {
    if (!addr.isAssigned())
        return;

    std::lock_guard<std::mutex> lock(m_queueMutex);
    if (delayMs == 0) {
        if (!m_prioritySyncSet.contains(addr) && !m_prioritySyncQueue.full()) {
            m_prioritySyncQueue.push(addr);
            m_prioritySyncSet.insert(addr);
        }
    } else {
        const int64_t nowMs = esp_timer_get_time() / 1000;
        if (!m_deferredSyncRequests.full()) {
            m_deferredSyncRequests.push_back({addr, nowMs + delayMs});
        }
    }
}

void DaliDeviceRegistry::pollTaskRunner(void* arg) {
    static_cast<DaliDeviceRegistry*>(arg)->pollLoop();
}

[[noreturn]] void DaliDeviceRegistry::pollLoop() {
    ESP_LOGI(TAG, "DALI Polling task running");

    while (true) {
        if (m_scanCommissionActive.load(std::memory_order_relaxed)) {
            vTaskDelay(pdMS_TO_TICKS(200));
            continue;
        }
        const int64_t nowMs = esp_timer_get_time() / 1000;
        handleDeferredNvsFlush(nowMs);

        std::optional<DaliInternalAddr> targetAddr;
        {
            std::lock_guard<std::mutex> lock(m_queueMutex);
            auto it = m_deferredSyncRequests.begin();
            while (it != m_deferredSyncRequests.end()) {
                if (nowMs >= it->executeAtTsMs) {
                    if (!m_prioritySyncSet.contains(it->addr) && !m_prioritySyncQueue.full()) {
                        m_prioritySyncQueue.push(it->addr);
                        m_prioritySyncSet.insert(it->addr);
                    }
                    it = m_deferredSyncRequests.erase(it);
                } else {
                    ++it;
                }
            }

            if (!m_prioritySyncQueue.empty()) {
                targetAddr = m_prioritySyncQueue.front();
                m_prioritySyncQueue.pop();
                m_prioritySyncSet.erase(*targetAddr);
            }
        }

        if (targetAddr.has_value()) {
            pollSingleDevice(*targetAddr);
            vTaskDelay(pdMS_TO_TICKS(15));
            continue;
        }

        DaliInternalAddr rrTarget{};
        bool hasTarget = false;
        size_t totalDevices = 1;

        {
            std::lock_guard<std::mutex> lock(m_registryMutex);
            totalDevices = m_devices.size();
            if (totalDevices > 0) {
                if (m_roundRobinIndex >= totalDevices)
                    m_roundRobinIndex = 0;
                rrTarget = getIdentity(m_devices[m_roundRobinIndex++]).internalAddress;
                hasTarget = true;
            }
        }

        if (hasTarget && rrTarget.isAssigned()) {
            pollSingleDevice(rrTarget);
        }

        constexpr uint32_t pollCycleMs = 300'000;
        const uint32_t count = (totalDevices > 0) ? static_cast<uint32_t>(totalDevices) : 64;
        constexpr uint32_t minDelayMs = 1000;
        constexpr uint32_t maxDelayMs = 15000;
        const uint32_t stepDelayMs = std::clamp(pollCycleMs / count, minDelayMs, maxDelayMs);
        vTaskDelay(pdMS_TO_TICKS(stepDelayMs));
    }
}

esp_err_t DaliDeviceRegistry::removeDevice(const DaliLongAddress_t longAddr) {
    std::lock_guard<std::mutex> lock(m_registryMutex);

    const auto it = std::ranges::find_if(
        m_devices, [longAddr](const DaliDevice& d) { return getIdentity(d).longAddress == longAddr; });

    if (it == m_devices.end())
        return ESP_ERR_NOT_FOUND;

    const auto intAddr = getIdentity(*it).internalAddress;
    size_t mapIdx = (intAddr.bus() * 64) + intAddr.shortAddr();
    if (etl::holds_alternative<InputDevice>(*it)) {
        mapIdx += (BUS_COUNT * 64);
    }
    m_internalToLongMap[mapIdx] = InvalidLongAddr;

    m_groupAssignments.erase(longAddr);
    m_devices.erase(it);

    saveAddressMapToNvs();
    ESP_LOGI(TAG, "Device 0x%06lX manually removed from registry and NVS.", longAddr);
    return ESP_OK;
}
void DaliDeviceRegistry::requestBroadcastSync(const uint32_t baseDelayMs, const uint32_t staggerStepMs) {
    std::lock_guard<std::mutex> lock(m_registryMutex);
    uint32_t currentDelay = baseDelayMs;

    for (const auto& dev : m_devices) {
        if (etl::holds_alternative<ControlGear>(dev)) {
            requestSync(getIdentity(dev).internalAddress, currentDelay);
            currentDelay += staggerStepMs;
        }
    }
}
void DaliDeviceRegistry::pollSingleDevice(const DaliInternalAddr addr) {
    const auto levelOpt = m_bus.query(DaliAddressType::Short, addr.shortAddr(), OpCode::QueryActualLevel);
    const bool isOnline = levelOpt.has_value();

    ControlGear copyGear;
    bool shouldNotify = false;
    bool needFetchMetadata = false;

    {
        std::lock_guard<std::mutex> lock(m_registryMutex);
        for (auto& dev : m_devices) {
            if (getIdentity(dev).internalAddress == addr) {
                const bool availChanged = (getIdentity(dev).available != isOnline);
                getIdentity(dev).available = isOnline;

                if (auto* gear = etl::get_if<ControlGear>(&dev)) {
                    if (isOnline) {
                        const bool levelChanged = (gear->currentLevel != *levelOpt);
                        gear->currentLevel = *levelOpt;
                        if (*levelOpt > 0)
                            gear->lastLevel = *levelOpt;

                        if (!gear->staticDataLoaded) {
                            needFetchMetadata = true;
                        }

                        if (levelChanged || availChanged || gear->initialSyncNeeded) {
                            gear->initialSyncNeeded = false;
                            copyGear = *gear;
                            shouldNotify = true;
                        }
                    } else if (availChanged) {
                        copyGear = *gear;
                        shouldNotify = true;
                    }
                }
                break;
            }
        }
    }

    if (needFetchMetadata) {
        auto [minLevel, maxLevel, powerOnLevel, systemFailureLevel, deviceType, color, gtin] =
            queryDeviceMetadataFromBus(addr.shortAddr());
        {
            std::lock_guard<std::mutex> lock(m_registryMutex);
            for (auto& dev : m_devices) {
                if (getIdentity(dev).internalAddress == addr) {
                    if (auto* gear = etl::get_if<ControlGear>(&dev)) {
                        gear->minLevel = minLevel;
                        gear->maxLevel = maxLevel;
                        gear->powerOnLevel = powerOnLevel;
                        gear->systemFailureLevel = systemFailureLevel;
                        gear->deviceType = deviceType;
                        gear->color = color;
                        gear->gtin = gtin;
                        gear->staticDataLoaded = true;

                        m_nvsDirty = true;
                        m_lastNvsDirtyTsMs = esp_timer_get_time() / 1000;
                    }
                    break;
                }
            }
        }
    }

    if (shouldNotify) {
        notifyDeviceChange(copyGear);
    }
}

void DaliDeviceRegistry::scanBus() {
    ESP_LOGI(TAG, "Starting non-destructive DALI bus scan...");
    ScanCommissionGuard guard(m_scanCommissionActive, m_bus);

    std::bitset<64> seenGears;
    std::bitset<64> seenInputs;

    const uint8_t busId = m_bus.getBusId();

    for (uint8_t sa = 0; sa < 64; ++sa) {
        if (m_bus.query(DaliAddressType::Short, sa, OpCode::QueryStatus).has_value()) {
            seenGears.set(sa);

            const auto existingLa = getLongAddress(DaliInternalAddr(busId, sa), false);
            DaliLongAddress_t longAddr;

            if (existingLa.has_value() && *existingLa != InvalidLongAddr) {
                longAddr = *existingLa;
            } else {
                const auto h = m_bus.query(DaliAddressType::Short, sa, OpCode::QueryRandomAddrH);
                const auto m = m_bus.query(DaliAddressType::Short, sa, OpCode::QueryRandomAddrM);
                const auto l = m_bus.query(DaliAddressType::Short, sa, OpCode::QueryRandomAddrL);
                const uint32_t rawRandom =
                    (h && m && l) ? ((static_cast<uint32_t>(*h) << 16) | (static_cast<uint32_t>(*m) << 8) | (*l))
                                  : 0xFFFFFF;

                if (rawRandom != 0xFFFFFF && rawRandom != 0x000000) {
                    longAddr = rawRandom;
                } else {
                    longAddr = (0xFE0000 | (static_cast<uint32_t>(busId) << 8) | sa);
                }
            }

            {
                std::lock_guard<std::mutex> lock(m_registryMutex);
                auto it = std::ranges::find_if(m_devices, [longAddr](const DaliDevice& dev) {
                    return etl::holds_alternative<ControlGear>(dev) && getIdentity(dev).longAddress == longAddr;
                });

                if (it != m_devices.end()) {
                    auto& gear = etl::get<ControlGear>(*it);
                    const uint8_t oldSa = gear.internalAddress.shortAddr();
                    if (oldSa != sa && oldSa < 64) {
                        m_internalToLongMap[(busId * 64) + oldSa] = InvalidLongAddr;
                    }
                    gear.internalAddress = DaliInternalAddr(busId, sa);
                    gear.available = true;
                } else if (!m_devices.full()) {
                    ControlGear gear;
                    gear.longAddress = longAddr;
                    gear.internalAddress = DaliInternalAddr(busId, sa);
                    gear.available = true;
                    gear.initialSyncNeeded = true;
                    m_devices.push_back(gear);
                }
                m_internalToLongMap[(busId * 64) + sa] = longAddr;
            }
            ESP_LOGI(TAG, "Gear detected at SA %d (LA: 0x%06lX)", sa, longAddr);
        }

        const uint32_t inputQueryFrame = (static_cast<uint32_t>((sa << 1) | 1) << 16) | (0xFE << 8) | 0x30;
        if (m_bus.queryRaw(inputQueryFrame, 24).has_value()) {
            seenInputs.set(sa);
            const DaliLongAddress_t longAddr = 0xFD0000 | sa;

            {
                std::lock_guard<std::mutex> lock(m_registryMutex);
                auto it = std::ranges::find_if(m_devices, [longAddr](const DaliDevice& dev) {
                    return etl::holds_alternative<InputDevice>(dev) && getIdentity(dev).longAddress == longAddr;
                });

                if (it != m_devices.end()) {
                    auto& input = etl::get<InputDevice>(*it);
                    const uint8_t oldSa = input.internalAddress.shortAddr();
                    if (oldSa != sa && oldSa < 64) {
                        m_internalToLongMap[(busId * 64) + oldSa + (BUS_COUNT * 64)] = InvalidLongAddr;
                    }
                    input.internalAddress = DaliInternalAddr(busId, sa);
                    input.available = true;
                } else if (!m_devices.full()) {
                    InputDevice input;
                    input.longAddress = longAddr;
                    input.internalAddress = DaliInternalAddr(busId, sa);
                    input.available = true;
                    m_devices.push_back(input);
                }
                m_internalToLongMap[(busId * 64) + sa + (BUS_COUNT * 64)] = longAddr;
            }
            ESP_LOGI(TAG, "Input Device detected at SA %d", sa);
        }

        vTaskDelay(pdMS_TO_TICKS(15));
    }

    {
        std::lock_guard<std::mutex> lock(m_registryMutex);
        for (auto& dev : m_devices) {
            auto& id = getIdentity(dev);
            if (id.internalAddress.bus() != busId)
                continue;

            const uint8_t sa = id.internalAddress.shortAddr();
            if (etl::holds_alternative<ControlGear>(dev)) {
                if (sa < 64 && !seenGears.test(sa)) {
                    id.available = false;
                }
            } else if (etl::holds_alternative<InputDevice>(dev)) {
                if (sa < 64 && !seenInputs.test(sa)) {
                    id.available = false;
                }
            }
        }
        m_nvsDirty = true;
        m_lastNvsDirtyTsMs = esp_timer_get_time() / 1000;
    }

    saveAddressMapToNvs();
    ESP_LOGI(TAG, "Scan completed. Total devices retained in registry: %zu", m_devices.size());
}

void DaliDeviceRegistry::commissionNewDevices() {
    ESP_LOGI(TAG, "Starting DALI Commissioning (Control Gear)...");
    ScanCommissionGuard guard(m_scanCommissionActive, m_bus);


    m_bus.sendDevice24BitCommand(0xFF, 0x1D, true);
    vTaskDelay(pdMS_TO_TICKS(50));
    m_bus.sendSpecial24BitCommand(0x00, 0x00, false);
    vTaskDelay(pdMS_TO_TICKS(50));
    std::bitset<64> occupiedAddresses;
    for (uint8_t sa = 0; sa < 64; ++sa) {
        if (m_bus.query(DaliAddressType::Short, sa, OpCode::QueryControlGear).has_value()) {
            occupiedAddresses.set(sa);
        }
        vTaskDelay(pdMS_TO_TICKS(5));
    }
    ESP_LOGI(TAG, "Occupied short addresses: %zu/64", occupiedAddresses.count());

    if (occupiedAddresses.all()) {
        ESP_LOGW(TAG, "No free short addresses available on bus (all 64 are occupied).");
        return;
    }

    m_bus.sendSpecialCommand(SpecialOpCode::Terminate, 0, false);
    m_bus.sendSpecialCommand(SpecialOpCode::Terminate, 0, false);

    m_bus.sendSpecialCommand(SpecialOpCode::Initialise, 0xFF, true);
    m_bus.sendSpecialCommand(SpecialOpCode::Randomise, 0, true);

    vTaskDelay(pdMS_TO_TICKS(100));

    uint8_t newlyAssignedCount = 0;
    uint8_t clashRetries = 0;

    while (occupiedAddresses.count() < 64) {
        const uint32_t longAddr = m_bus.findAddressBinarySearch(false);
        if (longAddr == ClashLongAddr) {
            if (++clashRetries <= 3) {
                ESP_LOGW(TAG, "Clash detected (devices share identical random address). Re-randomising...");
                m_bus.sendSpecialCommand(SpecialOpCode::Randomise, 0, true);
                vTaskDelay(pdMS_TO_TICKS(100));
                continue;
            }
            break;
        }

        if (longAddr == InvalidLongAddr) {
            break;
        }

        clashRetries = 0;

        uint8_t targetShortAddr = 64;
        for (uint8_t sa = 0; sa < 64; ++sa) {
            if (!occupiedAddresses.test(sa)) {
                targetShortAddr = sa;
                break;
            }
        }

        if (targetShortAddr >= 64) {
            ESP_LOGW(TAG, "Ran out of short addresses during commissioning.");
            break;
        }

        const uint8_t progByte = static_cast<uint8_t>((targetShortAddr << 1) | 0x01);
        m_bus.sendSpecialCommand(SpecialOpCode::ProgramShortAddr, progByte, true);

        const auto verifyResp = m_bus.querySpecial(SpecialOpCode::VerifyShortAddr, progByte);
        if (verifyResp.has_value()) {
            ESP_LOGI(TAG, "Successfully assigned Short Addr %d to Device 0x%06lX", targetShortAddr, longAddr);
            occupiedAddresses.set(targetShortAddr);
            newlyAssignedCount++;
            m_bus.sendSpecialCommand(SpecialOpCode::Withdraw, 0, false);
        } else {
            ESP_LOGE(TAG, "Verify failed for Short Addr %d (Device 0x%06lX)", targetShortAddr, longAddr);
        }

        vTaskDelay(pdMS_TO_TICKS(10));
    }

    m_bus.sendSpecialCommand(SpecialOpCode::Terminate, 0, false);
    m_bus.sendDevice24BitCommand(0xFF, 0x1E, true);
    vTaskDelay(pdMS_TO_TICKS(50));

    ESP_LOGI(TAG, "Commissioning complete. Newly assigned devices: %u", newlyAssignedCount);
    scanBus();
}

void DaliDeviceRegistry::commission24BitDevices() {
    ESP_LOGI(TAG, "Starting DALI Commissioning (Input Devices)...");
    ScanCommissionGuard guard(m_scanCommissionActive, m_bus);

    m_bus.sendSpecialCommand(SpecialOpCode::Terminate, 0, false);
    vTaskDelay(pdMS_TO_TICKS(50));

    std::bitset<64> occupiedInputAddresses;
    for (uint8_t sa = 0; sa < 64; ++sa) {
        const uint32_t queryFrame = (static_cast<uint32_t>((sa << 1) | 1) << 16) | (0xFE << 8) | 0x30;
        if (m_bus.queryRaw(queryFrame, 24).has_value()) {
            occupiedInputAddresses.set(sa);
        }
        vTaskDelay(pdMS_TO_TICKS(5));
    }
    ESP_LOGI(TAG, "Occupied Input Device addresses: %zu/64", occupiedInputAddresses.count());

    if (occupiedInputAddresses.all()) {
        ESP_LOGW(TAG, "All 64 input device addresses are occupied.");
        return;
    }

    m_bus.sendSpecial24BitCommand(0x00, 0x00, false); // Terminate
    m_bus.sendSpecial24BitCommand(0x01, 0x00, true);
    m_bus.sendSpecial24BitCommand(0x02, 0x00, true);  // Randomise (Send Twice)

    vTaskDelay(pdMS_TO_TICKS(100));

    uint8_t newlyAssignedCount = 0;
    uint8_t clashRetries = 0;

    while (occupiedInputAddresses.count() < 64) {
        const uint32_t longAddr = m_bus.findAddressBinarySearch(true);

        if (longAddr == ClashLongAddr) {
            if (++clashRetries <= 3) {
                ESP_LOGW(TAG, "Input device clash detected. Re-randomising...");
                m_bus.sendSpecial24BitCommand(0x02, 0x00, true);
                vTaskDelay(pdMS_TO_TICKS(100));
                continue;
            }
            break;
        }

        if (longAddr == InvalidLongAddr) {
            break;
        }
        clashRetries = 0;

        uint8_t targetShortAddr = 64;
        for (uint8_t sa = 0; sa < 64; ++sa) {
            if (!occupiedInputAddresses.test(sa)) {
                targetShortAddr = sa;
                break;
            }
        }

        if (targetShortAddr >= 64)
            break;

        const uint8_t progByte = targetShortAddr;
        m_bus.sendSpecial24BitCommand(0x08, progByte, true);
        const uint32_t verifyFrame = (0xC1U << 16) | (0x09U << 8) | progByte;
        const auto verifyResp = m_bus.queryRaw(verifyFrame, 24);

        if (verifyResp.has_value()) {
            ESP_LOGI(TAG, "Assigned Short Addr %d to Input Device 0x%06lX", targetShortAddr, longAddr);
            occupiedInputAddresses.set(targetShortAddr);
            newlyAssignedCount++;
            m_bus.sendSpecial24BitCommand(0x04, 0x00, false);
        } else {
            ESP_LOGE(TAG, "Verify failed for Input Device 0x%06lX", longAddr);
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    m_bus.sendSpecial24BitCommand(0x00, 0x00, false);
    ESP_LOGI(TAG, "Input Device Commissioning complete. Newly assigned: %u", newlyAssignedCount);

    scanBus();
}

etl::vector<DaliDevice, DaliDeviceRegistry::MAX_TOTAL_DEVICES> DaliDeviceRegistry::getDevicesSnapshot() const {
    std::lock_guard<std::mutex> lock(m_registryMutex);
    return m_devices;
}

bool DaliDeviceRegistry::loadAddressMapFromNvs() {
    NvsHandle nvs(NVS_NAMESPACE, NVS_READONLY);
    if (!nvs)
        return false;

    size_t requiredSize = 0;
    if (nvs_get_blob(nvs.get(), NVS_MAP_KEY, nullptr, &requiredSize) != ESP_OK || requiredSize == 0) {
        return false;
    }

    std::lock_guard<std::mutex> lock(m_registryMutex);

    size_t maxBytes = m_nvsBlobScratchpad.size() * sizeof(AddressMapBlobItem);
    size_t bytesToRead = std::min(requiredSize, maxBytes);

    if (nvs_get_blob(nvs.get(), NVS_MAP_KEY, m_nvsBlobScratchpad.data(), &bytesToRead) != ESP_OK) {
        return false;
    }

    const size_t count = bytesToRead / sizeof(AddressMapBlobItem);
    m_devices.clear();
    m_internalToLongMap.fill(InvalidLongAddr);

    for (size_t i = 0; i < count; ++i) {
        const auto& item = m_nvsBlobScratchpad[i];
        DaliInternalAddr addr(item.internalAddress);
        size_t mapIdx = (addr.bus() * 64) + addr.shortAddr();
        if (!item.isInput && item.groupMask != 0) {
            m_groupAssignments[item.longAddress] = GroupMask(item.groupMask);
        }
        if (item.isInput) {
            InputDevice dev;
            dev.longAddress = item.longAddress;
            dev.internalAddress = addr;
            dev.gtin = item.gtin;
            dev.available = false;
            m_devices.push_back(dev);
            m_internalToLongMap[mapIdx + (BUS_COUNT * 64)] = item.longAddress;
        } else {
            ControlGear gear;
            gear.longAddress = item.longAddress;
            gear.internalAddress = addr;
            gear.gtin = item.gtin;
            gear.minLevel = item.minLevel;
            gear.maxLevel = item.maxLevel;
            gear.powerOnLevel = item.powerOnLevel;
            gear.systemFailureLevel = item.systemFailureLevel;
            if (item.deviceType != 0xFF)
                gear.deviceType = item.deviceType;
            if (item.supportsRgb || item.supportsTc) {
                gear.color = ColorFeatures{.supportsRgb = item.supportsRgb, .supportsTc = item.supportsTc};
            }
            gear.staticDataLoaded = true;
            gear.available = false;
            m_devices.push_back(gear);
            m_internalToLongMap[mapIdx] = item.longAddress;
        }
    }

    ESP_LOGI(TAG, "Loaded %zu devices from NVS cache", m_devices.size());
    return true;
}

esp_err_t DaliDeviceRegistry::saveAddressMapToNvs() {
    const NvsHandle nvs(NVS_NAMESPACE, NVS_READWRITE);
    if (!nvs)
        return ESP_FAIL;

    size_t count = 0;
    {
        std::lock_guard<std::mutex> lock(m_registryMutex);
        for (const auto& dev : m_devices) {
            if (count >= m_nvsBlobScratchpad.size())
                break;

            AddressMapBlobItem& item = m_nvsBlobScratchpad[count];
            item = {};

            const auto& id = getIdentity(dev);
            item.longAddress = id.longAddress;
            item.internalAddress = id.internalAddress.value;
            strncpy(item.gtin, id.gtin.c_str(), sizeof(item.gtin) - 1);

            auto grpIt = m_groupAssignments.find(id.longAddress);
            if (grpIt != m_groupAssignments.end()) {
                item.groupMask = static_cast<uint16_t>(grpIt->second.to_ulong());
            } else {
                item.groupMask = 0;
            }

            if (const auto* gear = etl::get_if<ControlGear>(&dev)) {
                item.isInput = false;
                item.deviceType = gear->deviceType.value_or(0xFF);
                item.minLevel = gear->minLevel;
                item.maxLevel = gear->maxLevel;
                item.powerOnLevel = gear->powerOnLevel;
                item.systemFailureLevel = gear->systemFailureLevel;
                if (gear->color.has_value()) {
                    item.supportsRgb = gear->color->supportsRgb;
                    item.supportsTc = gear->color->supportsTc;
                }
            } else {
                item.isInput = true;
                item.deviceType = 0xFF;
            }
            count++;
        }
    }

    esp_err_t err =
        nvs_set_blob(nvs.get(), NVS_MAP_KEY, m_nvsBlobScratchpad.data(), count * sizeof(AddressMapBlobItem));
    if (err == ESP_OK) {
        err = nvs_commit(nvs.get());
    }
    return err;
}

void DaliDeviceRegistry::handleDeferredNvsFlush(const int64_t nowMs) {
    constexpr int64_t FLUSH_DEBOUNCE_MS = 60'000;
    if (m_nvsDirty && (nowMs - m_lastNvsDirtyTsMs > FLUSH_DEBOUNCE_MS)) {
        m_nvsDirty = false;
        saveAddressMapToNvs();
    }
}
StaticMetadata DaliDeviceRegistry::queryDeviceMetadataFromBus(const uint8_t sa) const {
    StaticMetadata meta{};

    const auto minOpt = m_bus.query(DaliAddressType::Short, sa, OpCode::QueryMinLevel);
    const auto maxOpt = m_bus.query(DaliAddressType::Short, sa, OpCode::QueryMaxLevel);
    const auto pOnOpt = m_bus.query(DaliAddressType::Short, sa, OpCode::QueryPowerOnLevel);
    const auto sysFailOpt = m_bus.query(DaliAddressType::Short, sa, OpCode::QuerySystemFailureLevel);
    const auto dtOpt = m_bus.query(DaliAddressType::Short, sa, OpCode::QueryDeviceType);

    if (minOpt)
        meta.minLevel = *minOpt;
    if (maxOpt)
        meta.maxLevel = *maxOpt;
    if (pOnOpt)
        meta.powerOnLevel = *pOnOpt;
    if (sysFailOpt)
        meta.systemFailureLevel = *sysFailOpt;
    if (dtOpt)
        meta.deviceType = *dtOpt;

    const uint8_t dt = meta.deviceType.value_or(0xFF);
    if (dt == 8 || dt == 255) {
        m_bus.sendSpecialCommand(SpecialOpCode::EnableDeviceTypeX, 8);
        auto colourTypeOpt = m_bus.query(DaliAddressType::Short, sa, static_cast<OpCode>(DT8OpCode::QueryColourType));

        if (!colourTypeOpt.has_value()) {
            colourTypeOpt = m_bus.readMemoryLocation(sa, 205, 0x09);
        }

        if (colourTypeOpt.has_value()) {
            const uint8_t val = *colourTypeOpt;
            ColorFeatures cf;
            cf.supportsTc = (val & 0x02) != 0;
            cf.supportsRgb = (val & 0xE0) != 0 || (val & 0x08) != 0;
            if (cf.supportsTc || cf.supportsRgb) {
                meta.color = cf;
                meta.deviceType = 8;
            }
        }
    }

    uint8_t gtinRaw[6] = {0};
    if (m_bus.readMemoryBlock(sa, 0, 0x03, gtinRaw, sizeof(gtinRaw))) {
        char buf[13];
        snprintf(buf, sizeof(buf), "%02X%02X%02X%02X%02X%02X", gtinRaw[0], gtinRaw[1], gtinRaw[2], gtinRaw[3],
                 gtinRaw[4], gtinRaw[5]);

        etl::string<16> gtinStr(buf);
        if (gtinStr != "FFFFFFFFFFFF" && gtinStr != "000000000000") {
            meta.gtin = gtinStr;
        }
    }

    return meta;
}
} // namespace daliMQTT