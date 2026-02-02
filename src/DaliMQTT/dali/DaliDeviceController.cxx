#include "dali/DaliDeviceController.hxx"
#include <dali/DaliGroupManagement.hxx>
#include <utils/StringUtils.hxx>
#include "system/ConfigManager.hxx"
#include "dali/DaliAddressMap.hxx"
#include "dali/DaliAdapter.hxx"
#include "mqtt/MQTTClient.hxx"
#include "utils/DaliLongAddrConversions.hxx"
#include <esp_timer.h>

namespace daliMQTT
{
    static constexpr char TAG[] = "DaliDeviceController";

    void DaliDeviceController::init() {
        ESP_LOGI(TAG, "Initializing DALI Device Controller...");
        if (!DaliAdapter::getInstance().isInitialized()) {
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

        auto& dali_api = DaliAdapter::getInstance();
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
        auto const& mqtt = MQTTClient::getInstance();
        const auto config = ConfigManager::getInstance().getConfig();

        const auto addr_str = utils::longAddressToString(long_addr);
        const std::string state_topic = utils::stringFormat("%s/light/%s/state", config.mqtt_base_topic.c_str(), addr_str.data());
        if (device.current_level == 255) return;

        cJSON* root = cJSON_CreateObject();
        cJSON_AddStringToObject(root, "state", (device.current_level > 0 ? "ON" : "OFF"));
        cJSON_AddNumberToObject(root, "brightness", device.current_level);
        cJSON_AddNumberToObject(root, "status_byte", device.status_byte);

        if (device.color.has_value()) {
            const auto& c = device.color.value();
            if (c.current_tc.has_value()) {
                cJSON_AddNumberToObject(root, "color_temp", c.current_tc.value());
            }
            if (c.current_rgb.has_value()) {
                cJSON* color = cJSON_CreateObject();
                cJSON_AddNumberToObject(color, "r", c.current_rgb->r);
                cJSON_AddNumberToObject(color, "g", c.current_rgb->g);
                cJSON_AddNumberToObject(color, "b", c.current_rgb->b);
                cJSON_AddItemToObject(root, "color", color);
            }
        }

        char* payload = cJSON_PrintUnformatted(root);
        if (payload) {
            ESP_LOGD(TAG, "Publishing to %s: %s", state_topic.c_str(), payload);
            mqtt.publish(state_topic, payload, 0, true);
            free(payload);
        }
        cJSON_Delete(root);
    }

    void DaliDeviceController::publishAvailability(const DaliLongAddress_t long_addr, const bool is_available) {
        auto const& mqtt = MQTTClient::getInstance();
        const auto config = ConfigManager::getInstance().getConfig();

        const auto addr_str = utils::longAddressToString(long_addr);
        const std::string status_topic = utils::stringFormat("%s/light/%s/status", config.mqtt_base_topic.c_str(), addr_str.data());
        const std::string payload = is_available ? CONFIG_DALI2MQTT_MQTT_PAYLOAD_ONLINE : CONFIG_DALI2MQTT_MQTT_PAYLOAD_OFFLINE;
        ESP_LOGD(TAG, "Publishing AVAILABILITY to %s: %s", status_topic.c_str(), payload.c_str());
        mqtt.publish(status_topic, payload, 1, true);
    }

    void DaliDeviceController::updateDeviceState(const DaliLongAddress_t longAddr, const DaliPublishState& state) {
        std::lock_guard<std::mutex> lock(m_devices_mutex);
        const auto it = m_devices.find(longAddr);
        if (it != m_devices.end()) {
            if (auto* gear = std::get_if<ControlGear>(&it->second)) {
                bool state_changed = false;

                if (state.level.has_value()) {
                    const uint8_t lvl = state.level.value();
                    if (lvl > 0) {
                        gear->last_level = lvl;
                    }
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
                    if (!gear->color.has_value()) {
                        gear->color = ColorFeatures();
                    }
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
                    ESP_LOGD(TAG, "State update for %s", utils::longAddressToString(longAddr).data());
                    publishState(longAddr, *gear);
                    gear->initial_sync_needed = false;
                }
            }
        }
    }

    void DaliDeviceController::publishAttributes(const DaliLongAddress_t long_addr) const {
        auto const& mqtt = MQTTClient::getInstance();
        const auto config = ConfigManager::getInstance().getConfig();
        const auto addr_str = utils::longAddressToString(long_addr);

        ControlGear dev_copy;
        {
            std::lock_guard<std::mutex> lock(m_devices_mutex);
            if (!getInstance().m_devices.contains(long_addr)) return;
            if (const auto* gear = std::get_if<ControlGear>(&getInstance().m_devices.at(long_addr))) {
                dev_copy = *gear;
            } else {
                return;
            }
        }
        const std::string attr_topic = utils::stringFormat("%s/light/%s/attributes", config.mqtt_base_topic.c_str(), addr_str.data());

        cJSON* root = cJSON_CreateObject();

        if (dev_copy.device_type.has_value()) {
            cJSON_AddNumberToObject(root, "device_type", dev_copy.device_type.value());
        }

        if (!dev_copy.gtin.empty()) {
            cJSON_AddStringToObject(root, "gtin", dev_copy.gtin.c_str());
        }

        cJSON_AddNumberToObject(root, "dev_min_level", dev_copy.min_level);
        cJSON_AddNumberToObject(root, "dev_max_level", dev_copy.max_level);
        cJSON_AddNumberToObject(root, "dev_power_on_level", dev_copy.power_on_level);
        cJSON_AddNumberToObject(root, "dev_system_failure_level", dev_copy.system_failure_level);
        cJSON_AddNumberToObject(root, "short_address", dev_copy.short_address);

        char* json_str = cJSON_PrintUnformatted(root);
        if (json_str) {
            mqtt.publish(attr_topic, json_str, 1, true);
            free(json_str);
        }
        cJSON_Delete(root);

        ESP_LOGI(TAG, "Published extended attributes for %s", addr_str.data());
    }

    std::optional<uint8_t> DaliDeviceController::getLastLevel(const DaliLongAddress_t longAddress) const {
        std::lock_guard<std::mutex> lock(m_devices_mutex);
        if (const auto it = m_devices.find(longAddress); it != m_devices.end()) {
            if (const auto* gear = std::get_if<ControlGear>(&it->second)) {
                return gear->last_level;
            }
        }
        return std::nullopt;
    }

    bool DaliDeviceController::validateAddressMap() {
        ESP_LOGI(TAG, "Validating cached DALI address map...");
        auto& dali = DaliAdapter::getInstance();

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
            for (const auto& [long_addr, dev_var] : m_devices) {
                const auto& id = getIdentity(dev_var);
                devices_to_validate.push_back({long_addr, id.short_address});
            }
        }

        for (const auto& [long_addr, short_addr] : devices_to_validate) {
            if (auto status_opt = dali.sendQuery(DALI_ADDRESS_TYPE_SHORT, short_addr, DALI_COMMAND_QUERY_STATUS); status_opt.has_value()) {
                if (auto long_addr_from_bus_opt = dali.getLongAddress(short_addr)) {
                    if (*long_addr_from_bus_opt != long_addr) {
                        ESP_LOGW(TAG, "Validation CONFLICT: Short Addr %d has Long Addr %lX, expected %lX. Full scan required.",
                            short_addr, *long_addr_from_bus_opt, long_addr);
                        return false;
                    }
                    {
                        std::lock_guard<std::mutex> lock(m_devices_mutex);
                        if (m_devices.contains(long_addr)) {
                            auto& dev_var = m_devices[long_addr];
                            getIdentity(dev_var).available = true;
                            if (std::holds_alternative<ControlGear>(dev_var)) {
                                publishAvailability(long_addr, true);
                            }
                        }
                    }
                } else {
                    ESP_LOGW(TAG, "Device at SA %d responded to Status but failed Long Addr query. Skipping check.", short_addr);
                }
            } else {
                ESP_LOGD(TAG, "Device at SA %d did not respond (Offline).", short_addr);
            }
            vTaskDelay(pdMS_TO_TICKS(CONFIG_DALI2MQTT_DALI_POLL_DELAY_MS));
        }
        return true;
    }
    [[noreturn]] void DaliDeviceController::daliEventHandlerTask(void* pvParameters) {
        auto* self = static_cast<DaliDeviceController*>(pvParameters);
        const auto& dali_api = DaliAdapter::getInstance();
        const QueueHandle_t queue = dali_api.getEventQueue();
        dali_frame_t frame;

        while (true) {
            if (xQueueReceive(queue, &frame, portMAX_DELAY) == pdPASS) {
                #ifdef CONFIG_DALI2MQTT_SNIFFER_DEBUG_PUBLISH_MQTT
                    {
                        auto const& mqtt = MQTTClient::getInstance();
                        if (mqtt.getStatus() == MqttStatus::CONNECTED) {
                            static std::string cached_topic;
                            if (cached_topic.empty()) {
                                cached_topic = ConfigManager::getInstance().getMqttBaseTopic() + "/debug/sniffer_raw";
                            }

                            char payload_buffer[128];
                            int len = 0;
                            uint32_t timestamp = esp_log_timestamp();

                            if (frame.length == 8) {
                                len = snprintf(payload_buffer, sizeof(payload_buffer),
                                    R"({"type":"backward","len":8,"data":%lu,"hex":"%02lX","ts":%lu})",
                                    frame.data, (frame.data & 0xFF), static_cast<unsigned long>(timestamp));
                            } else if (frame.length == 24) {
                                len = snprintf(payload_buffer, sizeof(payload_buffer),
                                    R"({"type":"forward","len":24,"data":%lu,"hex":"%06lX","ts":%lu})",
                                    frame.data, (frame.data & 0xFFFFFF), static_cast<unsigned long>(timestamp));
                            } else {
                                len = snprintf(payload_buffer, sizeof(payload_buffer),
                                    R"({"type":"forward","len":%u,"data":%lu,"hex":"%04lX","ts":%lu})",
                                    frame.length, frame.data, (frame.data & 0xFFFF), static_cast<unsigned long>(timestamp));
                            }

                            if (len > 0 && len < sizeof(payload_buffer)) {
                                mqtt.publish(cached_topic, std::string(payload_buffer, len), 0, false);
                            }
                        }
                    }
                #endif


                if (frame.length == 24) {
                    self->ProcessInputDeviceFrame(frame);
                } else self->SnifferProcessFrame(frame);
            }
        }
    }
    [[noreturn]] void DaliDeviceController::daliSyncTask(void* pvParameters) {
        auto* self = static_cast<DaliDeviceController*>(pvParameters);
        constexpr int64_t NVS_SAVE_DEBOUNCE_MS = 60000;
        self->requestBroadcastSync(200, 150);

        ESP_LOGI(TAG, "Dali Adaptive Sync Task Started.");
        const auto config = ConfigManager::getInstance().getConfig();

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
                    ESP_LOGI(TAG, "Autosaving updated device map to NVS...");
                    std::map<DaliLongAddress_t, DaliDevice> copy_devs;
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
                if (self->m_round_robin_index >= 64)
                {
                    self->m_round_robin_index = 0;

                    // Sync Group states from device info
                    auto all_assignments = DaliGroupManagement::getInstance().getAllAssignments();
                    std::map<DaliLongAddress_t, DaliDevice> devices_snapshot;
                    {
                        std::lock_guard<std::mutex> lock(self->m_devices_mutex);
                        devices_snapshot = self->m_devices;
                    }
                    std::map<uint8_t, DaliPublishState> group_sync_states;
                    for (const auto& [long_addr, groups] : all_assignments) {
                        if (!devices_snapshot.contains(long_addr)) continue;

                        if (const auto* gear = std::get_if<ControlGear>(&devices_snapshot.at(long_addr))) {
                            if (!gear->available) continue;

                            for (uint8_t group = 0; group < 16; ++group) {
                                if (groups.test(group)) {
                                    if (!group_sync_states.contains(group)) {
                                        group_sync_states[group] = DaliPublishState{ .level = 0 };
                                    }
                                    auto& g_state = group_sync_states[group];
                                    if (gear->current_level > g_state.level.value_or(0)) {
                                        g_state.level = gear->current_level;
                                    }
                                    if (gear->color.has_value())
                                        if (gear->color->supports_rgb && gear->color->current_rgb.has_value()) {
                                        g_state.rgb = gear->color->current_rgb;
                                    }
                                }
                            }
                        }
                    }

                    for(const auto& [group_id, state] : group_sync_states) {
                        DaliGroupManagement::getInstance().updateGroupState(group_id, state);
                    }
                }
                vTaskDelay(rr_delay_ticks);
            }
        }
    }

    void DaliDeviceController::requestDeviceSync(uint8_t shortAddress, uint32_t delay_ms) {
        if (shortAddress >= 64) { return ; }
        std::lock_guard<std::mutex> lock(m_queue_mutex);
        if (delay_ms == 0) {
            if (!m_priority_set.contains(shortAddress)) {
                m_priority_queue.push_back(shortAddress);
                m_priority_set.insert(shortAddress);
                ESP_LOGD(TAG, "Scheduled IMMEDIATE poll for SA: %d", shortAddress);
            }
        } else {
            int64_t now = esp_timer_get_time() / 1000;
            int64_t target_time = now + delay_ms;
            m_deferred_requests.push_back({shortAddress, target_time});
            ESP_LOGD(TAG, "Scheduled DELAYED poll for SA: %d in %u ms", shortAddress, delay_ms);
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
                // Short Address (0AAAAAA1)
                addr_type_str = "short";
                address = (addr_byte >> 1) & 0x3F;
            } else if ((addr_byte & 0xE0) == 0x80) {
                // Group Address (100AAAA1)
                addr_type_str = "group";
                address = (addr_byte >> 1) & 0x0F;
            } else if (addr_byte == 0xC1) {
                // Instance Group (11000001)
                addr_type_str = "instance_group";
                address = 0;
            } else if (addr_byte == 0xFD || addr_byte == 0xFF) {
                // Device Groups broadcast
                 addr_type_str = "broadcast";
            }
        } else {
             // 24-bit Command
             return;
        }
        std::string topic_addr_type = addr_type_str;
        std::string topic_addr_val = std::to_string(address);

        if (addr_type_str == "short") {
            auto long_addr_opt = getLongAddress(address, true);
            if (long_addr_opt.has_value()) {
                topic_addr_type = "long";
                auto la_str = utils::longAddressToString(long_addr_opt.value());
                topic_addr_val = std::string(la_str.data());
            }
        }
        cJSON* root = cJSON_CreateObject();
        cJSON_AddStringToObject(root, "type", "event");
        cJSON_AddStringToObject(root, "address_type", addr_type_str.c_str());
        cJSON_AddNumberToObject(root, "address", address);
        cJSON_AddNumberToObject(root, "instance", instance_byte);
        cJSON_AddNumberToObject(root, "event_code", event_byte);

        #ifdef CONFIG_DALI2MQTT_SNIFFER_DEBUG_PUBLISH_MQTT
            char hex_buf[10];
            snprintf(hex_buf, sizeof(hex_buf), "%06lX", data);
            cJSON_AddStringToObject(root, "raw_hex", hex_buf);
        #endif

        if (topic_addr_type == "long") {
            cJSON_AddStringToObject(root, "long_addr", topic_addr_val.c_str());
        }
        char* json_payload = cJSON_PrintUnformatted(root);
        cJSON_Delete(root);

        if (json_payload) {
            auto const& mqtt = MQTTClient::getInstance();
            const auto config = ConfigManager::getInstance().getConfig();

            std::string topic = utils::stringFormat("%s/event/%s/%s", // base/event/{address_type}/{address_str}
                config.mqtt_base_topic.c_str(),
                topic_addr_type.c_str(),
                topic_addr_val.c_str()
            );
            mqtt.publish(topic, json_payload, 0, false);
            ESP_LOGD(TAG, "Input Device Event Published: %s -> %s", topic.c_str(), json_payload);
            free(json_payload);
        }
    }

    void DaliDeviceController::requestBroadcastSync(const uint32_t base_delay_ms, const uint32_t stagger_ms) {
        std::lock_guard<std::mutex> lock(m_devices_mutex);
        ESP_LOGI(TAG, "Scheduling broadcast sync for %zu devices (Base: %ums, Stagger: %ums)",
                         m_devices.size(), base_delay_ms, stagger_ms);
        uint32_t current_delay = base_delay_ms;
        for (const auto &dev_var: m_devices | std::views::values) {
            if (std::holds_alternative<ControlGear>(dev_var)) {
                requestDeviceSync(getIdentity(dev_var).short_address, current_delay);
                current_delay += stagger_ms;
            }
        }
    }

 void DaliDeviceController::pollSingleDevice(const uint8_t shortAddr) {
        DaliLongAddress_t long_addr = 0;
        bool known_device = false;
        {
            std::lock_guard<std::mutex> lock(m_devices_mutex);
            if (const auto it = m_short_to_long_map.find(shortAddr); it != m_short_to_long_map.end()) {
                long_addr = it->second;
                known_device = true;
            }
        }
        if (!known_device) {
            return;
        }

        auto& dali = DaliAdapter::getInstance();
        const auto level_opt = dali.sendQuery(DALI_ADDRESS_TYPE_SHORT, shortAddr, DALI_COMMAND_QUERY_ACTUAL_LEVEL);
        const bool is_device_responding = level_opt.has_value();

        {
            std::lock_guard<std::mutex> lock(m_devices_mutex);
            if (m_devices.contains(long_addr)) {
                auto& dev_var = m_devices[long_addr];
                auto& id = getIdentity(dev_var);
                if (id.available != is_device_responding) {
                    id.available = is_device_responding;
                    if (std::holds_alternative<ControlGear>(dev_var)) {
                        publishAvailability(long_addr, is_device_responding);
                    }
                }
            } else {
                return;
            }
        }

        if (!is_device_responding) return;

        ControlGear* gear = nullptr;
        {
            std::lock_guard<std::mutex> lock(m_devices_mutex);
            if (m_devices.contains(long_addr)) {
                gear = std::get_if<ControlGear>(&m_devices[long_addr]);
            }
        }

        if (!gear) return; // Not a ControlGear or device removed

        const auto status_opt = dali.getDeviceStatus(shortAddr);

        bool is_dt8 = false;
        bool features_known = false;
        bool supports_tc = false;
        bool supports_rgb = false;
        bool is_initial_sync = false;
        uint8_t current_cached_level = 0;

        {
             std::lock_guard<std::mutex> lock(m_devices_mutex);
             if (m_devices.contains(long_addr)) {
                 if (auto* safe_gear = std::get_if<ControlGear>(&m_devices[long_addr])) {
                     current_cached_level = safe_gear->current_level;
                     is_initial_sync = safe_gear->initial_sync_needed;
                     if (safe_gear->device_type.has_value() && safe_gear->device_type.value() == 8) {
                         is_dt8 = true;
                         features_known = safe_gear->static_data_loaded;
                         if (safe_gear->color.has_value()) {
                             supports_tc = safe_gear->color->supports_tc;
                             supports_rgb = safe_gear->color->supports_rgb;
                         }
                     }
                 }
             }
        }

        if (is_dt8 && !features_known) {
            auto features_opt = dali.getDT8Features(shortAddr);
            if (features_opt.has_value()) {
                uint8_t feat = *features_opt;
                supports_tc = (feat & 0x02) != 0;
                supports_rgb = (feat & 0x08) != 0;
                ESP_LOGD(TAG, "DT8 Device %d features: Tc=%d, RGB=%d (Raw: 0x%02X)",
                    shortAddr, supports_tc, supports_rgb, feat);
                {
                    std::lock_guard<std::mutex> lock(m_devices_mutex);
                    if (m_devices.contains(long_addr)) {
                        if (auto* g = std::get_if<ControlGear>(&m_devices[long_addr])) {
                            if (!g->color.has_value()) g->color = ColorFeatures();
                            g->color->supports_tc = supports_tc;
                            g->color->supports_rgb = supports_rgb;
                            m_nvs_dirty = true;
                            m_last_nvs_change_ts = esp_timer_get_time() / 1000;
                        }
                    }
                }
            }
        }

        const uint8_t actual_level = (level_opt.value() == 255) ? current_cached_level : level_opt.value();
        std::optional<uint16_t> polled_tc = std::nullopt;
        std::optional<DaliRGB> polled_rgb = std::nullopt;

        const int64_t now_sec = esp_timer_get_time() / 1000000;
        constexpr int64_t COLOR_POLL_INTERVAL_SEC = 60;
        bool should_poll_color = false;

        {
            std::lock_guard<std::mutex> lock(m_devices_mutex);
            if (auto* g = std::get_if<ControlGear>(&m_devices[long_addr])) {
                if (actual_level > 0 && g->color.has_value()) {
                    if (g->color->supports_tc || g->color->supports_rgb) {
                        if ((now_sec - g->color->last_poll_ts) > 60) {
                            should_poll_color = true;
                        }
                    }
                }
            }
        }

        if (should_poll_color) {
            bool poll_tc = supports_tc;
            bool poll_rgb = supports_rgb;

            if (poll_tc) {
                polled_tc = dali.getDT8ColorTemp(shortAddr);
            }
            if (poll_rgb) {
                polled_rgb = dali.getDT8RGB(shortAddr);
            }

            {
                std::lock_guard<std::mutex> lock(m_devices_mutex);
                if(m_devices.contains(long_addr)) {
                    if (auto* g = std::get_if<ControlGear>(&m_devices[long_addr])) {
                        if (g->color) g->color->last_poll_ts = now_sec;
                    }
                }
            }
        }

        updateDeviceState(long_addr, {
            .level = actual_level,
            .status_byte = status_opt,
            .color_temp = polled_tc,
            .rgb = polled_rgb,
        });

        if (is_initial_sync) {
            auto groups_opt = DaliGroupManagement::getInstance().getGroupsForDevice(long_addr);
            if (groups_opt) {
                for (uint8_t i = 0; i < 16; ++i) {
                    if (groups_opt->test(i)) {
                        auto current_grp = DaliGroupManagement::getInstance().getGroupState(i);
                        DaliPublishState groupUpdate;
                        if (actual_level > current_grp.current_level) {
                            groupUpdate.level = actual_level;
                        }
                        if (is_dt8) {
                            groupUpdate.color_temp = polled_tc;
                            groupUpdate.rgb = polled_rgb;
                        }
                        if (groupUpdate.level.has_value() || groupUpdate.color_temp.has_value() || groupUpdate.rgb.has_value()) {
                            DaliGroupManagement::getInstance().updateGroupState(i, groupUpdate);
                        }
                    }
                }
            }
        }

        bool needs_static_data = false;
        {
            std::lock_guard<std::mutex> lock(m_devices_mutex);
            if (m_devices.contains(long_addr)) {
                 if (auto* g = std::get_if<ControlGear>(&m_devices[long_addr])) {
                     if (!g->static_data_loaded) needs_static_data = true;
                 }
            }
        }

        if (needs_static_data) {
            const auto min_opt = dali.sendQuery(DALI_ADDRESS_TYPE_SHORT, shortAddr, DALI_COMMAND_QUERY_MIN_LEVEL);
            const auto max_opt = dali.sendQuery(DALI_ADDRESS_TYPE_SHORT, shortAddr, DALI_COMMAND_QUERY_MAX_LEVEL);
            const auto power_on_opt = dali.sendQuery(DALI_ADDRESS_TYPE_SHORT, shortAddr, DALI_COMMAND_QUERY_POWER_ON_LEVEL);
            const auto fail_opt = dali.sendQuery(DALI_ADDRESS_TYPE_SHORT, shortAddr, DALI_COMMAND_QUERY_SYSTEM_FAILURE_LEVEL);
            const auto gtin_opt = dali.getGTIN(shortAddr);
            const auto dt_opt = dali.getDeviceType(shortAddr);

            {
                std::lock_guard<std::mutex> lock(m_devices_mutex);
                if(m_devices.contains(long_addr)) {
                    if (auto* g = std::get_if<ControlGear>(&m_devices[long_addr])) {
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
                }
            }
            publishAttributes(long_addr);
        }
    }

    std::bitset<64> DaliDeviceController::performFullInitialization() {
        if (!DaliAdapter::getInstance().isInitialized()) {
            ESP_LOGE(TAG, "Cannot initialize DALI bus: DALI driver is not initialized.");
            return {};
        }
        DaliAdapter::getInstance().initializeBus();
        return discoverAndMapDevices();
    }

    std::bitset<64> DaliDeviceController::perform24BitDeviceInitialization() {
        if (!DaliAdapter::getInstance().isInitialized()) {
            ESP_LOGE(TAG, "Cannot initialize DALI bus: DALI driver is not initialized.");
            return {};
        }
        DaliAdapter::getInstance().initialize24BitDevicesBus();
        return discoverAndMapDevices();
    }

    std::bitset<64> DaliDeviceController::performScan() {
        if (!DaliAdapter::getInstance().isInitialized()) {
            ESP_LOGE(TAG, "Cannot scan DALI bus: DALI driver is not initialized.");
            return {};
        }
        return discoverAndMapDevices();
    }

    std::bitset<64> DaliDeviceController::discoverAndMapDevices() {
        ESP_LOGI(TAG, "Starting DALI device discovery and mapping...");
        auto& dali = DaliAdapter::getInstance();
        std::map<DaliLongAddress_t, DaliDevice> new_devices;
        std::map<uint8_t, DaliLongAddress_t> new_short_to_long_map;
        std::bitset<64> found_devices;

        for (uint8_t sa = 0; sa < 64; ++sa) {
            if (auto status_opt = dali.sendQuery(DALI_ADDRESS_TYPE_SHORT, sa, DALI_COMMAND_QUERY_STATUS); status_opt.has_value()) {
                found_devices.set(sa);
                ESP_LOGI(TAG, "Gear found at SA %d", sa);
                if (auto long_addr_opt = dali.getLongAddress(sa)) {
                    DaliLongAddress_t long_addr = *long_addr_opt;
                    ControlGear dev;
                    dev.long_address = long_addr;
                    dev.short_address = sa;
                    dev.available = true;
                    new_devices.emplace(long_addr, dev);
                    new_short_to_long_map[sa] = long_addr;
                }
            }

            if (auto status_input_opt = dali.sendInputDeviceCommand(sa, DALI_COMMAND_INPUT_QUERY_STATUS); status_input_opt.has_value()) {
                ESP_LOGI(TAG, "Input Device found at SA %d", sa);
                auto long_addr_opt = getInputDeviceLongAddress(sa);
                DaliLongAddress_t long_addr;
                if (long_addr_opt.has_value()) {
                    long_addr = *long_addr_opt;
                } else {
                    long_addr = 0xFE0000 | sa; // Pseudo addr fallback
                }

                InputDevice dev;
                dev.long_address = long_addr;
                dev.short_address = sa;
                dev.available = true;

                new_devices.emplace(long_addr, dev);
                new_short_to_long_map[sa | 0x80] = long_addr;
            }
            vTaskDelay(pdMS_TO_TICKS(CONFIG_DALI2MQTT_DALI_POLL_DELAY_MS));
        }

        {
            std::lock_guard<std::mutex> lock(m_devices_mutex);
            m_devices = std::move(new_devices);
            m_short_to_long_map = std::move(new_short_to_long_map);
            ESP_LOGI(TAG, "Discovery finished. Mapped %zu DALI devices.", m_devices.size());
            DaliAddressMap::save(m_devices);
            m_nvs_dirty = false;
        }
        return found_devices;
    }

    std::optional<DaliLongAddress_t> DaliDeviceController::getInputDeviceLongAddress(const uint8_t shortAddress) const {
        auto& dali = DaliAdapter::getInstance();
        auto readByte = [&](uint8_t offset) -> std::optional<uint8_t> {
            dali.sendInputDeviceCommand(shortAddress, 0x31, 0x00); // Bank 0
            dali.sendInputDeviceCommand(shortAddress, 0x30, offset); // Offset
            return dali.sendInputDeviceCommand(shortAddress, DALI_COMMAND_INPUT_READ_MEMORY_LOCATION);
        };

        const auto h_opt = readByte(0x09);
        if (!h_opt) return std::nullopt;
        const auto m_opt = readByte(0x0A);
        if (!m_opt) return std::nullopt;
        const auto l_opt = readByte(0x0B);
        if (!l_opt) return std::nullopt;

        return (static_cast<DaliLongAddress_t>(*h_opt) << 16) |
               (static_cast<DaliLongAddress_t>(*m_opt) << 8) |
               (*l_opt);
    }

    std::map<DaliLongAddress_t, DaliDevice> DaliDeviceController::getDevices() const {
        std::lock_guard<std::mutex> lock(m_devices_mutex);
        return m_devices;
    }

    std::optional<uint8_t> DaliDeviceController::getShortAddress(const DaliLongAddress_t longAddress) const {
        std::lock_guard<std::mutex> lock(m_devices_mutex);
        if (const auto it = m_devices.find(longAddress); it != m_devices.end()) {
            return getIdentity(it->second).short_address;
        }
        return std::nullopt;
    }

    std::optional<DaliLongAddress_t> DaliDeviceController::getLongAddress(const uint8_t shortAddress, const bool is24bitSpace) const {
        std::lock_guard<std::mutex> lock(m_devices_mutex);
        uint8_t search_key = shortAddress;
        if (is24bitSpace) {
            search_key |= 0x80;
        }
        if (const auto it = m_short_to_long_map.find(search_key); it != m_short_to_long_map.end()) {
            return it->second;
        }
        return std::nullopt;
    }
} // daliMQTT