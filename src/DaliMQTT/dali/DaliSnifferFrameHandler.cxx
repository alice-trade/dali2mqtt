// Copyright (c) 2026 Alice-Trade Inc.
// SPDX-License-Identifier: GPL-2.0-or-later

#include "dali/DaliDeviceController.hxx"
#include <dali/DaliGroupManagement.hxx>
#include <mqtt/MQTTClient.hxx>
#include "utils/DaliLongAddrConversions.hxx"

namespace daliMQTT {
    static constexpr char TAG[] = "DaliSnifferFrameHandler";
    void DaliDeviceController::SnifferProcessFrame(const dali_frame_t& frame) {
        if (frame.is_backward_frame) {
            ESP_LOGV(TAG, "Sniffed backward frame 0x%02X", (uint8_t)(frame.data & 0xFF));
            return;
        }
        const uint8_t addr_byte = (frame.data >> 8) & 0xFF;
        const uint8_t data_byte = frame.data & 0xFF;

        if ((addr_byte & 0xE0) == 0xA0 || (addr_byte & 0xE0) == 0xC0) {
            ESP_LOGV(TAG, "Sniffed Special Command: 0x%02X, ignoring for state sync", addr_byte);
            return;
        }

        const bool is_command = (addr_byte & 0x01);
        const auto cmd = static_cast<Commands::OpCode>(data_byte);

        bool is_broadcast = (addr_byte == 0xFE || addr_byte == 0xFF);
        std::optional<uint8_t> target_group_id = std::nullopt;
        std::optional<uint8_t> target_short_addr = std::nullopt;

        if (!is_broadcast) {
            if ((addr_byte & 0x80) == 0x80) {
                target_group_id = (addr_byte >> 1) & 0x0F;
            } else {
                target_short_addr = (addr_byte >> 1) & 0x3F;
            }
        }
        std::lock_guard<std::mutex> lock(m_devices_mutex);

        if (target_group_id.has_value()) {
            auto& group_mgr = DaliGroupManagement::Instance();
            const uint8_t gid = *target_group_id;

            if (!is_command) {
                group_mgr.updateGroupState(frame.bus_id, gid, {.level = data_byte});
            } else {
                using enum daliMQTT::Commands::OpCode;
                switch (cmd) {
                    case Off:
                    case StepDownAndOff: group_mgr.updateGroupState(frame.bus_id, gid, {.level = 0}); break;
                    case RecallMaxLevel: group_mgr.updateGroupState(frame.bus_id, gid, {.level = 254}); break;
                    case RecallMinLevel: group_mgr.updateGroupState(frame.bus_id, gid, {.level = 1}); break;
                    case OnAndStepUp:    group_mgr.restoreGroupLevel(frame.bus_id, gid); break;
                    case Up:
                    case StepUp:         group_mgr.stepGroupLevel(frame.bus_id, gid, true); break;
                    case Down:
                    case StepDown:       group_mgr.stepGroupLevel(frame.bus_id, gid, false); break;
                    default: break;
                }
            }
        }

        bool needs_sync = false;
        std::vector<DaliInternalAddr> sync_candidates;

        for (auto& dev_var : m_devices) {
            auto* gear = std::get_if<ControlGear>(&dev_var);
            if (!gear) continue;
            if (gear->internal_address.bus() != frame.bus_id) continue;

            bool is_affected = is_broadcast;
            if (!is_affected && target_group_id.has_value()) {
                auto grps = DaliGroupManagement::Instance().getGroupsForDevice(gear->long_address);
                if (grps && grps->test(*target_group_id)) is_affected = true;
            }
            if (!is_affected && target_short_addr.has_value()) {
                if (gear->internal_address.shortAddr() == *target_short_addr) is_affected = true;
            }
            if (!is_affected) continue;
            std::optional<uint8_t> next_level = std::nullopt;

            if (!is_command) {
                next_level = data_byte; // DACP
            } else {
                using enum daliMQTT::Commands::OpCode;
                switch (cmd) {
                    case Off:
                    case StepDownAndOff: next_level = 0; break;
                    case RecallMaxLevel: next_level = 254; break;
                    case RecallMinLevel: next_level = 1; break;
                    case OnAndStepUp: next_level = (gear->current_level == 0) ? gear->last_level : gear->current_level; break;
                    case static_cast<Commands::OpCode>(Commands::DT8OpCode::Activate):
                    case static_cast<Commands::OpCode>(Commands::DT8OpCode::SetTempTc):
                    case static_cast<Commands::OpCode>(Commands::DT8OpCode::SetTempRGB):
                        needs_sync = true;
                        break;
                    default:
                        if (data_byte >= 0x10 || data_byte <= 0x09) needs_sync = true;
                        break;
                }
            }

            if (next_level.has_value()) {
                procUpdateDeviceState(gear->long_address, {.level = *next_level});
            }
            if (needs_sync) sync_candidates.push_back(gear->internal_address);
        }

        if (needs_sync && !sync_candidates.empty()) {
            uint32_t delay = 400;
            for (DaliInternalAddr internal_addr : sync_candidates) {
                requestDeviceSync(internal_addr, delay);
                delay += 150;
            }
        }
    }
}
