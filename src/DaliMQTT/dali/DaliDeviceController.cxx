// Copyright (c) 2026 Alice-Trade Inc.
// SPDX-License-Identifier: GPL-2.0-or-later

#include "dali/DaliDeviceController.hxx"
#include <dali/DaliGroupManagement.hxx>
#include <utility>
#include "system/ConfigManager.hxx"
#include "dali/DaliAddressMap.hxx"
#include "dali/DaliAdapter.hxx"
#include "mqtt/MQTTClient.hxx"
#include "utils/DaliLongAddrConversions.hxx"

namespace daliMQTT
{
    static constexpr char TAG[] = "DaliDeviceController";

    void DaliDeviceController::init() {
        ESP_LOGI(TAG, "Initializing DALI Device Controller...");
        m_short_to_long_map.fill(0xFFFFFFFF);
        if (!DaliAdapter::Instance().isInitialized()) {
            ESP_LOGW(TAG, "DALI Driver not initialized. Device discovery skipped.");
            return;
        }

        bool map_loaded = DaliAddressMap::load(m_devices, m_short_to_long_map);
        if (!map_loaded || !validateAddressMap()) {
            ESP_LOGI(TAG, "Cached address map is invalid or missing. Performing a full scan.");
            discoverAndMapDevices();
        } else {
            ESP_LOGI(TAG, "Successfully loaded and validated cached DALI address map.");
        }

    }

    void DaliDeviceController::start() {
        if (m_event_handler_task || m_sync_task_handle) {
            ESP_LOGW(TAG, "DALI tasks are already running.");
            return;
        }

        auto& dali_api = DaliAdapter::Instance();
        if (dali_api.isInitialized()) {
            dali_api.startSniffer();
            xTaskCreate(daliEventHandlerTask, "dali_event_handler", 4096, this, 5, &m_event_handler_task);
            xTaskCreate(daliSyncTask, "dali_sync", 6144 , this, 4, &m_sync_task_handle);
            ESP_LOGI(TAG, "DALI monitoring and sync tasks started.");
        } else {
            ESP_LOGE(TAG, "Cannot start DALI tasks: DaliAPI not initialized.");
        }
    }

    void DaliDeviceController::publishState(const DaliLongAddress_t long_addr, const ControlGear& device) const {
        auto const& mqtt = MQTTClient::Instance();
        const auto config = ConfigManager::Instance().getConfig();

        const auto addr_str = utils::longAddressToString(long_addr);
        char topic[128];
        snprintf(topic, sizeof(topic), "%s/light/%s/state", config.mqtt_base_topic.c_str(), addr_str.data());

        if (device.current_level == 255) return;

        JsonDocument doc;
        doc["state"] = (device.current_level > 0 ? "ON" : "OFF");
        doc["brightness"] = device.current_level;
        doc["status_byte"] = device.status_byte;

        if (device.color.has_value()) {
            const auto& c = device.color.value();
            if (c.current_tc.has_value()) {
                doc["color_temp"] = c.current_tc.value();
            }
            if (c.current_rgb.has_value()) {
                JsonObject color = doc["color"].to<JsonObject>();
                color["r"] = c.current_rgb->r;
                color["g"] = c.current_rgb->g;
                color["b"] = c.current_rgb->b;
            }
        }

        char payload[256];
        serializeJson(doc, payload, sizeof(payload));
        mqtt.publish(topic, payload, 0, true);
    }

    void DaliDeviceController::publishAvailability(const DaliLongAddress_t long_addr, const bool is_available) {
        auto const& mqtt = MQTTClient::Instance();
        const auto config = ConfigManager::Instance().getConfig();

        const auto addr_str = utils::longAddressToString(long_addr);
        char topic[128];
        snprintf(topic, sizeof(topic), "%s/light/%s/status", config.mqtt_base_topic.c_str(), addr_str.data());

        const char* payload = is_available ? CONFIG_DALI2MQTT_MQTT_PAYLOAD_ONLINE : CONFIG_DALI2MQTT_MQTT_PAYLOAD_OFFLINE;
        mqtt.publish(topic, payload, 1, true);
    }

    void DaliDeviceController::procUpdateDeviceState(const DaliLongAddress_t longAddr, const DaliPublishState& state) {
        for (auto& dev_var : m_devices) {
            if (getIdentity(dev_var).long_address == longAddr) {
                if (auto* gear = std::get_if<ControlGear>(&dev_var)) {
                    bool state_changed = false;
                    if (state.level.has_value()) {
                        const uint8_t lvl = state.level.value();
                        if (lvl > 0) gear->last_level = lvl;
                        if (gear->current_level != lvl) {
                            gear->current_level = lvl;
                            state_changed = true;
                        }
                    }
                    if (state.status_byte.has_value()) {
                        const uint8_t sb = state.status_byte.value();
                        if (gear->status_byte != sb) {
                            gear->status_byte = sb;
                            state_changed = true;
                        }
                    }
                    if (state.color_temp.has_value() || state.rgb.has_value() || state.active_mode.has_value()) {
                        if (!gear->color.has_value()) gear->color = ColorFeatures();
                        auto& c = gear->color.value();
                        if (state.color_temp.has_value() && c.current_tc != state.color_temp) {
                            c.current_tc = state.color_temp;
                            state_changed = true;
                        }
                        if (state.rgb.has_value() && c.current_rgb != state.rgb) {
                            c.current_rgb = state.rgb;
                            state_changed = true;
                        }
                        if (state.active_mode.has_value()) {
                            c.active_mode = state.active_mode.value();
                        }
                    }
                    if (state_changed || gear->initial_sync_needed) {
                        publishState(longAddr, *gear);
                        gear->initial_sync_needed = false;
                    }
                }
                break;
            }
        }
    }

    void DaliDeviceController::publishAttributes(const DaliLongAddress_t long_addr) const {
        auto const& mqtt = MQTTClient::Instance();
        const auto config = ConfigManager::Instance().getConfig();
        const auto addr_str = utils::longAddressToString(long_addr);
        char topic[128];
        snprintf(topic, sizeof(topic), "%s/light/%s/attributes", config.mqtt_base_topic.c_str(), addr_str.data());

        ControlGear dev_copy;
        bool found = false;
        {
            std::lock_guard<std::mutex> lock(m_devices_mutex);
            for (const auto& dev_var : m_devices) {
                if (getIdentity(dev_var).long_address == long_addr) {
                    if (const auto* gear = std::get_if<ControlGear>(&dev_var)) {
                        dev_copy = *gear;
                        found = true;
                    }
                    break;
                }
            }
        }
        if (!found) return;

        JsonDocument doc;
        if (dev_copy.device_type.has_value()) doc["device_type"] = dev_copy.device_type.value();
        if (!dev_copy.gtin.empty()) doc["gtin"] = dev_copy.gtin;
        doc["dev_min_level"] = dev_copy.min_level;
        doc["dev_max_level"] = dev_copy.max_level;
        doc["dev_power_on_level"] = dev_copy.power_on_level;
        doc["dev_system_failure_level"] = dev_copy.system_failure_level;
        doc["short_address"] = dev_copy.short_address;

        std::string json_str;
        serializeJson(doc, json_str);
        mqtt.publish(topic, json_str.c_str(), 1, true);
    }

    std::optional<uint8_t> DaliDeviceController::getLastLevel(const DaliLongAddress_t longAddress) const {
        std::lock_guard<std::mutex> lock(m_devices_mutex);
        for (const auto& dev_var : m_devices) {
            if (getIdentity(dev_var).long_address == longAddress) {
                if (const auto* gear = std::get_if<ControlGear>(&dev_var)) {
                    return gear->last_level;
                }
                break;
            }
        }
        return std::nullopt;
    }

    bool DaliDeviceController::validateAddressMap() {
        ESP_LOGI(TAG, "Validating cached DALI address map...");
        auto& dali = DaliAdapter::Instance();

        struct ValidationItem {
            DaliLongAddress_t long_addr;
            uint8_t short_addr;
        };
        std::vector<ValidationItem> devices_to_validate;

        {
            std::lock_guard<std::mutex> lock(m_devices_mutex);
            if (m_devices.empty()) {
                ESP_LOGI(TAG, "Map is empty, validation skipped.");
                return false;
            }
            devices_to_validate.reserve(m_devices.size());
            for (const auto& dev_var : m_devices) {
                const auto& id = getIdentity(dev_var);
                devices_to_validate.push_back({id.long_address, id.short_address});
            }
        }

        for (const auto& [long_addr, short_addr] : devices_to_validate) {
            if (auto status_opt = dali.sendQuery(DaliAddressType::Short, short_addr, Commands::OpCode::QueryStatus); status_opt.has_value()) {
                if (auto long_addr_from_bus_opt = dali.getLongAddress(short_addr)) {
                    if (*long_addr_from_bus_opt != long_addr) {
                        ESP_LOGW(TAG, "Validation CONFLICT: Short Addr %d has Long Addr %lX, expected %lX. Full scan required.",
                            short_addr, *long_addr_from_bus_opt, long_addr);
                        return false;
                    }
                    {
                        std::lock_guard<std::mutex> lock(m_devices_mutex);
                        for(auto& dev_var : m_devices) {
                             if(getIdentity(dev_var).long_address == long_addr) {
                                 getIdentity(dev_var).available = true;
                                 if (std::holds_alternative<ControlGear>(dev_var)) {
                                     publishAvailability(long_addr, true);
                                 }
                                 break;
                             }
                        }
                    }
                }
            }
            vTaskDelay(pdMS_TO_TICKS(CONFIG_DALI2MQTT_DALI_POLL_DELAY_MS));
        }
        return true;
    }

    [[noreturn]] void DaliDeviceController::daliEventHandlerTask(void* pvParameters) {
        auto* self = static_cast<DaliDeviceController*>(pvParameters);
        const auto& dali_api = DaliAdapter::Instance();

        QueueHandle_t queue = dali_api.getEventQueue();
        dali_frame_t frame{};

        while (true) {
            if (xQueueReceive(queue, &frame, portMAX_DELAY) == pdPASS) {
#ifdef CONFIG_DALI2MQTT_SNIFFER_DEBUG_PUBLISH_MQTT
                auto const& mqtt = MQTTClient::Instance();
                if (mqtt.getStatus() == MqttStatus::CONNECTED) {
                    char topic[128];
                    snprintf(topic, sizeof(topic), "%s/debug/sniffer_raw", ConfigManager::Instance().getConfig().mqtt_base_topic.c_str());

                    char payload[128];
                    uint32_t ts = esp_log_timestamp();
                    if (frame.length == 8) {
                        snprintf(payload, sizeof(payload), R"({"type":"backward","len":8,"data":%lu,"hex":"%02lX","ts":%lu})", frame.data, (frame.data & 0xFF), ts);
                    } else {
                        snprintf(payload, sizeof(payload), R"({"type":"forward","len":%u,"data":%lu,"hex":"%06lX","ts":%lu})", frame.length, frame.data, (frame.data & 0xFFFFFF), ts);
                    }
                    mqtt.publish(topic, payload, 0, false);
                }
#endif

                if (frame.length == 24) {
                    self->ProcessInputDeviceFrame(frame);
                } else if (frame.length == 16) {
                    self->SnifferProcessFrame(frame);
                }
            }
        }
    }

    [[noreturn]] void DaliDeviceController::daliSyncTask(void* pvParameters) {
        auto* self = static_cast<DaliDeviceController*>(pvParameters);
        constexpr int64_t NVS_SAVE_DEBOUNCE_MS = 60000;
        self->requestBroadcastSync(200, 150);

        ESP_LOGI(TAG, "Dali Adaptive Sync Task Started.");
        const auto config = ConfigManager::Instance().getConfig();

        const uint32_t safe_cycle_time = std::max<uint32_t>(1000, config.dali_poll_interval_ms);
        const uint32_t calc_delay_ms = safe_cycle_time >> 6;
        const TickType_t rr_delay_ticks = pdMS_TO_TICKS(std::max<uint32_t>(20, calc_delay_ms));
        constexpr TickType_t priority_delay_ticks = pdMS_TO_TICKS(10);

        while (true) {
            uint8_t priority_addr = 255;
            bool has_priority = false;
            int64_t now = esp_timer_get_time() / 1000;

            if (self->m_nvs_dirty) {
                if ((now - self->m_last_nvs_change_ts) > NVS_SAVE_DEBOUNCE_MS) {
                    std::vector<DaliDevice> copy_devs;
                    {
                        std::lock_guard<std::mutex> lock(self->m_devices_mutex);
                        copy_devs = self->m_devices;
                        self->m_nvs_dirty = false;
                    }
                    DaliAddressMap::save(copy_devs);
                }
            }

            // Check Deferred Requests
            {
                std::lock_guard<std::mutex> lock(self->m_queue_mutex);

                if (!self->m_deferred_requests.empty()) {
                    auto it = self->m_deferred_requests.begin();
                    while (it != self->m_deferred_requests.end()) {
                        if (now >= it->execute_at_ts) {
                            if (!self->m_priority_set.contains(it->short_address)) {
                                self->m_priority_queue.push_back(it->short_address);
                                self->m_priority_set.insert(it->short_address);
                            }
                            it = self->m_deferred_requests.erase(it);
                        } else {
                            ++it;
                        }
                    }
                }

                // Check Priority Queue
                if (!self->m_priority_queue.empty()) {
                    priority_addr = self->m_priority_queue.front();
                    self->m_priority_queue.erase(self->m_priority_queue.begin());
                    self->m_priority_set.erase(priority_addr);
                    has_priority = true;
                }
            }

            if (has_priority) {
                self->pollSingleDevice(priority_addr);
                vTaskDelay(priority_delay_ticks);
            } else {
                // Round Robin Logic
                self->pollSingleDevice(self->m_round_robin_index);
                self->m_round_robin_index++;
                if (self->m_round_robin_index >= 64) {
                    self->m_round_robin_index = 0;

                    // Sync Group states from device info
                    auto all_assignments = DaliGroupManagement::Instance().getAllAssignments();
                    std::vector<DaliDevice> devices_snapshot;
                    {
                        std::lock_guard<std::mutex> lock(self->m_devices_mutex);
                        devices_snapshot = self->m_devices;
                    }
                    std::array<std::optional<DaliPublishState>, 16> group_sync_states;

                    for (const auto& [long_addr, groups] : all_assignments) {
                        for (const auto& dev : devices_snapshot) {
                            if (getIdentity(dev).long_address == long_addr) {
                                if (const auto* gear = std::get_if<ControlGear>(&dev)) {
                                    if (!gear->available) break;

                                    for (uint8_t group = 0; group < 16; ++group) {
                                        if (groups.test(group)) {
                                            if (!group_sync_states[group].has_value()) {
                                                group_sync_states[group] = DaliPublishState{ .level = 0 };
                                            }

                                            auto& g_state = group_sync_states[group].value();

                                            if (gear->current_level > g_state.level.value_or(0)) {
                                                g_state.level = gear->current_level;
                                            }
                                            if (gear->color.has_value()) {
                                                if (gear->color->supports_rgb && gear->color->current_rgb.has_value()) {
                                                    g_state.rgb = gear->color->current_rgb;
                                                }
                                                if (gear->color->supports_tc && gear->color->current_tc.has_value()) {
                                                    g_state.color_temp = gear->color->current_tc;
                                                }
                                            }
                                        }
                                    }
                                }
                                break;
                            }
                        }
                    }

                    for (uint8_t group_id = 0; group_id < 16; ++group_id) {
                        if (group_sync_states[group_id].has_value()) {
                            DaliGroupManagement::Instance().updateGroupState(group_id, group_sync_states[group_id].value());
                        }
                    }
                }
                vTaskDelay(rr_delay_ticks);
            }
        }
    }
    void DaliDeviceController::ProcessInputDeviceFrame(const dali_frame_t& frame) const {
            const uint32_t data = frame.data;
            const uint8_t addr_byte = (data >> 16) & 0xFF;
            const uint8_t instance_byte = (data >> 8) & 0xFF;
            const uint8_t event_byte = data & 0xFF;
            bool is_event_scheme = (addr_byte & 0x01) == 0x01;

            std::string addr_type_str = "unknown";
            uint8_t address = 0;

            if (is_event_scheme) {
                if ((addr_byte & 0x80) == 0) {
                    addr_type_str = "short";
                    address = (addr_byte >> 1) & 0x3F;
                } else if ((addr_byte & 0xE0) == 0x80) {
                    addr_type_str = "group";
                    address = (addr_byte >> 1) & 0x0F;
                } else if (addr_byte == 0xC1) {
                    addr_type_str = "instance_group";
                } else if (addr_byte == 0xFD || addr_byte == 0xFF) {
                     addr_type_str = "broadcast";
                }
            } else {
                 return; // 24-bit Command (not event)
            }

            char topic_addr_val[16];
            snprintf(topic_addr_val, sizeof(topic_addr_val), "%u", address);

            if (addr_type_str == "short") {
                if (const auto long_addr_opt = getLongAddress(address, true)) {
                    addr_type_str = "long";
                    auto la_str = utils::longAddressToString(*long_addr_opt);
                    snprintf(topic_addr_val, sizeof(topic_addr_val), "%s", la_str.data());
                }
            }

            JsonDocument doc;
            doc["type"] = "event";
            doc["address_type"] = addr_type_str;
            doc["address"] = address;
            doc["instance"] = instance_byte;
            doc["event_code"] = event_byte;
            if (addr_type_str == "long") doc["long_addr"] = topic_addr_val;

            char payload[256];
            serializeJson(doc, payload, sizeof(payload));

            char topic[128];
            snprintf(topic, sizeof(topic), "%s/event/%s/%s",
                     ConfigManager::Instance().getConfig().mqtt_base_topic.c_str(),
                     addr_type_str.c_str(),
                     topic_addr_val);

            MQTTClient::Instance().publish(topic, payload, 0, false);
    }

    void DaliDeviceController::requestDeviceSync(uint8_t shortAddress, uint32_t delay_ms) {
        if (shortAddress >= 64) return;
        std::lock_guard<std::mutex> lock(m_queue_mutex);
        if (delay_ms == 0) {
            if (!m_priority_set.contains(shortAddress)) {
                m_priority_queue.push_back(shortAddress);
                m_priority_set.insert(shortAddress);
            }
        } else {
            int64_t now = esp_timer_get_time() / 1000;
            m_deferred_requests.push_back({shortAddress, now + delay_ms});
        }
    }

    void DaliDeviceController::requestBroadcastSync(const uint32_t base_delay_ms, const uint32_t stagger_ms) {
        std::lock_guard<std::mutex> lock(m_devices_mutex);
        uint32_t current_delay = base_delay_ms;
        for (const auto &dev_var: m_devices) {
            if (std::holds_alternative<ControlGear>(dev_var)) {
                requestDeviceSync(getIdentity(dev_var).short_address, current_delay);
                current_delay += stagger_ms;
            }
        }
    }

    std::optional<uint8_t> DaliDeviceController::pollAvailabilityAndLevel(const uint8_t shortAddr, const DaliLongAddress_t longAddr) {
        auto& dali = DaliAdapter::Instance();
        const auto level_opt = dali.sendQuery(DaliAddressType::Short, shortAddr, Commands::OpCode::QueryActualLevel);
        const bool is_responding = level_opt.has_value();
        {
            std::lock_guard<std::mutex> lock(m_devices_mutex);
            bool found = false;
            for (auto& dev_var : m_devices) {
                auto& id = getIdentity(dev_var);
                if (id.long_address == longAddr) {
                    found = true;
                    if (id.available != is_responding) {
                        id.available = is_responding;
                        if (std::holds_alternative<ControlGear>(dev_var)) {
                            publishAvailability(longAddr, is_responding);
                        }
                    }
                    break;
                }
            }
            if(!found) return std::nullopt;
        }
        if (!is_responding) return std::nullopt;
        return level_opt.value();
    }

    void DaliDeviceController::checkDT8Features(const uint8_t shortAddr, const DaliLongAddress_t longAddr) {
        bool needs_check = false;
        {
            std::lock_guard<std::mutex> lock(m_devices_mutex);
            for(auto& dev : m_devices) {
                 if (getIdentity(dev).long_address == longAddr) {
                     if (const auto* gear = std::get_if<ControlGear>(&dev)) {
                         if (gear->device_type.value_or(0xFF) == 8 && !gear->static_data_loaded) needs_check = true;
                     }
                     break;
                 }
            }
        }

        if (!needs_check) return;
        auto& dali = DaliAdapter::Instance();
        const auto colour_type = dali.readMemoryLocation(shortAddr, 205, Commands::MemBank205::ColourType);

        if (colour_type.has_value()) {
            const bool tc = (*colour_type & 0x02);
            const bool rgb = (*colour_type & 0x08);
            std::lock_guard<std::mutex> lock(m_devices_mutex);
            for(auto& dev : m_devices) {
                 if(getIdentity(dev).long_address == longAddr) {
                     if (auto* g = std::get_if<ControlGear>(&dev)) {
                         if (!g->color.has_value()) g->color = ColorFeatures();
                         g->color->supports_tc = tc;
                         g->color->supports_rgb = rgb;
                         m_nvs_dirty = true;
                     }
                     break;
                 }
            }
        }
    }

    DaliDeviceController::ColorPollResult DaliDeviceController::pollColorDataCyclic(const uint8_t shortAddr, const DaliLongAddress_t longAddr, const uint8_t current_level) {
        ColorPollResult result;
        if (current_level == 0) return result;
        bool tc_supp = false, rgb_supp = false;
        {
            std::lock_guard<std::mutex> lock(m_devices_mutex);
            for(auto& dev : m_devices) {
                 if(getIdentity(dev).long_address == longAddr) {
                     auto* g = std::get_if<ControlGear>(&dev);
                     if (g && g->color.has_value()) {
                         tc_supp = g->color->supports_tc;
                         rgb_supp = g->color->supports_rgb;
                     }
                     break;
                 }
            }
        }
        if (!tc_supp && !rgb_supp) return result;
        auto& dali = DaliAdapter::Instance();
        if (tc_supp) {
            auto h = dali.readMemoryLocation(shortAddr, 205, Commands::MemBank205::ColourValueTcH);
            auto l = dali.readMemoryLocation(shortAddr, 205, Commands::MemBank205::ColourValueTcL);
            if (h && l) result.tc = (*h << 8) | *l;
        }
        if (rgb_supp) {
            auto r = dali.readMemoryLocation(shortAddr, 205, Commands::MemBank205::RgbR);
            auto g = dali.readMemoryLocation(shortAddr, 205, Commands::MemBank205::RgbG);
            auto b = dali.readMemoryLocation(shortAddr, 205, Commands::MemBank205::RgbB);
            if (r && g && b) result.rgb = DaliRGB{*r, *g, *b};
        }
        return result;
    }

    void DaliDeviceController::performInitialGroupSync(const DaliLongAddress_t longAddr, const uint8_t level, const ColorPollResult& colorData) {
        bool is_initial_sync = false;
        bool is_dt8 = false;
        {
            std::lock_guard<std::mutex> lock(m_devices_mutex);
            for(auto& dev : m_devices) {
                 if(getIdentity(dev).long_address == longAddr) {
                     if (const auto* gear = std::get_if<ControlGear>(&dev)) {
                         is_initial_sync = gear->initial_sync_needed;
                         is_dt8 = gear->color.has_value();
                     }
                     break;
                 }
            }
        }
        if (is_initial_sync) {
            if (const auto groups_opt = DaliGroupManagement::Instance().getGroupsForDevice(longAddr)) {
                for (uint8_t i = 0; i < 16; ++i) {
                    if (groups_opt->test(i)) {
                        const auto current_grp = DaliGroupManagement::Instance().getGroupState(i);
                        DaliPublishState groupUpdate;
                        if (level > current_grp.current_level) groupUpdate.level = level;
                        if (is_dt8) {
                            groupUpdate.color_temp = colorData.tc;
                            groupUpdate.rgb = colorData.rgb;
                        }
                        if (groupUpdate.level.has_value() || groupUpdate.color_temp.has_value() || groupUpdate.rgb.has_value()) {
                            DaliGroupManagement::Instance().updateGroupState(i, groupUpdate);
                        }
                    }
                }
            }
        }
    }

    void DaliDeviceController::initialStaticDataFetch(const uint8_t shortAddr, const DaliLongAddress_t longAddr) {
        bool needs_load = false;
        {
            std::lock_guard<std::mutex> lock(m_devices_mutex);
            for(auto& dev : m_devices) {
                 if (getIdentity(dev).long_address == longAddr) {
                     if (const auto* g = std::get_if<ControlGear>(&dev)) {
                         if (!g->static_data_loaded) needs_load = true;
                     }
                     break;
                 }
            }
        }
        if (!needs_load) return;

        auto& dali = DaliAdapter::Instance();
        const auto min_opt = dali.sendQuery(DaliAddressType::Short, shortAddr, Commands::OpCode::QueryMinLevel);
        const auto max_opt = dali.sendQuery(DaliAddressType::Short, shortAddr, Commands::OpCode::QueryMaxLevel);
        const auto power_on_opt = dali.sendQuery(DaliAddressType::Short, shortAddr, Commands::OpCode::QueryPowerOnLevel);
        const auto fail_opt = dali.sendQuery(DaliAddressType::Short, shortAddr, Commands::OpCode::QuerySystemFailureLevel);
        const auto gtin_opt = dali.getGTIN(shortAddr);
        const auto dt_opt = dali.getDeviceType(shortAddr);

        {
            std::lock_guard<std::mutex> lock(m_devices_mutex);
            for(auto& dev : m_devices) {
                 if (getIdentity(dev).long_address == longAddr) {
                     if (auto* g = std::get_if<ControlGear>(&dev)) {
                         bool changed = false;
                         if (gtin_opt.has_value()) { g->gtin = gtin_opt.value(); changed = true; }
                         if (dt_opt.has_value()) { g->device_type = dt_opt; changed = true; }
                         if (min_opt.has_value()) { g->min_level = *min_opt; changed = true; }
                         if (max_opt.has_value()) { g->max_level = *max_opt; changed = true; }
                         if (power_on_opt.has_value()) { g->power_on_level = *power_on_opt; changed = true; }
                         if (fail_opt.has_value()) { g->system_failure_level = *fail_opt; changed = true; }
                         g->static_data_loaded = true;
                         if (changed) {
                             m_nvs_dirty = true;
                             m_last_nvs_change_ts = esp_timer_get_time() / 1000;
                         }
                     }
                     break;
                 }
            }
        }
        publishAttributes(longAddr);
    }

    void DaliDeviceController::pollSingleDevice(const uint8_t shortAddr) {
        DaliLongAddress_t longAddr = 0;
        if (auto la_opt = getLongAddress(shortAddr)) longAddr = *la_opt;
        else return;

        const auto levelOpt = pollAvailabilityAndLevel(shortAddr, longAddr);
        if (!levelOpt) return; // Offline

        bool isControlGear = false;
        {
            std::lock_guard<std::mutex> lock(m_devices_mutex);
            for(auto& dev : m_devices) {
                 if (getIdentity(dev).long_address == longAddr) {
                     isControlGear = std::holds_alternative<ControlGear>(dev);
                     break;
                 }
            }
        }
        if (!isControlGear) return;

        auto& dali = DaliAdapter::Instance();
        const auto statusOpt = dali.getDeviceStatus(shortAddr);

        // Part 207 (?)
        if (statusOpt.has_value() && (*statusOpt & 0x02)) {
            auto led_faults = dali.readMemoryLocation(shortAddr, 1, 0x02); // TODO: Pass LED Fault
            if (led_faults) ESP_LOGW(TAG, "Device %d LED Fault Bits: 0x%02X", shortAddr, *led_faults);
        }

        checkDT8Features(shortAddr, longAddr);
        const auto colorData = pollColorDataCyclic(shortAddr, longAddr, *levelOpt);

        updateDeviceState(longAddr, {
            .level = *levelOpt,
            .status_byte = statusOpt,
            .color_temp = colorData.tc,
            .rgb = colorData.rgb,
        });

        performInitialGroupSync(longAddr, *levelOpt, colorData);
        initialStaticDataFetch(shortAddr, longAddr);
    }

    std::bitset<64> DaliDeviceController::performFullInitialization() {
        if (!DaliAdapter::Instance().isInitialized()) {
            ESP_LOGE(TAG, "Cannot initialize DALI bus: DALI driver is not initialized.");
            return {};
        }
        DaliAdapter::Instance().initializeBus();
        return discoverAndMapDevices();
    }

    std::bitset<64> DaliDeviceController::perform24BitDeviceInitialization() {
        if (!DaliAdapter::Instance().isInitialized()) {
            ESP_LOGE(TAG, "Cannot initialize DALI bus: DALI driver is not initialized.");
            return {};
        }
        DaliAdapter::Instance().initialize24BitDevicesBus();
        return discoverAndMapDevices();
    }

    std::bitset<64> DaliDeviceController::performScan() {
        if (!DaliAdapter::Instance().isInitialized()) {
            ESP_LOGE(TAG, "Cannot scan DALI bus: DALI driver is not initialized.");
            return {};
        }
        return discoverAndMapDevices();
    }

    std::bitset<64> DaliDeviceController::discoverAndMapDevices() {
        ESP_LOGI(TAG, "Starting DALI device discovery and mapping...");
        auto& dali = DaliAdapter::Instance();
        std::vector<DaliDevice> new_devices;
        new_devices.reserve(64);
        std::array<DaliLongAddress_t, 256> new_short_to_long_map;
        new_short_to_long_map.fill(0xFFFFFFFF);
        std::bitset<64> found_devices;

        for (uint8_t sa = 0; sa < 64; ++sa) {
            if (auto status_opt = dali.sendQuery(DaliAddressType::Short, sa, Commands::OpCode::QueryStatus); status_opt.has_value()) {
                found_devices.set(sa);
                if (auto long_addr_opt = dali.getLongAddress(sa)) {
                    DaliLongAddress_t long_addr = *long_addr_opt;
                    ControlGear dev;
                    dev.long_address = long_addr;
                    dev.short_address = sa;
                    dev.available = true;
                    new_devices.push_back(dev);
                    new_short_to_long_map[sa] = long_addr;
                    ESP_LOGI(TAG, "Gear found at SA %d (LA: 0x%06lX)", sa, long_addr);
                }
            }

            if (dali.sendInputDeviceCommand(sa, std::to_underlying(
                                                Commands::InputDeviceOp::QueryStatus), 0x00)) {
                auto long_addr_opt = getInputDeviceLongAddress(sa);
                DaliLongAddress_t long_addr = long_addr_opt.value_or(0xFE0000 | sa);

                InputDevice dev;
                dev.long_address = long_addr;
                dev.short_address = sa;
                dev.available = true;
                new_devices.push_back(dev);
                new_short_to_long_map[sa | 0x80] = long_addr;

                ESP_LOGI(TAG, "Input Device found at SA (CD) %d (LA: 0x%06lX)", sa, long_addr);
            }
            vTaskDelay(pdMS_TO_TICKS(CONFIG_DALI2MQTT_DALI_POLL_DELAY_MS));
        }

        {
            std::lock_guard<std::mutex> lock(m_devices_mutex);
            m_devices = std::move(new_devices);
            m_short_to_long_map = std::move(new_short_to_long_map);
            m_nvs_dirty = true;
        }

        ESP_LOGI(TAG, "Discovery finished. Mapped %zu DALI devices.", m_devices.size());
        DaliAddressMap::save(getDevices());
        return found_devices;
    }

    std::optional<DaliLongAddress_t> DaliDeviceController::getInputDeviceLongAddress(const uint8_t shortAddress) const {
        auto& dali = DaliAdapter::Instance();

        auto readBank0Byte = [&](uint8_t offset) -> std::optional<uint8_t> {
            // 103: WRITE DTR1 (0x31), WRITE DTR0 (0x30), READ MEMORY (0xC5)
            dali.sendInputDeviceCommand(shortAddress, static_cast<uint8_t>(Commands::InputDeviceOp::WriteDtr1), 0x00);
            dali.sendInputDeviceCommand(shortAddress, static_cast<uint8_t>(Commands::InputDeviceOp::WriteDtr0), offset);
            return dali.sendInputDeviceCommand(shortAddress, static_cast<uint8_t>(Commands::InputDeviceOp::ReadMemory), std::nullopt);
        };

        const auto h_opt = readBank0Byte(0x09);
        if (!h_opt) return std::nullopt;
        const auto m_opt = readBank0Byte(0x0A);
        if (!m_opt) return std::nullopt;
        const auto l_opt = readBank0Byte(0x0B);
        if (!l_opt) return std::nullopt;

        return (static_cast<DaliLongAddress_t>(*h_opt) << 16) |
               (static_cast<DaliLongAddress_t>(*m_opt) << 8) |
               (*l_opt);
    }

    std::vector<DaliDevice> DaliDeviceController::getDevices() const {
        std::lock_guard<std::mutex> lock(m_devices_mutex);
        return m_devices;
    }

    std::optional<uint8_t> DaliDeviceController::getShortAddress(const DaliLongAddress_t longAddress) const {
        std::lock_guard<std::mutex> lock(m_devices_mutex);
        for(const auto& d : m_devices) {
             if (getIdentity(d).long_address == longAddress) return getIdentity(d).short_address;
        }
        return std::nullopt;
    }

    std::optional<DaliLongAddress_t> DaliDeviceController::getLongAddress(const uint8_t shortAddress, const bool is24bitSpace) const {
        std::lock_guard<std::mutex> lock(m_devices_mutex);
        if (const uint8_t search_key = shortAddress | (is24bitSpace ? 0x80 : 0); m_short_to_long_map[search_key] != 0xFFFFFFFF) {
            return m_short_to_long_map[search_key];
        }
        return std::nullopt;
    }
} // daliMQTT