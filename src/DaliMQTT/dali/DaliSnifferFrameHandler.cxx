#include "DaliDeviceController.hxx"
#include <DaliGroupManagement.hxx>

namespace daliMQTT {
    static constexpr char TAG[] = "DaliSnifferFrameHandler";
    void DaliDeviceController::processDaliFrame(const dali_frame_t& frame) {
        if (frame.is_backward_frame) {
            ESP_LOGD(TAG, "Process sniffed backward frame 0x%02X", frame.data & 0xFF);
            return;
        }
        ESP_LOGD(TAG, "Sniffed forward frame 0x%04X", frame.data);

        uint8_t addr_byte = (frame.data >> 8) & 0xFF;
        uint8_t cmd_byte = frame.data & 0xFF;

        std::vector<DaliLongAddress_t> affected_devices;
        dali_addressType_t addr_type;

        // DACP (Direct Arc Power Control)
        if ((addr_byte & 0x01) == 0) {
            if ((addr_byte & 0x80) == 0) { // Short Address
                addr_type = DALI_ADDRESS_TYPE_SHORT;
                uint8_t short_addr = (addr_byte >> 1) & 0x3F;
                if (auto long_addr_opt = getLongAddress(short_addr)) {
                    affected_devices.push_back(*long_addr_opt);
                }
            } else if ((addr_byte & 0xE0) == 0x80) { // Group Address
                addr_type = DALI_ADDRESS_TYPE_GROUP;
                uint8_t group_addr = (addr_byte >> 1) & 0x0F;
                auto all_assignments = DaliGroupManagement::getInstance().getAllAssignments();
                for (const auto& [long_addr, groups] : all_assignments) {
                    if (groups.test(group_addr)) affected_devices.push_back(long_addr);
                }
            } else if (addr_byte == 0xFE) { // Broadcast
                addr_type = DALI_ADDRESS_TYPE_BROADCAST;
                auto all_devices = getDevices();
                for (const auto& long_addr : all_devices | std::views::keys) {
                    affected_devices.push_back(long_addr);
                }
            }
        }
        else {
            if ((addr_byte & 0x80) == 0) { // Short Address
                addr_type = DALI_ADDRESS_TYPE_SHORT;
                uint8_t short_addr = (addr_byte >> 1) & 0x3F;
                if (auto long_addr_opt = getLongAddress(short_addr)) {
                    affected_devices.push_back(*long_addr_opt);
                }
            } else if ((addr_byte & 0xE0) == 0x80) { // Group Address
                addr_type = DALI_ADDRESS_TYPE_GROUP;
                uint8_t group_addr = (addr_byte >> 1) & 0x0F;
                auto all_assignments = DaliGroupManagement::getInstance().getAllAssignments();
                for (const auto& [long_addr, groups] : all_assignments) {
                    if (groups.test(group_addr)) affected_devices.push_back(long_addr);
                }
            } else if ((addr_byte & 0xFF) == 0xFF) { // Broadcast
                addr_type = DALI_ADDRESS_TYPE_BROADCAST;
                auto all_devices = getDevices();
                for (const auto& long_addr : all_devices | std::views::keys) {
                    affected_devices.push_back(long_addr);
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
            switch (cmd_byte) {
                case DALI_COMMAND_OFF:
                case DALI_COMMAND_STEP_DOWN_AND_OFF:
                    known_level = 0;
                    break;
                    // Should be recalled
                case DALI_COMMAND_UP:
                case DALI_COMMAND_DOWN:
                case DALI_COMMAND_STEP_UP:
                case DALI_COMMAND_STEP_DOWN:
                case DALI_COMMAND_RECALL_MAX_LEVEL:
                case DALI_COMMAND_RECALL_MIN_LEVEL:
                case DALI_COMMAND_ON_AND_STEP_UP:
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
            ESP_LOGD(TAG, "Sniffer: Querying state for %zu devices due to command 0x%02X.", affected_devices.size(), cmd_byte);
            auto& dali = DaliAPI::getInstance();
            for (const auto& long_addr : affected_devices) {
                if (auto short_addr_opt = getShortAddress(long_addr)) {
                    vTaskDelay(pdMS_TO_TICKS(5));
                    if (auto level_opt = dali.sendQuery(DALI_ADDRESS_TYPE_SHORT, *short_addr_opt, DALI_COMMAND_QUERY_ACTUAL_LEVEL)) {
                        updateDeviceState(long_addr, *level_opt);
                    }
                }
            }
        }
    }
}