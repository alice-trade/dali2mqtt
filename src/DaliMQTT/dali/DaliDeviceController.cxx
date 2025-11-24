#include "DaliDeviceController.hxx"
#include <DaliGroupManagement.hxx>
#include "ConfigManager.hxx"
#include "DaliAddressMap.hxx"
#include "DaliAPI.hxx"
#include "MQTTClient.hxx"
#include "utils/DaliLongAddrConversions.hxx"

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
     void publishState(const DaliLongAddress_t long_addr, uint8_t level) {
        auto const& mqtt = MQTTClient::getInstance();
        auto config = ConfigManager::getInstance().getConfig();

        const auto addr_str = longAddressToString(long_addr);
        const std::string state_topic = std::format("{}/light/{}/state", config.mqtt_base_topic, addr_str.data());
        if (level == 255) return;

        const std::string payload = std::format(R"({{"state":"{}","brightness":{}}})", (level > 0 ? "ON" : "OFF"), level);

        ESP_LOGD(TAG, "Publishing to %s: %s", state_topic.c_str(), payload.c_str());
        mqtt.publish(state_topic, payload);
    }

    void DaliDeviceController::publishAvailability(const DaliLongAddress_t long_addr, const bool is_available) {
        auto const& mqtt = MQTTClient::getInstance();
        auto config = ConfigManager::getInstance().getConfig();

        const auto addr_str = longAddressToString(long_addr);
        const std::string status_topic = std::format("{}/light/{}/status", config.mqtt_base_topic, addr_str.data());

        const std::string payload = is_available ? CONFIG_DALI2MQTT_MQTT_PAYLOAD_ONLINE : CONFIG_DALI2MQTT_MQTT_PAYLOAD_OFFLINE;

        ESP_LOGD(TAG, "Publishing AVAILABILITY to %s: %s", status_topic.c_str(), payload.c_str());
        mqtt.publish(status_topic, payload, 1, true);
    }

    void DaliDeviceController::updateDeviceState(DaliLongAddress_t longAddr, uint8_t level) {
        std::lock_guard lock(m_devices_mutex);
        const auto it = m_devices.find(longAddr);
        if (it != m_devices.end()) {
            if (level > 0) {
                it->second.last_level = level;
            }
            if (it->second.current_level != level || it->second.initial_sync_needed) {
                ESP_LOGI(TAG, "State update for %s: %d -> %d (InitSync: %d)",
                         longAddressToString(longAddr).data(), it->second.current_level, level, it->second.initial_sync_needed);
                it->second.current_level = level;
                it->second.initial_sync_needed = false;
                publishState(longAddr, level);
            }
        }
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
            for (auto &val: m_devices | std::views::values) {
                val.is_present = false;
                val.available = false;
                val.initial_sync_needed = true;
            }
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

    [[noreturn]] void DaliDeviceController::daliSyncTask(void* pvParameters) {
        auto* self = static_cast<DaliDeviceController*>(pvParameters);
        if (!self) { vTaskDelete(nullptr); }

        auto config = ConfigManager::getInstance().getConfig();
        auto& dali = DaliAPI::getInstance();

        ESP_LOGI(TAG, "DALI background sync task started.");

        while (true) {
            vTaskDelay(pdMS_TO_TICKS(config.dali_poll_interval_ms));
            
            std::map<DaliLongAddress_t, DaliDevice> devices_to_poll;
            {
                std::lock_guard lock(self->m_devices_mutex);
                devices_to_poll = self->m_devices;
            }

            if (devices_to_poll.empty()) {
                ESP_LOGD(TAG, "No active devices to poll.");
            } else {
                ESP_LOGD(TAG, "Polling %zu active DALI devices...", devices_to_poll.size());
                for (const auto& [long_addr, device] : devices_to_poll) {
                    if (!device.is_present) continue;
                    auto level_opt = dali.sendQuery(DALI_ADDRESS_TYPE_SHORT, device.short_address, DALI_COMMAND_QUERY_ACTUAL_LEVEL);

                    bool new_availability = level_opt.has_value();
                    bool prev_availability = device.available;
                    if (new_availability != prev_availability) {
                        ESP_LOGD(TAG, "Availability change for %s: %s -> %s",
                                longAddressToString(long_addr).data(), prev_availability ? "online" : "offline", new_availability ? "online" : "offline");
                        {
                            std::lock_guard lock(self->m_devices_mutex);
                            if(self->m_devices.contains(long_addr)) {
                                self->m_devices.at(long_addr).available = new_availability;
                            }
                        }
                        publishAvailability(long_addr, new_availability);
                    }

                    if (new_availability && level_opt.value() != 255) {
                        self->updateDeviceState(long_addr, level_opt.value());
                    } else if (!new_availability) {
                        ESP_LOGD(TAG, "No reply from DALI device short addr %d", device.short_address);
                    }

                    vTaskDelay(pdMS_TO_TICKS(CONFIG_DALI2MQTT_DALI_POLL_DELAY_MS));
                }
            }
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
                        if (!group_sync_levels.contains(group)) {
                            group_sync_levels[group] = level;
                        } else if (level > group_sync_levels[group]) {
                            group_sync_levels[group] = level;
                        }
                    }
                }
            }

            for(const auto& [group_id, level] : group_sync_levels) {
                DaliGroupManagement::getInstance().updateGroupState(group_id, level);
            }
        }
    }

    std::bitset<64> DaliDeviceController::performFullInitialization() {
        if (!DaliAPI::getInstance().isInitialized()) {
            ESP_LOGE(TAG, "Cannot initialize DALI bus: DALI driver is not initialized (device might be in provisioning mode).");
            return {};
        }
        const std::bitset<64> found_devices = DaliAPI::getInstance().initializeBus();
        discoverAndMapDevices();

        return found_devices;
    }

    std::bitset<64> DaliDeviceController::performScan() {
        if (!DaliAPI::getInstance().isInitialized()) {
            ESP_LOGE(TAG, "Cannot scan DALI bus: DALI driver is not initialized (device might be in provisioning mode).");
            return {};
        }
        const std::bitset<64> found_devices = DaliAPI::getInstance().scanBus();
        discoverAndMapDevices();
        
        return found_devices;
    }
    
    void DaliDeviceController::discoverAndMapDevices() {
        ESP_LOGI(TAG, "Starting DALI device discovery and mapping...");
        auto& dali = DaliAPI::getInstance();
        std::map<DaliLongAddress_t, DaliDevice> new_devices;
        std::map<uint8_t, DaliLongAddress_t> new_short_to_long_map;

        for (uint8_t sa = 0; sa < 64; ++sa) {
            if (auto status_opt = dali.sendQuery(DALI_ADDRESS_TYPE_SHORT, sa, DALI_COMMAND_QUERY_STATUS); status_opt.has_value()) {
                ESP_LOGI(TAG, "Device found at short address %d. Querying long address...", sa);
                if (auto long_addr_opt = dali.getLongAddress(sa)) {
                    DaliLongAddress_t long_addr = *long_addr_opt;
                    const auto addr_str = longAddressToString(long_addr);
                    ESP_LOGI(TAG, "  -> Long address: %s (0x%lX)", addr_str.data(), long_addr);

                    new_devices[long_addr] = DaliDevice{
                        .long_address = long_addr,
                        .short_address = sa,
                        .current_level = 0,
                        .last_level = 254,
                        .is_present = true,
                        .available = true,
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
    }


    std::map<DaliLongAddress_t, DaliDevice> DaliDeviceController::getDevices() const {
        std::lock_guard lock(m_devices_mutex);
        return m_devices;
    }

    std::optional<uint8_t> DaliDeviceController::getShortAddress(DaliLongAddress_t longAddress) const {
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