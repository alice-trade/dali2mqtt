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
        applyBusConfiguration();
        m_internal_to_long_map.fill(0xFFFFFFFF);

        bool map_loaded = DaliAddressMap::load(m_devices, m_internal_to_long_map);
        if (!map_loaded || !validateAddressMap()) {
            ESP_LOGI(TAG, "Cached address map is invalid or missing. Performing a full scan.");
            scanAllActiveBuses();
        } else {
            ESP_LOGI(TAG, "Successfully loaded and validated cached DALI address map.");
        }

    }

    void DaliDeviceController::applyBusConfiguration() {
        auto cfg = ConfigManager::Instance().getConfig();
        for (uint8_t i = 0; i < Constants::MaxBuses; ++i) {
            if (cfg.buses[i].enabled) {
                if (!m_adapters[i]) {
                    m_adapters[i] = std::make_unique<DaliAdapter>(i, m_central_event_queue);
                    m_adapters[i]->init(static_cast<gpio_num_t>(cfg.buses[i].rx_pin), static_cast<gpio_num_t>(cfg.buses[i].tx_pin));
                }
            } else {
                if (m_adapters[i]) m_adapters[i].reset();
            }
        }
    }

    DaliAdapter* DaliDeviceController::getAdapter(uint8_t bus_id) {
        if (bus_id < Constants::MaxBuses) return m_adapters[bus_id].get();
        return nullptr;
    }

    void DaliDeviceController::start() {
        if (!m_event_handler_task) xTaskCreate(daliEventHandlerTask, "dali_event", 4096, this, 5, &m_event_handler_task);
        if (!m_sync_task_handle) xTaskCreate(daliSyncTask, "dali_sync", 6144, this, 4, &m_sync_task_handle);
        for(auto& adapter : m_adapters) { if (adapter) adapter->startSniffer(); }
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
        doc["driverId"] = extractBusId(dev_copy.internal_address);
        doc["short_address"] = extractShortAddr(dev_copy.internal_address);

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

        struct ValidationItem {
            DaliLongAddress_t long_addr;
            uint16_t internal_addr;
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
                devices_to_validate.push_back({id.long_address, id.internal_address});
            }
        }

        for (const auto& [long_addr, int_addr] : devices_to_validate) {
            auto* adapter = daliMQTT::DaliDeviceController::getAdapter(extractBusId(int_addr));
            if (!adapter || !adapter->isInitialized()) continue;

            if (auto status_opt = adapter->sendQuery(DaliAddressType::Short, extractShortAddr(int_addr), Commands::OpCode::QueryStatus); status_opt.has_value()) {
                if (auto long_addr_from_bus_opt = adapter->getLongAddress(extractShortAddr(int_addr))) {
                    if (*long_addr_from_bus_opt != long_addr) {
                        ESP_LOGW(TAG, "Validation CONFLICT: Short Addr %d has Long Addr %lX, expected %lX. Full scan required.",
                            extractShortAddr(int_addr), *long_addr_from_bus_opt, long_addr);
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
        dali_frame_t frame{};

        while (true) {
            if (xQueueReceive(self->m_central_event_queue, &frame, portMAX_DELAY) == pdPASS) {
#ifdef CONFIG_DALI2MQTT_SNIFFER_DEBUG_PUBLISH_MQTT
                auto const& mqtt = MQTTClient::Instance();
                if (mqtt.getStatus() == MqttStatus::CONNECTED) {
                    char topic[128];
                    snprintf(topic, sizeof(topic), "%s/debug/sniffer_raw", ConfigManager::Instance().getConfig().mqtt_base_topic.c_str());

                    char payload[128];
                    uint32_t ts = esp_log_timestamp();
                    if (frame.length == 8) {
                        snprintf(payload, sizeof(payload), R"({"type":"backward","len":8,"data":%lu,"bus":%d,"ts":%lu})", frame.data, frame.bus_id, ts);
                    } else {
                        snprintf(payload, sizeof(payload), R"({"type":"forward","len":%u,"data":%lu,"bus":%d,"ts":%lu})", frame.length, frame.data, frame.bus_id, ts);
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
        constexpr int64_t NVS_SAVE_DEBOUNCE_MS = 180000;
        self->requestBroadcastSync(200, 150);

        ESP_LOGI(TAG, "Dali Adaptive Sync Task Started.");
        const auto config = ConfigManager::Instance().getConfig();

        const uint32_t safe_cycle_time = std::max<uint32_t>(1000, config.dali_poll_interval_ms);
        const uint32_t calc_delay_ms = safe_cycle_time >> 6;
        const TickType_t rr_delay_ticks = pdMS_TO_TICKS(std::max<uint32_t>(20, calc_delay_ms));
        constexpr TickType_t priority_delay_ticks = pdMS_TO_TICKS(10);

        while (true) {
            uint16_t priority_addr = 0xFFFF;
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
                            if (!self->m_priority_set.contains(it->internal_address)) {
                                self->m_priority_queue.push_back(it->internal_address);
                                self->m_priority_set.insert(it->internal_address);
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

            if (has_priority && priority_addr != 0xFFFF) {
                self->pollSingleDevice(priority_addr);
                vTaskDelay(priority_delay_ticks);
            } else {
                // Round Robin Logic
                uint8_t current_bus = self->m_round_robin_index / 64;
                uint8_t current_sa  = self->m_round_robin_index % 64;

                if (auto* adapter = self->getAdapter(current_bus); adapter && adapter->isInitialized()) {
                    self->pollSingleDevice(packInternalAddr(current_bus, current_sa));
                }

                self->m_round_robin_index++;
                if (self->m_round_robin_index >= (64 * Constants::MaxBuses)) {
                    self->m_round_robin_index = 0;

                    // Sync Group states from device info
                    auto all_assignments = DaliGroupManagement::Instance().getAllAssignments();
                    std::vector<DaliDevice> devices_snapshot;
                    {
                        std::lock_guard<std::mutex> lock(self->m_devices_mutex);
                        devices_snapshot = self->m_devices;
                    }
                    std::array<std::array<std::optional<DaliPublishState>, 16>, Constants::MaxBuses> group_sync_states;

                    for (const auto& [long_addr, groups] : all_assignments) {
                        for (const auto& dev : devices_snapshot) {
                            if (getIdentity(dev).long_address == long_addr) {
                                if (const auto* gear = std::get_if<ControlGear>(&dev)) {
                                    if (!gear->available) break;
                                    uint8_t bus_id = extractBusId(gear->internal_address);

                                    for (uint8_t group = 0; group < 16; ++group) {
                                        if (groups.test(group)) {
                                            if (!group_sync_states[bus_id][group].has_value()) {
                                                group_sync_states[bus_id][group] = DaliPublishState{ .level = 0 };
                                            }

                                            auto& g_state = group_sync_states[bus_id][group].value();

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

                    for (uint8_t b = 0; b < Constants::MaxBuses; ++b) {
                        for (uint8_t g = 0; g < 16; ++g) {
                            if (group_sync_states[b][g].has_value()) {
                                DaliGroupManagement::Instance().updateGroupState(b, g, group_sync_states[b][g].value());
                            }
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
            if (const auto long_addr_opt = getLongAddress(packInternalAddr(frame.bus_id, address))) {
                addr_type_str = "long";
                auto la_str = utils::longAddressToString(*long_addr_opt);
                snprintf(topic_addr_val, sizeof(topic_addr_val), "%s", la_str.data());
            }
        }

            JsonDocument doc;
            doc["type"] = "event";
            doc["address_type"] = addr_type_str;
            doc["driverId"] = frame.bus_id;
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

    void DaliDeviceController::requestDeviceSync(const uint16_t internalAddress, const uint32_t delay_ms) {
        if (extractShortAddr(internalAddress) >= 64)
            return;
        std::lock_guard<std::mutex> lock(m_queue_mutex);
        if (delay_ms == 0) {
            if (!m_priority_set.contains(internalAddress)) {
                m_priority_queue.push_back(internalAddress);
                m_priority_set.insert(internalAddress);
            }
        } else {
            int64_t now = esp_timer_get_time() / 1000;
            m_deferred_requests.push_back({internalAddress, now + delay_ms});
        }
    }

    void DaliDeviceController::requestBroadcastSync(const uint32_t base_delay_ms, const uint32_t stagger_ms) {
        std::lock_guard<std::mutex> lock(m_devices_mutex);
        uint32_t current_delay = base_delay_ms;
        for (const auto &dev_var: m_devices) {
            if (std::holds_alternative<ControlGear>(dev_var)) {
                requestDeviceSync(getIdentity(dev_var).internal_address, current_delay);
                current_delay += stagger_ms;
            }
        }
    }

    std::optional<uint8_t> DaliDeviceController::pollAvailabilityAndLevel(const uint16_t internalAddr, const DaliLongAddress_t longAddr) {
        const auto* adapter = daliMQTT::DaliDeviceController::getAdapter(extractBusId(internalAddr));
        if (!adapter || !adapter->isInitialized()) return std::nullopt;

        const auto level_opt = adapter->sendQuery(DaliAddressType::Short, extractShortAddr(internalAddr), Commands::OpCode::QueryActualLevel);
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

    void DaliDeviceController::checkDT8Features(const uint16_t internalAddr, const DaliLongAddress_t longAddr) {
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
        auto* adapter = daliMQTT::DaliDeviceController::getAdapter(extractBusId(internalAddr));
        if (!adapter) return;

        const auto colour_type = adapter->readMemoryLocation(extractShortAddr(internalAddr), 205, Commands::MemBank205::ColourType);

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

    DaliDeviceController::ColorPollResult DaliDeviceController::pollColorDataCyclic(const uint16_t internalAddr, const DaliLongAddress_t longAddr, const uint8_t current_level) {
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

        auto* adapter = daliMQTT::DaliDeviceController::getAdapter(extractBusId(internalAddr));
        if (!adapter) return result;

        const uint8_t shortAddr = extractShortAddr(internalAddr);
        if (tc_supp) {
            auto h = adapter->readMemoryLocation(shortAddr, 205, Commands::MemBank205::ColourValueTcH);
            auto l = adapter->readMemoryLocation(shortAddr, 205, Commands::MemBank205::ColourValueTcL);
            if (h && l) result.tc = (*h << 8) | *l;
        }
        if (rgb_supp) {
            auto r = adapter->readMemoryLocation(shortAddr, 205, Commands::MemBank205::RgbR);
            auto g = adapter->readMemoryLocation(shortAddr, 205, Commands::MemBank205::RgbG);
            auto b = adapter->readMemoryLocation(shortAddr, 205, Commands::MemBank205::RgbB);
            if (r && g && b) result.rgb = DaliRGB{*r, *g, *b};
        }
        return result;
    }


    void DaliDeviceController::performInitialGroupSync(const DaliLongAddress_t longAddr, const uint8_t level, const ColorPollResult& colorData) {
        bool is_initial_sync = false;
        bool is_dt8 = false;
        uint8_t bus_id = 0;
        {
            std::lock_guard<std::mutex> lock(m_devices_mutex);
            for(auto& dev : m_devices) {
                if(getIdentity(dev).long_address == longAddr) {
                    if (const auto* gear = std::get_if<ControlGear>(&dev)) {
                        is_initial_sync = gear->initial_sync_needed;
                        is_dt8 = gear->color.has_value();
                        bus_id = extractBusId(gear->internal_address);
                    }
                    break;
                }
            }
        }
        if (is_initial_sync) {
            if (const auto groups_opt = DaliGroupManagement::Instance().getGroupsForDevice(longAddr)) {
                for (uint8_t i = 0; i < 16; ++i) {
                    if (groups_opt->test(i)) {
                        const auto current_grp = DaliGroupManagement::Instance().getGroupState(bus_id, i);
                        DaliPublishState groupUpdate;
                        if (level > current_grp.current_level) groupUpdate.level = level;
                        if (is_dt8) {
                            groupUpdate.color_temp = colorData.tc;
                            groupUpdate.rgb = colorData.rgb;
                        }
                        if (groupUpdate.level.has_value() || groupUpdate.color_temp.has_value() || groupUpdate.rgb.has_value()) {
                            DaliGroupManagement::Instance().updateGroupState(bus_id, i, groupUpdate);
                        }
                    }
                }
            }
        }
    }

    void DaliDeviceController::initialStaticDataFetch(const uint16_t internalAddr, const DaliLongAddress_t longAddr) {
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

        auto* adapter = daliMQTT::DaliDeviceController::getAdapter(extractBusId(internalAddr));
        if (!adapter) return;

        const uint8_t shortAddr = extractShortAddr(internalAddr);
        const auto min_opt = adapter->sendQuery(DaliAddressType::Short, shortAddr, Commands::OpCode::QueryMinLevel);
        const auto max_opt = adapter->sendQuery(DaliAddressType::Short, shortAddr, Commands::OpCode::QueryMaxLevel);
        const auto power_on_opt = adapter->sendQuery(DaliAddressType::Short, shortAddr, Commands::OpCode::QueryPowerOnLevel);
        const auto fail_opt = adapter->sendQuery(DaliAddressType::Short, shortAddr, Commands::OpCode::QuerySystemFailureLevel);
        const auto gtin_opt = adapter->getGTIN(shortAddr);
        const auto dt_opt = adapter->getDeviceType(shortAddr);

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

    void DaliDeviceController::pollSingleDevice(const uint16_t InternalAddr) {
        DaliLongAddress_t longAddr = 0;
        if (auto la_opt = getLongAddress(InternalAddr)) longAddr = *la_opt;
        else return;

        const auto levelOpt = pollAvailabilityAndLevel(InternalAddr, longAddr);
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

        uint8_t shortAddr = extractShortAddr(InternalAddr);
        auto* adapter = getAdapter(extractBusId(InternalAddr));
        if(!adapter) return;

        const auto statusOpt = adapter->getDeviceStatus(shortAddr);

        // Part 207 (?)
        if (statusOpt.has_value() && (*statusOpt & 0x02)) {
            auto led_faults = adapter->readMemoryLocation(shortAddr, 1, 0x02); // TODO: Pass LED Fault
            if (led_faults) ESP_LOGW(TAG, "Device %d LED Fault Bits: 0x%02X", shortAddr, *led_faults);
        }

        checkDT8Features(InternalAddr, longAddr);
        const auto colorData = pollColorDataCyclic(InternalAddr, longAddr, *levelOpt);

        updateDeviceState(longAddr, {
            .level = *levelOpt,
            .status_byte = statusOpt,
            .color_temp = colorData.tc,
            .rgb = colorData.rgb,
        });

        performInitialGroupSync(longAddr, *levelOpt, colorData);
        initialStaticDataFetch(InternalAddr, longAddr);
    }

    std::bitset<64> DaliDeviceController::performFullInitialization() {
        std::bitset<64> overall_devices;
        for(uint8_t i=0; i < Constants::MaxBuses; ++i) {
            auto* adapter = getAdapter(i);
            if (adapter && adapter->isInitialized()) {
                adapter->initializeBus();
            }
        }
        scanAllActiveBuses();
        return overall_devices;
    }

    std::bitset<64> DaliDeviceController::perform24BitDeviceInitialization() {
        std::bitset<64> overall_devices;
        for(uint8_t i=0; i < Constants::MaxBuses; ++i) {
            auto* adapter = getAdapter(i);
            if (adapter && adapter->isInitialized()) {
                adapter->initialize24BitDevicesBus();
            }
        }
        scanAllActiveBuses();
        return overall_devices;
    }

    std::bitset<64> DaliDeviceController::performScan() {
        scanAllActiveBuses();
        return {};
    }

    std::bitset<64> DaliDeviceController::discoverAndMapDevices(uint8_t bus_id) {
        auto* adapter = getAdapter(bus_id);
        if(!adapter || !adapter->isInitialized()) return {};
        ESP_LOGI(TAG, "Discovering on Bus %d...", bus_id);

        std::vector<DaliDevice> new_devices;
        new_devices.reserve(64);
        std::bitset<64> found_devices;

        for (uint8_t sa = 0; sa < 64; ++sa) {
            if (adapter->sendQuery(DaliAddressType::Short, sa, Commands::OpCode::QueryStatus).has_value()) {
                found_devices.set(sa);
                if (auto long_addr_opt = adapter->getLongAddress(sa)) {
                    DaliLongAddress_t long_addr = *long_addr_opt;
                    ControlGear dev;
                    dev.long_address = long_addr;
                    dev.internal_address  = packInternalAddr(bus_id, sa);
                    dev.available = true;
                    new_devices.emplace_back(dev);
                    m_internal_to_long_map[(bus_id * 256) + sa] = *long_addr_opt;
                    ESP_LOGI(TAG, "Gear found at SA %d (LA: 0x%06lX)", sa, long_addr);
                }
            }

            if (adapter->sendInputDeviceCommand(sa, std::to_underlying(Commands::InputDeviceOp::QueryStatus), 0x00)) {
                auto long_addr_opt = getInputDeviceLongAddress(packInternalAddr(bus_id, sa));
                DaliLongAddress_t long_addr = long_addr_opt.value_or(0xFE0000 | sa);

                InputDevice dev;
                dev.long_address = long_addr;
                dev.internal_address = packInternalAddr(bus_id, sa);;
                dev.available = true;
                new_devices.emplace_back(dev);
                m_internal_to_long_map[((bus_id * 256) + sa) | 0x80] = long_addr;

                ESP_LOGI(TAG, "Input Device found at SA (CD) %d (LA: 0x%06lX)", sa, long_addr);
            }
            vTaskDelay(pdMS_TO_TICKS(CONFIG_DALI2MQTT_DALI_POLL_DELAY_MS));
        }

        {
            std::lock_guard<std::mutex> lock(m_devices_mutex);
            for(auto& d : new_devices) m_devices.push_back(d);
            m_nvs_dirty = true;
        }

        return found_devices;
    }

    void DaliDeviceController::scanAllActiveBuses() {
        {
            std::lock_guard<std::mutex> lock(m_devices_mutex);
            m_devices.clear();
        }
        for (uint8_t i = 0; i < Constants::MaxBuses; ++i) {
            if (m_adapters[i] && m_adapters[i]->isInitialized()) {
                discoverAndMapDevices(i);
            }
        }
        ESP_LOGI(TAG, "Discovery finished. Mapped %zu DALI devices total.", m_devices.size());
        DaliAddressMap::save(getDevices());
    }

    std::optional<DaliLongAddress_t> DaliDeviceController::getInputDeviceLongAddress(const uint16_t internalAddress) const {
        const auto* adapter = DaliDeviceController::Instance().getAdapter(extractBusId(internalAddress));
        if(!adapter) return std::nullopt;

        const uint8_t shortAddress = extractShortAddr(internalAddress);
        auto readBank0Byte = [&](uint8_t offset) -> std::optional<uint8_t> {
            adapter->sendInputDeviceCommand(shortAddress, static_cast<uint8_t>(Commands::InputDeviceOp::WriteDtr1), 0x00);
            adapter->sendInputDeviceCommand(shortAddress, static_cast<uint8_t>(Commands::InputDeviceOp::WriteDtr0), offset);
            return adapter->sendInputDeviceCommand(shortAddress, static_cast<uint8_t>(Commands::InputDeviceOp::ReadMemory), std::nullopt);
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

    std::optional<uint16_t> DaliDeviceController::getInternalAddress(const DaliLongAddress_t longAddress) const {
        std::lock_guard<std::mutex> lock(m_devices_mutex);
        for(const auto& d : m_devices) {
            if (getIdentity(d).long_address == longAddress) return getIdentity(d).internal_address;
        }
        return std::nullopt;
    }

    std::optional<DaliLongAddress_t> DaliDeviceController::getLongAddress(const uint16_t internalAddress) const {
        uint8_t bus_id = extractBusId(internalAddress);
        uint8_t short_addr = extractShortAddr(internalAddress);
        size_t map_idx = (bus_id * 256) + short_addr;

        std::lock_guard<std::mutex> lock(m_devices_mutex);
        if (m_internal_to_long_map[map_idx] != 0xFFFFFFFF) {
            return m_internal_to_long_map[map_idx];
        }
        return std::nullopt;
    }
} // daliMQTT