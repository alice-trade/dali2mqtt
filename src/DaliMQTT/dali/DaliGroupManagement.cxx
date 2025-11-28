#include "DaliGroupManagement.hxx"

#include <utils/StringUtils.hxx>

#include "ConfigManager.hxx"
#include "DaliDeviceController.hxx"
#include "DaliAPI.hxx"
#include "MQTTClient.hxx"
#include "utils/DaliLongAddrConversions.hxx"

namespace daliMQTT
{
    static constexpr char   TAG[] = "DaliGroupManagement";

    void DaliGroupManagement::init() {
        ESP_LOGI(TAG, "Initializing DALI Group Manager...");
        loadFromConfig();
    }

    void DaliGroupManagement::loadFromConfig() {
        std::lock_guard<std::mutex> lock(m_mutex);
        const auto& config = ConfigManager::getInstance().getConfig();
        m_assignments.clear();

        cJSON* root = cJSON_Parse(config.dali_group_assignments.c_str());
        if (!cJSON_IsObject(root)) {
            ESP_LOGW(TAG, "No valid group assignments found in NVS or JSON is invalid. Starting fresh.");
            cJSON_Delete(root);
            return;
        }

        cJSON* device_item = nullptr;
        cJSON_ArrayForEach(device_item, root) {
            auto long_addr_opt = utils::stringToLongAddress(device_item->string);
            if (!long_addr_opt) {
                ESP_LOGW(TAG, "Skipping invalid key '%s' in DALI group assignments JSON.", device_item->string);
                continue;
            }
            DaliLongAddress_t long_address = *long_addr_opt;

            std::bitset<16> groups;
            if (cJSON_IsArray(device_item)) {
                cJSON* group_item = nullptr;
                cJSON_ArrayForEach(group_item, device_item) {
                    if (cJSON_IsNumber(group_item) && group_item->valueint >= 0 && group_item->valueint < 16) {
                        groups.set(group_item->valueint);
                    }
                }
            }
            m_assignments[long_address] = groups;
        }

        cJSON_Delete(root);
        ESP_LOGI(TAG, "Loaded %zu device group assignments from NVS.", m_assignments.size());
    }

    esp_err_t DaliGroupManagement::saveToConfig() {
        cJSON* root = nullptr;
        char* json_string = nullptr;

        {
            std::lock_guard lock(m_mutex);
            root = cJSON_CreateObject();
            if (!root) return ESP_ERR_NO_MEM;

            for (const auto& [addr, groups] : m_assignments) {
                const auto addr_str = utils::longAddressToString(addr);
                cJSON* group_array = cJSON_CreateArray();
                for (int i = 0; i < 16; ++i) {
                    if (groups.test(i)) {
                        cJSON_AddItemToArray(group_array, cJSON_CreateNumber(i));
                    }
                }
                cJSON_AddItemToObject(root, addr_str.data(), group_array);
            }
        }

        json_string = cJSON_PrintUnformatted(root);
        cJSON_Delete(root);
        if (!json_string) {
            return ESP_ERR_NO_MEM;
        }

        const esp_err_t err = ConfigManager::getInstance().saveDaliGroupAssignments(json_string);
        free(json_string);
        return err;
    }

    GroupAssignments DaliGroupManagement::getAllAssignments() const {
        std::lock_guard lock(m_mutex);
        return m_assignments;
    }

    std::optional<std::bitset<16>> DaliGroupManagement::getGroupsForDevice(const DaliLongAddress_t longAddress) const {
        std::lock_guard lock(m_mutex);
        if (const auto it = m_assignments.find(longAddress); it != m_assignments.end()) {
            return it->second;
        }
        return std::nullopt;
    }

    esp_err_t DaliGroupManagement::setGroupMembership(DaliLongAddress_t longAddress, uint8_t group, bool assigned) {
        if (group >= 16) return ESP_ERR_INVALID_ARG;

        auto short_address_opt = DaliDeviceController::getInstance().getShortAddress(longAddress);
        if (!short_address_opt) {
            ESP_LOGE(TAG, "Cannot set group membership: device with long address %lX not found on bus.", longAddress);
            return ESP_ERR_NOT_FOUND;
        }
        uint8_t shortAddress = *short_address_opt;

        std::lock_guard lock(m_mutex);
        m_assignments[longAddress].set(group, assigned);
        
        auto& dali = DaliAPI::getInstance();
        esp_err_t result;

        if (assigned) {
            ESP_LOGD(TAG, "Assigning device %d to group %d", shortAddress, group);
            result = dali.assignToGroup(shortAddress, group);
        } else {
            ESP_LOGD(TAG, "Removing device %d from group %d", shortAddress, group);
            result = dali.removeFromGroup(shortAddress, group);
        }

        if (result == ESP_OK) {
            return saveToConfig();
        }

        m_assignments[longAddress].set(group, !assigned);
        return result;
    }

    esp_err_t DaliGroupManagement::setAllAssignments(const GroupAssignments& newAssignments) {
        struct CommandToSend {
            uint8_t short_address;
            uint8_t group;
            bool assign;
        };
        std::vector<CommandToSend> commands_to_send;
        {
            std::lock_guard lock(m_mutex);
            std::set<DaliLongAddress_t> all_addresses;
            for(const auto& long_addr : m_assignments | std::views::keys) all_addresses.insert(long_addr);
            for(const auto& long_addr : newAssignments | std::views::keys) all_addresses.insert(long_addr);

            for (const auto& long_addr : all_addresses) {
                std::bitset<16> old_groups = m_assignments.contains(long_addr) ? m_assignments.at(long_addr) : std::bitset<16>();
                std::bitset<16> new_groups = newAssignments.contains(long_addr) ? newAssignments.at(long_addr) : std::bitset<16>();

                if (old_groups == new_groups) continue;
                
                auto short_addr_opt = DaliDeviceController::getInstance().getShortAddress(long_addr);
                if (!short_addr_opt) continue;
                std::bitset<16> changed_bits = old_groups ^ new_groups;
                for (uint8_t i = 0; i < 16; ++i) {
                    if (changed_bits.test(i)) {
                        commands_to_send.push_back({*short_addr_opt, i, new_groups.test(i)});
                    }
                }
            }
            m_assignments = newAssignments;
        }

        auto& dali = DaliAPI::getInstance();
        for (const auto& [short_address, group, assign] : commands_to_send) {
            if (assign) {
                ESP_LOGI(TAG, "Sync: Adding device %d to group %d", short_address, group);
                dali.assignToGroup(short_address, group);
            } else {
                ESP_LOGI(TAG, "Sync: Removing device %d from group %d", short_address, group);
                dali.removeFromGroup(short_address, group);
            }
            vTaskDelay(pdMS_TO_TICKS(CONFIG_DALI2MQTT_DALI_INTER_FRAME_DELAY_MS));
        }
        return saveToConfig();
    }

    esp_err_t DaliGroupManagement::refreshAssignmentsFromBus() {
        ESP_LOGI(TAG, "Refreshing group assignments from DALI bus...");

        auto& device_controller = DaliDeviceController::getInstance();
        auto devices = device_controller.getDevices();
        if (devices.empty()) {
            ESP_LOGW(TAG, "No devices found to refresh group assignments.");
            return ESP_OK;
        }

        GroupAssignments new_assignments;
        auto& dali = DaliAPI::getInstance();

        for (const auto& [long_addr, device] : devices) {
            if (!device.is_present) continue;

            if (auto groups_opt = dali.getDeviceGroups(device.short_address)) {
                new_assignments[long_addr] = *groups_opt;
                ESP_LOGD(TAG, "Device %s (SA %d) has group mask: %s",
                         utils::longAddressToString(long_addr).data(),
                         device.short_address,
                         groups_opt->to_string().c_str());
            } else {
                ESP_LOGW(TAG, "Failed to get group info for device %s (SA %d)",
                         utils::longAddressToString(long_addr).data(),
                         device.short_address);
            }
            vTaskDelay(pdMS_TO_TICKS(CONFIG_DALI2MQTT_DALI_INTER_FRAME_DELAY_MS));
        }
        {
            std::lock_guard lock(m_mutex);
            m_assignments = new_assignments;
            ESP_LOGI(TAG, "Finished refreshing group assignments. Found assignments for %zu devices.", m_assignments.size());
        }
        return saveToConfig();
    }
    DaliGroup DaliGroupManagement::getGroupState(const uint8_t group_id) const {
        std::lock_guard lock(m_mutex);
        if (group_id < 16) {
            return m_group_states[group_id];
        }
        return DaliGroup{};
    }

    void DaliGroupManagement::updateGroupState(const uint8_t group_id, const uint8_t level) {
        if (group_id >= 16) return;

        bool changed = false;
        {
            std::lock_guard lock(m_mutex);
            auto& group = m_group_states[group_id];

            if (level > 0) {
                group.last_level = level;
            }

            if (group.current_level != level) {
                group.current_level = level;
                changed = true;
                ESP_LOGI(TAG, "Group %d state updated: %d", group_id, level);
            }
        }

        if (changed) {
            publishGroupState(group_id, level);
        }
    }

    void DaliGroupManagement::restoreGroupLevel(const uint8_t group_id) {
        if (group_id >= 16) return;
        uint8_t target;
        {
            std::lock_guard lock(m_mutex);
            target = (m_group_states[group_id].last_level > 0)
                     ? m_group_states[group_id].last_level
                     : 254;
        }
        updateGroupState(group_id, target);
    }

    void DaliGroupManagement::publishGroupState(uint8_t group_id, uint8_t level) const {
        auto const& mqtt = MQTTClient::getInstance();
        const auto config = ConfigManager::getInstance().getConfig();

        // Topic: base/light/group/+/state
        const std::string state_topic = utils::stringFormat("%s/light/group/%d/state", config.mqtt_base_topic.c_str(), group_id);
        const std::string payload = utils::stringFormat(R"({"state":"%s","brightness":%d})", (level > 0 ? "ON" : "OFF"), level);

        ESP_LOGD(TAG, "Publishing Group %d State: %s", group_id, payload.c_str());
        mqtt.publish(state_topic, payload);
    }
    void DaliGroupManagement::stepGroupLevel(const uint8_t group_id, const bool is_up) {
        if (group_id >= 16) return;
        constexpr int STEP_SIZE = 10; // FIXME: hard coded step
        uint8_t new_level = 0;
        bool should_update = false;

        {
            std::lock_guard lock(m_mutex);
            const uint8_t current = m_group_states[group_id].current_level;

            if (current == 0) {
                return;
            }

            int calculated = current;
            if (is_up) {
                calculated += STEP_SIZE;
                if (calculated > 254) calculated = 254;
            } else {
                calculated -= STEP_SIZE;
                if (calculated < 1) calculated = 1;
            }

            if (calculated != current) {
                new_level = static_cast<uint8_t>(calculated);
                should_update = true;
            }
        }

        if (should_update) {
            updateGroupState(group_id, new_level);
        }
    }
}