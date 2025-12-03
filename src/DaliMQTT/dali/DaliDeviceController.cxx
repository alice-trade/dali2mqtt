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
    void DaliDeviceController::publishState(const DaliLongAddress_t long_addr, const uint8_t level, const bool lamp_failure) {
        auto const& mqtt = MQTTClient::getInstance();
        const auto config = ConfigManager::getInstance().getConfig();

        const auto addr_str = utils::longAddressToString(long_addr);
        const std::string state_topic = utils::stringFormat("%s/light/%s/state", config.mqtt_base_topic.c_str(), addr_str.data());
        if (level == 255) return;

        const std::string payload = utils::stringFormat(
                R"({"state":"%s","brightness":%d,"lamp_failure":%s})",
                (level > 0 ? "ON" : "OFF"),
                level,
                (lamp_failure ? "true" : "false")
            );
        ESP_LOGD(TAG, "Publishing to %s: %s", state_topic.c_str(), payload.c_str());
        mqtt.publish(state_topic, payload, 0, false);
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

    void DaliDeviceController::updateDeviceState(DaliLongAddress_t longAddr, uint8_t level, std::optional<bool> lamp_failure) {
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
            bool actual_failure_status = lamp_failure.has_value() ? *lamp_failure : it->second.lamp_failure;

            if (it->second.lamp_failure != actual_failure_status) {
                ESP_LOGW(TAG, "Lamp failure status changed for %s: %d", utils::longAddressToString(longAddr).data(), actual_failure_status);
                it->second.lamp_failure = actual_failure_status;
                state_changed = true;
            }

            if (state_changed || it->second.initial_sync_needed) {
                ESP_LOGI(TAG, "State update for %s: Level=%d, Fail=%d (InitSync: %d)",
                         utils::longAddressToString(longAddr).data(), level, actual_failure_status, it->second.initial_sync_needed);

                publishState(longAddr, level, actual_failure_status);

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

            auto type_str = "Unknown";
            if (dt == 6) type_str = "LED Module (DT6)";
            else if (dt == 8) type_str = "Colour Control (DT8)";
            cJSON_AddStringToObject(root, "device_type_str", type_str);
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

        std::vector<DaliLongAddress_t> validated_devices;
        for (const auto&[long_address, short_address] : devices_to_validate) {
            if (auto status_opt = dali.sendQuery(DALI_ADDRESS_TYPE_SHORT, short_address, DALI_COMMAND_QUERY_STATUS); status_opt.has_value()) {
                if (auto long_addr_from_bus_opt = dali.getLongAddress(short_address); long_addr_from_bus_opt && *long_addr_from_bus_opt == long_address) {
                    validated_devices.push_back(long_address);
                } else {
                    ESP_LOGW(TAG, "Validation failed: Device at short address %d has a different long address now.", short_address);
                    return false;
                }
            } else {
                ESP_LOGW(TAG, "Validation failed: Device at short address %d did not respond.", short_address);
            }
            vTaskDelay(pdMS_TO_TICKS(CONFIG_DALI2MQTT_DALI_POLL_DELAY_MS));
        }

        {
            std::lock_guard lock(m_devices_mutex);
            for (const auto& long_addr : validated_devices) {
                if (m_devices.contains(long_addr)) {
                    m_devices.at(long_addr).is_present = true;
                }
            }
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
                self->processDaliFrame(frame);
            }
        }
    }

    void DaliDeviceController::requestDeviceSync(uint8_t shortAddress, uint32_t delay_ms) {
        if (shortAddress >= 64) return;
        std::lock_guard lock(m_queue_mutex);

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

        const bool is_present = level_opt.has_value();
        {
            std::lock_guard lock(m_devices_mutex);
            if (m_devices.contains(long_addr)) {
                if (m_devices[long_addr].available != is_present) {
                    m_devices[long_addr].available = is_present;
                    publishAvailability(long_addr, is_present);
                }
            } else {
                return;
            }
        }

        if (!is_present) return;

        const auto status_opt = dali.getDeviceStatus(shortAddr);
        bool lamp_failure = false;
        if (status_opt.has_value()) {
            lamp_failure = (*status_opt >> 1) & 0x01;
        }

        uint8_t current_cached_level = 0;
        {
             std::lock_guard lock(m_devices_mutex);
             if(m_devices.contains(long_addr)) current_cached_level = m_devices[long_addr].current_level;
        }

        const uint8_t actual_level = (level_opt.value() == 255) ? current_cached_level : level_opt.value();

        updateDeviceState(long_addr, actual_level, lamp_failure);

        bool needs_static_data = false;
        {
            std::lock_guard lock(m_devices_mutex);
            if (m_devices.contains(long_addr) && !m_devices[long_addr].static_data_loaded) {
                needs_static_data = true;
            }
        }

        if (needs_static_data) {
                const auto dt_opt = dali.getDeviceType(shortAddr);
                {
                    std::lock_guard lock(m_devices_mutex);
                    if(m_devices.contains(long_addr)) {
                        m_devices[long_addr].device_type = dt_opt;
                        m_devices[long_addr].static_data_loaded = true;
                    }
                }
                publishAttributes(long_addr);
        }
    }


    [[noreturn]] void DaliDeviceController::daliSyncTask(void* pvParameters) {
        auto* self = static_cast<DaliDeviceController*>(pvParameters);

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
                            if(dev.is_present && dev.available) current_levels[addr] = dev.current_level;
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
                        .current_level = 0,
                        .last_level = 254,
                        .is_present = true,
                        .available = false,
                        .initial_sync_needed = true
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