#include "DaliDeviceController.hxx"
#include <DaliGroupManagement.hxx>
#include <utils/StringUtils.hxx>
#include "ConfigManager.hxx"
#include "DaliAddressMap.hxx"
#include "DaliAPI.hxx"
#include "MQTTClient.hxx"
#include "utils/DaliLongAddrConversions.hxx"
#include <esp_timer.h>
namespace daliMQTT
{
    static constexpr char TAG[] = "DaliDeviceController";

    void DaliDeviceController::init() {
        ESP_LOGI(TAG, "Initializing DALI Device Controller...");
        if (!DaliAPI::getInstance().isInitialized()) {
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

        auto& dali_api = DaliAPI::getInstance();
        if (dali_api.isInitialized()) {
            dali_api.startSniffer();
            xTaskCreate(daliEventHandlerTask, "dali_event_handler", 4096, this, 5, &m_event_handler_task);
            xTaskCreate(daliSyncTask, "dali_sync", 4096, this, 4, &m_sync_task_handle);
            ESP_LOGI(TAG, "DALI monitoring and sync tasks started.");
        } else {
            ESP_LOGE(TAG, "Cannot start DALI tasks: DaliAPI not initialized.");
        }
    }
    void DaliDeviceController::publishState(const DaliLongAddress_t long_addr, const DaliDevice& device) {
        auto const& mqtt = MQTTClient::getInstance();
        const auto config = ConfigManager::getInstance().getConfig();

        const auto addr_str = utils::longAddressToString(long_addr);
        const std::string state_topic = utils::stringFormat("%s/light/%s/state", config.mqtt_base_topic.c_str(), addr_str.data());
        if (device.current_level == 255) return;

        cJSON* root = cJSON_CreateObject();
        cJSON_AddStringToObject(root, "state", (device.current_level > 0 ? "ON" : "OFF"));
        cJSON_AddNumberToObject(root, "brightness", device.current_level);
        cJSON_AddNumberToObject(root, "status_byte", device.status_byte);

        if (device.color_temp.has_value()) {
            cJSON_AddNumberToObject(root, "color_temp", device.color_temp.value());
        }
        if (device.rgb.has_value()) {
            cJSON* color = cJSON_CreateObject();
            cJSON_AddNumberToObject(color, "r", device.rgb->r);
            cJSON_AddNumberToObject(color, "g", device.rgb->g);
            cJSON_AddNumberToObject(color, "b", device.rgb->b);
            cJSON_AddItemToObject(root, "color", color);
        }

        char* payload = cJSON_PrintUnformatted(root);
        if (payload) {
            ESP_LOGD(TAG, "Publishing to %s: %s", state_topic.c_str(), payload);
            mqtt.publish(state_topic, payload, 0, false);
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

  void DaliDeviceController::updateDeviceState(DaliLongAddress_t longAddr, uint8_t level, std::optional<uint8_t> status_byte, std::optional<uint16_t> mireds, std::optional<DaliRGB> rgb) {
        std::lock_guard lock(m_devices_mutex);
        const auto it = m_devices.find(longAddr);
        if (it != m_devices.end()) {
            bool state_changed = false;

            if (level > 0) {
                it->second.last_level = level;
            }

            if (it->second.current_level != level) {
                it->second.current_level = level;
                state_changed = true;
            }

            uint8_t actual_status_byte = status_byte.has_value() ? *status_byte : it->second.status_byte;

            if (it->second.status_byte != actual_status_byte) {
                ESP_LOGD(TAG, "Status byte changed for %s: %d -> %d", utils::longAddressToString(longAddr).data(), it->second.status_byte, actual_status_byte);
                it->second.status_byte = actual_status_byte;
                state_changed = true;
            }

            if (mireds.has_value() && it->second.color_temp != mireds) {
                it->second.color_temp = mireds;
                state_changed = true;
            }

            if (rgb.has_value() && it->second.rgb != rgb) {
                it->second.rgb = rgb;
                state_changed = true;
            }

            if (state_changed || it->second.initial_sync_needed) {
                ESP_LOGD(TAG, "State update for %s: Level=%d, Status=%d (InitSync: %d)",
                         utils::longAddressToString(longAddr).data(), level, actual_status_byte, it->second.initial_sync_needed);

                publishState(longAddr, it->second);

                it->second.initial_sync_needed = false;
            }
        }
    }
    void DaliDeviceController::publishAttributes(const DaliLongAddress_t long_addr) {
        auto const& mqtt = MQTTClient::getInstance();
        const auto config = ConfigManager::getInstance().getConfig();
        const auto addr_str = utils::longAddressToString(long_addr);

        DaliDevice dev_copy;
        {
            std::lock_guard lock(m_devices_mutex);
            if (!getInstance().m_devices.contains(long_addr)) return;
            dev_copy = getInstance().m_devices.at(long_addr);
        }
        const std::string attr_topic = utils::stringFormat("%s/light/%s/attributes", config.mqtt_base_topic.c_str(), addr_str.data());

        cJSON* root = cJSON_CreateObject();

        if (dev_copy.device_type.has_value()) {
            uint8_t dt = dev_copy.device_type.value();
            cJSON_AddNumberToObject(root, "device_type", dt);
        }

        if (!dev_copy.gtin.empty()) {
            cJSON_AddStringToObject(root, "gtin", dev_copy.gtin.c_str());
        }

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
        std::lock_guard lock(m_devices_mutex);
        if (const auto it = m_devices.find(longAddress); it != m_devices.end()) {
            return it->second.last_level;
        }
        return std::nullopt;
    }

    bool DaliDeviceController::validateAddressMap() {
        ESP_LOGI(TAG, "Validating cached DALI address map...");
        auto& dali = DaliAPI::getInstance();

        std::vector<AddressMapping> devices_to_validate;
        {
            std::lock_guard lock(m_devices_mutex);
            if (m_devices.empty()) {
                ESP_LOGI(TAG, "Map is empty, validation skipped.");
                return false;
            }
            devices_to_validate.reserve(m_devices.size());
            for (const auto& [long_addr, device] : m_devices) {
                devices_to_validate.push_back({.long_address = long_addr, .short_address = device.short_address});
            }
        }

        for (const auto& mapping : devices_to_validate) {
            const auto expected_long_addr = mapping.long_address;
            const auto short_address = mapping.short_address;

            if (auto status_opt = dali.sendQuery(DALI_ADDRESS_TYPE_SHORT, short_address, DALI_COMMAND_QUERY_STATUS); status_opt.has_value()) {

                if (auto long_addr_from_bus_opt = dali.getLongAddress(short_address)) {
                    if (*long_addr_from_bus_opt != expected_long_addr) {
                        ESP_LOGW(TAG, "Validation CONFLICT: Short Addr %d has Long Addr %lX, expected %lX. Full scan required.",
                            short_address, *long_addr_from_bus_opt, expected_long_addr);
                        return false;
                    }
                    {
                        std::lock_guard lock(m_devices_mutex);
                        if (m_devices.contains(expected_long_addr)) {
                            auto& dev = m_devices[expected_long_addr];
                            if (!dev.available) {
                                dev.available = true;
                                publishAvailability(expected_long_addr, true);
                            }
                        }
                    }
                } else {
                    ESP_LOGW(TAG, "Device at SA %d responded to Status but failed Long Addr query. Skipping check.", short_address);
                }
            } else {
                ESP_LOGD(TAG, "Device at SA %d did not respond (Offline).", short_address);
            }
            vTaskDelay(pdMS_TO_TICKS(CONFIG_DALI2MQTT_DALI_POLL_DELAY_MS));
        }
        return true;
    }

    [[noreturn]] void DaliDeviceController::daliEventHandlerTask(void* pvParameters) {
        auto* self = static_cast<DaliDeviceController*>(pvParameters);
        const auto& dali_api = DaliAPI::getInstance();
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
            auto long_addr_opt = getLongAddress(address);
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

    void DaliDeviceController::requestDeviceSync(uint8_t shortAddress, uint32_t delay_ms) {
        if (shortAddress >= 64) return;

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
    void DaliDeviceController::requestBroadcastSync(const uint32_t base_delay_ms, const uint32_t stagger_ms) {
        std::lock_guard lock(m_devices_mutex);
        ESP_LOGI(TAG, "Scheduling broadcast sync for %zu devices (Base: %ums, Stagger: %ums)",
                         m_devices.size(), base_delay_ms, stagger_ms);
        uint32_t current_delay = base_delay_ms;
        for (const auto &dev: m_devices | std::views::values) {
            requestDeviceSync(dev.short_address, current_delay);
            current_delay += stagger_ms;
        }
    }
    void DaliDeviceController::pollSingleDevice(const uint8_t shortAddr) {
        DaliLongAddress_t long_addr = 0;
        bool known_device = false;

        {
            std::lock_guard lock(m_devices_mutex);
            if (const auto it = m_short_to_long_map.find(shortAddr); it != m_short_to_long_map.end()) {
                long_addr = it->second;
                known_device = true;
            }
        }
        if (!known_device) {
            return;
        }

        auto& dali = DaliAPI::getInstance();
        const auto level_opt = dali.sendQuery(DALI_ADDRESS_TYPE_SHORT, shortAddr, DALI_COMMAND_QUERY_ACTUAL_LEVEL);
        const bool is_device_responding = level_opt.has_value();
        {
            std::lock_guard lock(m_devices_mutex);
            if (m_devices.contains(long_addr)) {
                if (m_devices[long_addr].available != is_device_responding) {
                    m_devices[long_addr].available = is_device_responding;
                    publishAvailability(long_addr, is_device_responding);
                }
            } else {
                return;
            }
        }

        if (!is_device_responding) return;

        const auto status_opt = dali.getDeviceStatus(shortAddr);
        uint8_t current_cached_level = 0;

        bool is_dt8 = false;
        bool features_known = false;
        bool supports_tc = false;
        bool supports_rgb = false;

        {
             std::lock_guard lock(m_devices_mutex);
             if(m_devices.contains(long_addr)) {
                 const auto& dev = m_devices[long_addr];

                 current_cached_level = dev.current_level;
                 if (dev.device_type.has_value() && dev.device_type.value() == 8) {
                     is_dt8 = true;
                     features_known = dev.static_data_loaded;
                     supports_tc = dev.supports_tc;
                     supports_rgb = dev.supports_rgb;
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
                    std::lock_guard lock(m_devices_mutex);
                    if (m_devices.contains(long_addr)) {
                        m_devices[long_addr].supports_tc = supports_tc;
                        m_devices[long_addr].supports_rgb = supports_rgb;
                        m_nvs_dirty = true;
                        m_last_nvs_change_ts = esp_timer_get_time() / 1000;
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
        int64_t last_poll_ts = 0;

        {
            std::lock_guard lock(m_devices_mutex);
            if(m_devices.contains(long_addr)) {
                const auto& dev = m_devices[long_addr];
                last_poll_ts = dev.last_color_poll_ts;
                if (actual_level > 0 && (dev.supports_tc || dev.supports_rgb)) {
                    if ((now_sec - last_poll_ts) > COLOR_POLL_INTERVAL_SEC) {
                        should_poll_color = true;
                    }
                }
            }
        }

        if (should_poll_color) {
            if (supports_tc) {
                polled_tc = dali.getDT8ColorTemp(shortAddr);
                if (polled_tc) ESP_LOGD(TAG, "Polled Tc for SA %d: %d mireds", shortAddr, *polled_tc);
            }

            if (supports_rgb) {
                polled_rgb = dali.getDT8RGB(shortAddr);
                if (polled_rgb) ESP_LOGD(TAG, "Polled RGB for SA %d: %d,%d,%d", shortAddr, polled_rgb->r, polled_rgb->g, polled_rgb->b);
            }

            {
                std::lock_guard lock(m_devices_mutex);
                if(m_devices.contains(long_addr)) {
                    m_devices[long_addr].last_color_poll_ts = now_sec;
                }
            }
        }

        updateDeviceState(long_addr, actual_level, status_opt, polled_tc, polled_rgb);

        bool needs_static_data = false;
        {
            std::lock_guard lock(m_devices_mutex);
            if (m_devices.contains(long_addr) && !m_devices[long_addr].static_data_loaded) {
                needs_static_data = true;
            }
        }

        if (needs_static_data) {
                const auto gtin_opt = dali.getGTIN(shortAddr);
                const auto dt_opt = dali.getDeviceType(shortAddr);
                {
                    std::lock_guard lock(m_devices_mutex);
                    if(m_devices.contains(long_addr)) {
                        bool changed = false;
                        if (gtin_opt.has_value()) {
                            m_devices[long_addr].gtin = gtin_opt.value();
                            changed = true;
                        }
                        if (dt_opt.has_value()) {
                            m_devices[long_addr].device_type = dt_opt;
                            changed = true;
                        }
                        m_devices[long_addr].static_data_loaded = true;

                        if (changed) {
                            m_nvs_dirty = true;
                            m_last_nvs_change_ts = esp_timer_get_time() / 1000;
                        }
                    }
                }
                publishAttributes(long_addr);
        }
    }


    [[noreturn]] void DaliDeviceController::daliSyncTask(void* pvParameters) {
        auto* self = static_cast<DaliDeviceController*>(pvParameters);
        const int64_t NVS_SAVE_DEBOUNCE_MS = 20000;
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
                        std::lock_guard lock(self->m_devices_mutex);
                        copy_devs = self->m_devices;
                        self->m_nvs_dirty = false;
                    }
                    DaliAddressMap::save(copy_devs);
                }
            }

            {
                std::lock_guard lock(self->m_queue_mutex);

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
                self->pollSingleDevice(self->m_round_robin_index);

                self->m_round_robin_index++;
                if (self->m_round_robin_index >= 64)
                {
                    self->m_round_robin_index = 0;
                    auto all_assignments = DaliGroupManagement::getInstance().getAllAssignments();
                    std::map<DaliLongAddress_t, uint8_t> current_levels;
                    {
                        std::lock_guard lock(self->m_devices_mutex);
                        for(const auto& [addr, dev] : self->m_devices) {
                            if(dev.available) current_levels[addr] = dev.current_level;
                        }
                    }
                    std::map<uint8_t, uint8_t> group_sync_levels;
                    for (const auto& [long_addr, groups] : all_assignments) {
                        if (!current_levels.contains(long_addr)) continue;
                        uint8_t level = current_levels.at(long_addr);
                        for (uint8_t group = 0; group < 16; ++group) {
                            if (groups.test(group)) {
                                if (!group_sync_levels.contains(group)) group_sync_levels[group] = level;
                                else if (level > group_sync_levels[group]) group_sync_levels[group] = level;
                            }
                        }
                    }

                    for(const auto& [group_id, level] : group_sync_levels) {
                        DaliGroupManagement::getInstance().updateGroupState(group_id, level);
                    }
                }
                vTaskDelay(rr_delay_ticks);
            }
        }
    }

    std::bitset<64> DaliDeviceController::performFullInitialization() {
        if (!DaliAPI::getInstance().isInitialized()) {
            ESP_LOGE(TAG, "Cannot initialize DALI bus: DALI driver is not initialized (device might be in provisioning mode).");
            return {};
        }
        DaliAPI::getInstance().initializeBus();
        const std::bitset<64> devices = discoverAndMapDevices();
        return devices;
    }

    std::bitset<64> DaliDeviceController::performScan() {
        if (!DaliAPI::getInstance().isInitialized()) {
            ESP_LOGE(TAG, "Cannot scan DALI bus: DALI driver is not initialized (device might be in provisioning mode).");
            return {};
        }
        const std::bitset<64> found_devices = discoverAndMapDevices();
        
        return found_devices;
    }
    
    std::bitset<64> DaliDeviceController::discoverAndMapDevices() {
        ESP_LOGI(TAG, "Starting DALI device discovery and mapping...");
        auto& dali = DaliAPI::getInstance();
        std::map<DaliLongAddress_t, DaliDevice> new_devices;
        std::map<uint8_t, DaliLongAddress_t> new_short_to_long_map;
        std::bitset<64> found_devices;

        for (uint8_t sa = 0; sa < 64; ++sa) {
            if (auto status_opt = dali.sendQuery(DALI_ADDRESS_TYPE_SHORT, sa, DALI_COMMAND_QUERY_STATUS); status_opt.has_value()) {
                found_devices.set(sa);
                ESP_LOGI(TAG, "Device found at short address %d. Querying long address...", sa);
                if (auto long_addr_opt = dali.getLongAddress(sa)) {
                    DaliLongAddress_t long_addr = *long_addr_opt;
                    const auto addr_str = utils::longAddressToString(long_addr);
                    ESP_LOGI(TAG, "  -> Long address: %s (0x%lX)", addr_str.data(), long_addr);

                    new_devices[long_addr] = DaliDevice{
                        .long_address = long_addr,
                        .short_address = sa,
                        .available = true
                    };
                    new_short_to_long_map[sa] = long_addr;
                } else {
                    ESP_LOGW(TAG, "  -> Failed to query long address for short address %d.", sa);
                }
            }
            vTaskDelay(pdMS_TO_TICKS(CONFIG_DALI2MQTT_DALI_POLL_DELAY_MS));
        }

        {
            std::lock_guard lock(m_devices_mutex);
            m_devices = std::move(new_devices);
            m_short_to_long_map = std::move(new_short_to_long_map);
            ESP_LOGI(TAG, "Discovery finished. Mapped %zu DALI devices.", m_devices.size());

            DaliAddressMap::save(m_devices);
            m_nvs_dirty = false;

        }
        return found_devices;
    }


    std::map<DaliLongAddress_t, DaliDevice> DaliDeviceController::getDevices() const {
        std::lock_guard lock(m_devices_mutex);
        return m_devices;
    }

    std::optional<uint8_t> DaliDeviceController::getShortAddress(const DaliLongAddress_t longAddress) const {
        std::lock_guard lock(m_devices_mutex);
        if (const auto it = m_devices.find(longAddress); it != m_devices.end()) {
            return it->second.short_address;
        }
        return std::nullopt;
    }

    std::optional<DaliLongAddress_t> DaliDeviceController::getLongAddress(const uint8_t shortAddress) const {
        {
            std::lock_guard lock(m_devices_mutex);
            if (const auto it = m_short_to_long_map.find(shortAddress); it != m_short_to_long_map.end()) {
                return it->second;
            }
        }
        return std::nullopt;
    }
}// daliMQTT