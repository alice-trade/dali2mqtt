#include <ConfigManager.hxx>
#include "DaliDeviceController.hxx"
#include <DaliGroupManagement.hxx>
#include <MQTTClient.hxx>
#include "utils/DaliLongAddrConversions.hxx"

namespace daliMQTT {
    static constexpr char TAG[] = "DaliSnifferFrameHandler";
    void DaliDeviceController::SnifferProcessFrame(const dali_frame_t& frame) {
        if (frame.is_backward_frame) {
            ESP_LOGD(TAG, "Process sniffed backward frame 0x%02X", frame.data & 0xFF);
            return;
        }
        ESP_LOGD(TAG, "Sniffed forward frame 0x%04X", frame.data);

        uint8_t addr_byte = (frame.data >> 8) & 0xFF;
        uint8_t cmd_byte = frame.data & 0xFF;

        std::vector<DaliLongAddress_t> affected_devices;
        std::optional<uint8_t> target_group_id = std::nullopt;

        if ((addr_byte & 0x01) == 0) {               // DACP (Direct Arc Power Control)
            if ((addr_byte & 0x80) == 0) {           // Short Address
                uint8_t short_addr = (addr_byte >> 1) & 0x3F;
                if (auto long_addr_opt = getLongAddress(short_addr)) {
                    affected_devices.push_back(*long_addr_opt);
                }
            } else if ((addr_byte & 0xE0) == 0x80) { // Group Address
                target_group_id = (addr_byte >> 1) & 0x0F;
            } else if (addr_byte == 0xFE) {          // Broadcast
                auto all_devices = getDevices();
                for (const auto& long_addr : all_devices | std::views::keys) {
                    affected_devices.push_back(long_addr);
                }
            }
        }
        else {                                       // Command
            if ((addr_byte & 0x80) == 0) {           // Short Address
                uint8_t short_addr = (addr_byte >> 1) & 0x3F;
                if (auto long_addr_opt = getLongAddress(short_addr)) {
                    affected_devices.push_back(*long_addr_opt);
                }
            } else if ((addr_byte & 0xE0) == 0x80) { // Group Address
                target_group_id = (addr_byte >> 1) & 0x0F;
            } else if ((addr_byte & 0xFF) == 0xFF) { // Broadcast
                auto all_devices = getDevices();
                for (const auto& long_addr : all_devices | std::views::keys) {
                    affected_devices.push_back(long_addr);
                }
            }
        }

        if (target_group_id.has_value()) {
            auto all_assignments = DaliGroupManagement::getInstance().getAllAssignments();
            for (const auto& [long_addr, groups] : all_assignments) {
                if (groups.test(target_group_id.value())) {
                    affected_devices.push_back(long_addr);
                }
            }
        }

        if (target_group_id.has_value()) {
            auto& group_mgr = DaliGroupManagement::getInstance();
            const uint8_t gid = target_group_id.value();

            if ((addr_byte & 0x01) == 0) { // DACP
                group_mgr.updateGroupState(gid, cmd_byte);
            } else {                       // Command
                switch (cmd_byte) {
                case DALI_COMMAND_OFF:
                case DALI_COMMAND_STEP_DOWN_AND_OFF:
                    group_mgr.updateGroupState(gid, 0);
                    break;
                case DALI_COMMAND_RECALL_MAX_LEVEL:
                    group_mgr.updateGroupState(gid, 254);
                    break;
                case DALI_COMMAND_RECALL_MIN_LEVEL:
                    group_mgr.updateGroupState(gid, 1);
                    break;
                case DALI_COMMAND_ON_AND_STEP_UP:
                    group_mgr.restoreGroupLevel(gid);
                    break;
                case DALI_COMMAND_UP:
                case DALI_COMMAND_STEP_UP:
                    group_mgr.stepGroupLevel(gid, true);
                    break;
                case DALI_COMMAND_DOWN:
                case DALI_COMMAND_STEP_DOWN:
                    group_mgr.stepGroupLevel(gid, false);
                    break;
                default: break; // TODO: Other cmd
                }
            }
        }

        if (affected_devices.empty()) {
            return;
        }

        std::optional<uint8_t> known_level;
        bool needs_query = false;

        if ((addr_byte & 0x01) == 0) { // DACP
            known_level = cmd_byte;
        } else { // Command
            switch (cmd_byte)
            {
            case DALI_COMMAND_OFF:
            case DALI_COMMAND_STEP_DOWN_AND_OFF:
                known_level = 0;
                break;
            case DALI_COMMAND_ON_AND_STEP_UP:
                {
                    std::vector<std::pair<DaliLongAddress_t, uint8_t>> optimistic_updates;
                    bool any_requires_query = false;

                    {
                        std::lock_guard lock(m_devices_mutex);
                        for (const auto& long_addr : affected_devices) {
                            if (auto it = m_devices.find(long_addr); it != m_devices.end()) {
                                if (it->second.current_level == 0) {
                                    uint8_t target = (it->second.last_level > 0) ? it->second.last_level : 254;
                                    optimistic_updates.emplace_back(long_addr, target);
                                } else {
                                    any_requires_query = true;
                                }
                            }
                        }
                    }

                    for (const auto& [addr, lvl] : optimistic_updates) {
                        ESP_LOGD(TAG, "Sniffer: Optimistic ON_AND_STEP_UP for %s to level %d",
                                 utils::longAddressToString(addr).data(), lvl);
                        updateDeviceState(addr, lvl);
                    }

                    if (any_requires_query) {
                        needs_query = true;
                    }
                    break;
                }
                case DALI_COMMAND_RECALL_MAX_LEVEL:
                    known_level = 254;
                    break;
                case DALI_COMMAND_RECALL_MIN_LEVEL:
                    known_level = 1;
                    break;
                // Should be recalled
                case DALI_COMMAND_UP:
                case DALI_COMMAND_DOWN:
                case DALI_COMMAND_STEP_UP:
                case DALI_COMMAND_STEP_DOWN:
                case DALI_COMMAND_GO_TO_SCENE_0:
                case DALI_COMMAND_GO_TO_SCENE_1:
                case DALI_COMMAND_GO_TO_SCENE_2:
                case DALI_COMMAND_GO_TO_SCENE_3:
                case DALI_COMMAND_GO_TO_SCENE_4:
                case DALI_COMMAND_GO_TO_SCENE_5:
                case DALI_COMMAND_GO_TO_SCENE_6:
                case DALI_COMMAND_GO_TO_SCENE_7:
                case DALI_COMMAND_GO_TO_SCENE_8:
                case DALI_COMMAND_GO_TO_SCENE_9:
                case DALI_COMMAND_GO_TO_SCENE_10:
                case DALI_COMMAND_GO_TO_SCENE_11:
                case DALI_COMMAND_GO_TO_SCENE_12:
                case DALI_COMMAND_GO_TO_SCENE_13:
                case DALI_COMMAND_GO_TO_SCENE_14:
                case DALI_COMMAND_GO_TO_SCENE_15:
                    needs_query = true;
                    break;
                default:
                    return;
            }
        }

        if (known_level.has_value()) {
            ESP_LOGD(TAG, "Sniffer: Applying known level %d to %zu devices.", *known_level, affected_devices.size());
            for (const auto& long_addr : affected_devices) {
                updateDeviceState(long_addr, *known_level);
            }
        } else if (needs_query) {
            uint32_t current_delay_ms = 400;
            constexpr uint32_t stagger_step_ms = 150;

            // if (is_group_or_broadcast) {
            //     current_delay_ms = 600;
            // }

            ESP_LOGD(TAG, "Sniffer: Scheduling sync for %zu devices (Base delay: %u ms)", affected_devices.size(), current_delay_ms);

            for (const auto& long_addr : affected_devices) {
                auto short_addr_opt = getShortAddress(long_addr);

                if (short_addr_opt.has_value()) {
                    requestDeviceSync(short_addr_opt.value(), current_delay_ms);
                    current_delay_ms += stagger_step_ms;
                }
            }
        }
    }
}
