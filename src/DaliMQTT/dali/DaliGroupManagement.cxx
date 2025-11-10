#include "DaliGroupManagement.hxx"
#include "ConfigManager.hxx"
#include "DaliAPI.hxx"

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
            char* endptr;
            long addr_val = strtol(device_item->string, &endptr, 10);

            if (endptr == device_item->string || *endptr != '\0' || addr_val < 0 || addr_val > 63) {
                ESP_LOGW(TAG, "Skipping invalid key '%s' in DALI group assignments JSON.", device_item->string);
                continue;
            }
            uint8_t short_address = static_cast<uint8_t>(addr_val);


            std::bitset<16> groups;
            if (cJSON_IsArray(device_item)) {
                cJSON* group_item = nullptr;
                cJSON_ArrayForEach(group_item, device_item) {
                    if (cJSON_IsNumber(group_item) && group_item->valueint >= 0 && group_item->valueint < 16) {
                        groups.set(group_item->valueint);
                    }
                }
            }
            m_assignments[short_address] = groups;
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
                cJSON* group_array = cJSON_CreateArray();
                for (int i = 0; i < 16; ++i) {
                    if (groups.test(i)) {
                        cJSON_AddItemToArray(group_array, cJSON_CreateNumber(i));
                    }
                }
                cJSON_AddItemToObject(root, std::to_string(addr).c_str(), group_array);
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

    std::optional<std::bitset<16>> DaliGroupManagement::getGroupsForDevice(uint8_t shortAddress) const {
        std::lock_guard lock(m_mutex);
        if (auto it = m_assignments.find(shortAddress); it != m_assignments.end()) {
            return it->second;
        }
        return std::nullopt;
    }

    esp_err_t DaliGroupManagement::setGroupMembership(uint8_t shortAddress, uint8_t group, bool assigned) {
        if (group >= 16) return ESP_ERR_INVALID_ARG;

        {
            std::lock_guard lock(m_mutex);
            m_assignments[shortAddress].set(group, assigned);
        }
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

        {
            std::lock_guard lock(m_mutex);
            m_assignments[shortAddress].set(group, !assigned);
        }
        return result;
    }

    esp_err_t DaliGroupManagement::setAllAssignments(const GroupAssignments& newAssignments) {
        struct CommandToSend {
            uint8_t address;
            uint8_t group;
            bool assign;
        };
        std::vector<CommandToSend> commands_to_send;
        {
            std::lock_guard lock(m_mutex);
            std::set<uint8_t> all_addresses;
            for(const auto& addr : m_assignments | std::views::keys) all_addresses.insert(addr);
            for(const auto& addr : newAssignments | std::views::keys) all_addresses.insert(addr);

            for (const auto& addr : all_addresses) {
                std::bitset<16> old_groups = m_assignments.contains(addr) ? m_assignments.at(addr) : std::bitset<16>();
                std::bitset<16> new_groups = newAssignments.contains(addr) ? newAssignments.at(addr) : std::bitset<16>();

                if (old_groups == new_groups) continue;

                std::bitset<16> changed_bits = old_groups ^ new_groups;
                for (uint8_t i = 0; i < 16; ++i) {
                    if (changed_bits.test(i)) {
                        commands_to_send.push_back({addr, i, new_groups.test(i)});
                    }
                }
            }
            m_assignments = newAssignments;
        }

        auto& dali = DaliAPI::getInstance();
        for (const auto& cmd : commands_to_send) {
            if (cmd.assign) {
                ESP_LOGI(TAG, "Sync: Adding device %d to group %d", cmd.address, cmd.group);
                dali.assignToGroup(cmd.address, cmd.group);
            } else {
                ESP_LOGI(TAG, "Sync: Removing device %d from group %d", cmd.address, cmd.group);
                dali.removeFromGroup(cmd.address, cmd.group);
            }
            vTaskDelay(pdMS_TO_TICKS(CONFIG_DALI2MQTT_DALI_INTER_FRAME_DELAY_MS));
        }
        return saveToConfig();
    }
}