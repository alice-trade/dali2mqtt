#include "DaliGroupManagement.hxx"
#include "ConfigManager.hxx"
#include "DaliAPI.hxx"
#include <cJSON.h>
#include <esp_log.h>
#include <format>

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
            uint8_t short_address = std::stoi(device_item->string);
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
        std::lock_guard lock(m_mutex);
        cJSON* root = cJSON_CreateObject();
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

        char* json_string = cJSON_PrintUnformatted(root);
        cJSON_Delete(root);

        auto& config_manager = ConfigManager::getInstance();
        auto config = config_manager.getConfig();
        config.dali_group_assignments = json_string;
        config_manager.setConfig(config);

        delete json_string;

        return config_manager.save();
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

        std::lock_guard lock(m_mutex);
        auto& dali = DaliAPI::getInstance();

        if (assigned) {
            ESP_LOGD(TAG, "Assigning device %d to group %d", shortAddress, group);
            dali.assignToGroup(shortAddress, group);
        } else {
            ESP_LOGD(TAG, "Removing device %d from group %d", shortAddress, group);
            dali.removeFromGroup(shortAddress, group);
        }
        m_assignments[shortAddress].set(group, assigned);

        return saveToConfig();
    }

    void DaliGroupManagement::syncDeviceToBus(uint8_t shortAddress, std::bitset<16> newGroups, std::bitset<16> oldGroups) {
        auto& dali = DaliAPI::getInstance();
        // Находим различия
        std::bitset<16> changed = newGroups ^ oldGroups;

        for (uint8_t i = 0; i < 16; ++i) {
            if (changed.test(i)) {
                if (newGroups.test(i)) { // Был 0, стал 1 -> Добавить
                    ESP_LOGI(TAG, "Sync: Adding device %d to group %d", shortAddress, i);
                    dali.assignToGroup(shortAddress, i);
                } else { // Был 1, стал 0 -> Удалить
                    ESP_LOGI(TAG, "Sync: Removing device %d from group %d", shortAddress, i);
                    dali.removeFromGroup(shortAddress, i);
                }
            }
        }
    }


    esp_err_t DaliGroupManagement::setAllAssignments(const GroupAssignments& newAssignments) {
        std::lock_guard lock(m_mutex);

        // Сначала применяем изменения на шине DALI
        for(const auto& [addr, new_groups] : newAssignments) {
            std::bitset<16> old_groups;
            if (auto it = m_assignments.find(addr); it != m_assignments.end()) {
                old_groups = it->second;
            }
            syncDeviceToBus(addr, new_groups, old_groups);
        }

        // Затем обновляем наше внутреннее состояние
        m_assignments = newAssignments;

        // И сохраняем в NVS
        return saveToConfig();
    }
}