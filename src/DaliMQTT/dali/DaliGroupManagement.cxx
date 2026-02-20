// Copyright (c) 2026 Alice-Trade Inc.
// SPDX-License-Identifier: GPL-2.0-or-later

#include "dali/DaliGroupManagement.hxx"
#include "system/ConfigManager.hxx"
#include "dali/DaliDeviceController.hxx"
#include "dali/DaliAdapter.hxx"
#include "mqtt/MQTTClient.hxx"
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
        const auto& config = ConfigManager::Instance().getConfig();
        m_assignments.clear();

        JsonDocument doc;
        if (deserializeJson(doc, config.dali_group_assignments)) return;

        for (JsonPair kv : doc.as<JsonObject>()) {
            auto long_addr_opt = utils::stringToLongAddress(kv.key().c_str());
            if (!long_addr_opt) {
                ESP_LOGW(TAG, "Skipping invalid key '%s' in DALI group assignments JSON.", kv.key().c_str());
                continue;
            }
            std::bitset<16> groups;
            if (kv.value().is<JsonArray>()) {
                for (JsonVariant v : kv.value().as<JsonArray>()) {
                    if (v.is<int>() && v.as<int>() >= 0 && v.as<int>() < 16) groups.set(v.as<int>());
                }
            }
            m_assignments.push_back({*long_addr_opt, groups});
        }
    }

    esp_err_t DaliGroupManagement::saveToConfig() {
        JsonDocument doc;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            for (const auto& [addr, groups] : m_assignments) {
                const auto addr_str = utils::longAddressToString(addr);
                JsonArray group_array = doc[addr_str.data()].to<JsonArray>();
                for (int i = 0; i < 16; ++i) {
                    if (groups.test(i)) {
                        group_array.add(i);
                    }
                }
            }
        }

        std::string json_string;
        serializeJson(doc, json_string);
        return ConfigManager::Instance().saveDaliGroupAssignments(json_string);
    }

    GroupAssignments DaliGroupManagement::getAllAssignments() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_assignments;
    }

    std::optional<std::bitset<16>> DaliGroupManagement::getGroupsForDevice(const DaliLongAddress_t longAddress) const {
        std::lock_guard<std::mutex> lock(m_mutex);
        for(const auto& p : m_assignments) {
             if (p.first == longAddress) return p.second;
        }
        return std::nullopt;
    }

    esp_err_t DaliGroupManagement::setGroupMembership(DaliLongAddress_t longAddress, uint8_t group, bool assigned) {
        if (group >= 16) return ESP_ERR_INVALID_ARG;

        auto int_addr_opt = DaliDeviceController::Instance().getInternalAddress(longAddress);
        if (!int_addr_opt) {
            ESP_LOGE(TAG, "Cannot set group membership: device with long address %lX not found on bus.", longAddress);
            return ESP_ERR_NOT_FOUND;
        }

        {
            std::lock_guard<std::mutex> lock(m_mutex);
            bool found = false;
            for(auto& p : m_assignments) {
                 if (p.first == longAddress) { p.second.set(group, assigned); found = true; break; }
            }
            if(!found) {
                 std::bitset<16> bs; bs.set(group, assigned);
                 m_assignments.push_back({longAddress, bs});
            }
        }

        uint8_t bus_id = extractBusId(*int_addr_opt);
        uint8_t short_addr = extractShortAddr(*int_addr_opt);
        auto* adapter = DaliDeviceController::Instance().getAdapter(bus_id);
        if(!adapter) return ESP_FAIL;

        esp_err_t result = assigned ? adapter->assignToGroup(short_addr, group) : adapter->removeFromGroup(short_addr, group);
        if (result == ESP_OK) {
            saveToConfig();
            publishDeviceGroupState(longAddress, getGroupsForDevice(longAddress).value_or(std::bitset<16>()));
        }
        return result;
    }

    esp_err_t DaliGroupManagement::setAllAssignments(const GroupAssignments& newAssignments) {
        struct Cmd { uint8_t sa; uint8_t grp; bool assign; uint8_t bus_id; };
        std::vector<Cmd> commands;

        {
            std::lock_guard<std::mutex> lock(m_mutex);

            for (const auto& [new_addr, new_groups] : newAssignments) {
                std::bitset<16> old_groups;
                for (const auto& [old_addr, og] : m_assignments) {
                    if (old_addr == new_addr) { old_groups = og; break; }
                }

                if (old_groups != new_groups) {
                    if (auto int_addr_opt = DaliDeviceController::Instance().getInternalAddress(new_addr)) {
                        std::bitset<16> diff = old_groups ^ new_groups;
                        for (uint8_t i = 0; i < 16; ++i) {
                            if (diff.test(i)) {
                                commands.push_back({extractShortAddr(*int_addr_opt), i, new_groups.test(i), extractBusId(*int_addr_opt)});
                            }
                        }
                    }
                }
            }
            m_assignments = newAssignments;
        }

        for (const auto& c : commands) {
            auto* adapter = DaliDeviceController::Instance().getAdapter(c.bus_id);
            if(adapter) {
                if (c.assign) adapter->assignToGroup(c.sa, c.grp);
                else adapter->removeFromGroup(c.sa, c.grp);
            }
            vTaskDelay(pdMS_TO_TICKS(15));
        }

        publishAllGroups();
        return saveToConfig();
    }

    esp_err_t DaliGroupManagement::refreshAssignmentsFromBus() {
        ESP_LOGI(TAG, "Refreshing group assignments from DALI bus...");
        auto devices = DaliDeviceController::Instance().getDevices();
        if (devices.empty()) return ESP_OK;

        GroupAssignments new_assignments;

        for (const auto& device : devices) {
            const auto& id = getIdentity(device);
            if (!id.available) continue;
            if (std::holds_alternative<ControlGear>(device)) {
                if(auto* adapter = DaliDeviceController::Instance().getAdapter(extractBusId(id.internal_address))) {
                    if (auto groups_opt = adapter->getDeviceGroups(extractShortAddr(id.internal_address))) {
                        new_assignments.emplace_back(id.long_address, *groups_opt);
                    }
                }
            }
            vTaskDelay(pdMS_TO_TICKS(20));
        }

        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_assignments = std::move(new_assignments);
        }

        publishAllGroups();
        return saveToConfig();
    }

    DaliGroup DaliGroupManagement::getGroupState(uint8_t bus_id, const uint8_t group_id) const {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (bus_id < Constants::MaxBuses && group_id < 16) return m_group_states[(bus_id * 16) + group_id];
        return DaliGroup{};
    }

    void DaliGroupManagement::publishDeviceGroupState(const DaliLongAddress_t longAddr, const std::bitset<16>& groups) const {
        auto const& mqtt = MQTTClient::Instance();
        const auto config = ConfigManager::Instance().getConfig();
        const auto addr_str = utils::longAddressToString(longAddr);

        char topic[128];
        snprintf(topic, sizeof(topic), "%s/light/%s/groups", config.mqtt_base_topic.c_str(), addr_str.data());

        JsonDocument doc;
        JsonArray groups_array = doc["groups"].to<JsonArray>();
        for (uint8_t i = 0; i < 16; ++i) {
            if (groups.test(i)) groups_array.add(i);
        }
        std::string payload;
        serializeJson(doc, payload);
        mqtt.publish(topic, payload.c_str(), 1, true);
    }

    void DaliGroupManagement::publishAllGroups() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        for (const auto& [longAddr, groups] : m_assignments) {
            publishDeviceGroupState(longAddr, groups);
        }
    }

    void DaliGroupManagement::updateGroupState(uint8_t bus_id, const uint8_t group_id, const DaliPublishState& state) {
        if (bus_id >= Constants::MaxBuses || group_id >= 16) return;

        uint8_t index = (bus_id * 16) + group_id;
        bool changed = false;

        {
            std::lock_guard<std::mutex> lock(m_mutex);
            auto& group = m_group_states[index];

            if (state.level.has_value()) {
                const uint8_t lvl = state.level.value();
                if (lvl > 0) group.last_level = lvl;
                if (group.current_level != lvl) {
                    group.current_level = lvl;
                    changed = true;
                }
            }
            if (state.color_temp.has_value() && group.color_temp != state.color_temp) {
                group.color_temp = state.color_temp; changed = true;
            }
            if (state.rgb.has_value() && group.rgb != state.rgb) {
                group.rgb = state.rgb; changed = true;
            }
        }

        publishGroupState(bus_id, group_id, m_group_states[index].current_level, m_group_states[index].color_temp, m_group_states[index].rgb);
    }

    void DaliGroupManagement::restoreGroupLevel(uint8_t bus_id, const uint8_t group_id) {
        if (bus_id >= Constants::MaxBuses || group_id >= 16) return;
        uint8_t target;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            uint8_t index = (bus_id * 16) + group_id;
            target = (m_group_states[index].last_level > 0) ? m_group_states[index].last_level : 254;
        }
        updateGroupState(bus_id, group_id, {.level = target});
    }

    void DaliGroupManagement::stepGroupLevel(uint8_t bus_id, const uint8_t group_id, const bool is_up) {
        if (bus_id >= Constants::MaxBuses || group_id >= 16) return;
        constexpr int STEP_SIZE = 10;
        uint8_t new_level = 0;
        bool should_update = false;

        {
            std::lock_guard<std::mutex> lock(m_mutex);
            uint8_t index = (bus_id * 16) + group_id;
            const uint8_t current = m_group_states[index].current_level;

            if (current == 0) return;

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

        if (should_update) updateGroupState(bus_id, group_id, {.level = new_level});
    }

    void DaliGroupManagement::publishGroupState(uint8_t bus_id, const uint8_t group_id, const uint8_t level,
                                                std::optional<uint16_t> color_temp,
                                                std::optional<DaliRGB> rgb) const {
        auto const& mqtt = MQTTClient::Instance();
        const auto config = ConfigManager::Instance().getConfig();

        char topic[128];
        snprintf(topic, sizeof(topic), "%s/light/bus/%d/group/%d/state", config.mqtt_base_topic.c_str(), bus_id, group_id);

        JsonDocument doc;
        doc["state"] = (level > 0 ? "ON" : "OFF");
        doc["brightness"] = level;
        if (color_temp.has_value()) doc["color_temp"] = *color_temp;
        if (rgb.has_value()) {
            JsonObject color = doc["color"].to<JsonObject>();
            color["r"] = rgb->r; color["g"] = rgb->g; color["b"] = rgb->b;
        }
        char payload[256];
        serializeJson(doc, payload, sizeof(payload));
        mqtt.publish(topic, payload, 0, true);
    }
}
