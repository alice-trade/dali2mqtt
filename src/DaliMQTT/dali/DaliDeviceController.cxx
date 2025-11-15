#include "DaliDeviceController.hxx"
#include <DaliGroupManagement.hxx>
#include "ConfigManager.hxx"
#include "DaliAddressMap.hxx"
#include "DaliAPI.hxx"
#include "MQTTClient.hxx"

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
    static void publishState(DaliLongAddress_t long_addr, uint8_t level) {
        auto const& mqtt = MQTTClient::getInstance();
        auto config = ConfigManager::getInstance().getConfig();

        const auto addr_str = longAddressToString(long_addr);
        std::string state_topic = std::format("{}/light/{}/state", config.mqtt_base_topic, addr_str.data());
        std::string payload = std::format(R"({{"state":"{}","brightness":{}}})", (level > 0 ? "ON" : "OFF"), level);

        ESP_LOGD(TAG, "Publishing to %s: %s", state_topic.c_str(), payload.c_str());
        mqtt.publish(state_topic, payload);
    }


    void DaliDeviceController::updateDeviceLevel(DaliLongAddress_t longAddr, uint8_t level) {
        std::lock_guard lock(m_devices_mutex);
        if (m_devices.contains(longAddr)) {
            m_devices.at(longAddr).current_level = level;
        }
    }

    static void processGroupCommand(uint8_t group_addr, std::optional<uint8_t> known_level, DaliDeviceController* self) {
        auto& dali = DaliAPI::getInstance();
        auto const& group_manager = DaliGroupManagement::getInstance();
        auto all_assignments = group_manager.getAllAssignments();

        for (const auto& [long_addr, groups] : all_assignments) {
            if (groups.test(group_addr)) {
                if (auto short_addr_opt = self->getShortAddress(long_addr)) {
                     if (known_level.has_value()) {
                        publishState(long_addr, *known_level);
                     } else if (auto level_opt = dali.sendQuery(DALI_ADDRESS_TYPE_SHORT, *short_addr_opt, DALI_COMMAND_QUERY_ACTUAL_LEVEL); level_opt) {
                         publishState(long_addr, level_opt.value());
                     }
                }
            }
        }
    }

    static void processBroadcastCommand(const std::optional<uint8_t> known_level, const DaliDeviceController* self) {
        auto& dali = DaliAPI::getInstance();
        auto devices = self->getDevices();
        
        for(const auto& [long_addr, device] : devices) {
                if (known_level.has_value()) {
                    publishState(long_addr, *known_level);
                } else if (auto level_opt = dali.sendQuery(DALI_ADDRESS_TYPE_SHORT, device.short_address, DALI_COMMAND_QUERY_ACTUAL_LEVEL); level_opt) {
                     publishState(long_addr, level_opt.value());
                }
            }
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
                return false;
            }
            vTaskDelay(pdMS_TO_TICKS(CONFIG_DALI2MQTT_DALI_POLL_DELAY_MS));
        }

        {
            std::lock_guard lock(m_devices_mutex);
            for (auto &val: m_devices | std::views::values) {
                val.is_present = false;
            }
            for (const auto& long_addr : validated_devices) {
                if (m_devices.contains(long_addr)) {
                    m_devices.at(long_addr).is_present = true;
                }
            }
        }
        return true;
    }

     void DaliDeviceController::processDaliFrame(const dali_frame_t& frame) {
        if (frame.is_backward_frame) {
            ESP_LOGD(TAG, "Process sniffed backward frame 0x%02X", frame.data & 0xFF);
            return;
        }

        ESP_LOGD(TAG, "Process sniffed forward frame 0x%04X", frame.data);

        uint8_t addr_byte = (frame.data >> 8) & 0xFF;
        uint8_t cmd_byte = frame.data & 0xFF;

        if ((addr_byte & 0x01) == 0) {
            uint8_t level = cmd_byte;
            if ((addr_byte & 0x80) == 0) {
                uint8_t short_addr = (addr_byte >> 1) & 0x3F;
                if (auto long_addr_opt = getLongAddress(short_addr)) {
                    ESP_LOGD(TAG,"Found direct change of short address %u (long: %lX) state to %u", short_addr, *long_addr_opt, level);
                    publishState(*long_addr_opt, level);
                }
            }
            // Групповой адрес
            else if ((addr_byte & 0xE0) == 0x80) {
                uint8_t group_addr = (addr_byte >> 1) & 0x0F;
                ESP_LOGD(TAG,"Found direct change of group address state: %u, %u", group_addr, level);
                processGroupCommand(group_addr, level, this);
            }
            // Broadcast
            else if (addr_byte == 0xFE) {
                ESP_LOGD(TAG,"Found direct change of broadcast address state: %u", level);
                processBroadcastCommand(level, this);
            }
            return;
        }

        switch (cmd_byte) {
            case DALI_COMMAND_OFF:
            case DALI_COMMAND_RECALL_MIN_LEVEL:
            case DALI_COMMAND_STEP_DOWN_AND_OFF: {
                std::optional<uint8_t> known_level = (cmd_byte == DALI_COMMAND_OFF) ? std::optional(0) : std::nullopt;
                if ((addr_byte & 0x80) == 0) {
                    uint8_t short_addr = (addr_byte >> 1) & 0x3F;
                    if(auto long_addr_opt = getLongAddress(short_addr)) {
                        ESP_LOGD(TAG,"Found OFF command for short address %u (long: %lX)", short_addr, *long_addr_opt);
                        publishState(*long_addr_opt, 0);
                    }
                } else if ((addr_byte & 0xE0) == 0x80) {
                    ESP_LOGD(TAG,"Found OFF command of group address state: %u", (addr_byte >> 1) & 0x0F);
                    processGroupCommand((addr_byte >> 1) & 0x0F, known_level, this);
                } else if ((addr_byte & 0xFE) == 0xFE) {
                     processBroadcastCommand(known_level, this);
                }
                break;
            }
            case DALI_COMMAND_ON_AND_STEP_UP:
            case DALI_COMMAND_RECALL_MAX_LEVEL: {
                if ((addr_byte & 0x80) == 0) {
                    uint8_t short_addr = (addr_byte >> 1) & 0x3F;
                     if (auto long_addr_opt = getLongAddress(short_addr)) {
                        ESP_LOGD(TAG,"Found ON command for short address %u (long: %lX)", short_addr, *long_addr_opt);
                        if (auto level_opt = DaliAPI::getInstance().sendQuery(DALI_ADDRESS_TYPE_SHORT, short_addr, DALI_COMMAND_QUERY_ACTUAL_LEVEL); level_opt) {
                           publishState(*long_addr_opt, level_opt.value());
                        }
                    }
                }
                else if ((addr_byte & 0xE0) == 0x80) {
                    uint8_t group_addr = (addr_byte >> 1) & 0x0F;
                    ESP_LOGD(TAG,"Found ON command of group address: %u", group_addr);
                    processGroupCommand(group_addr, std::nullopt, this);
                }
                else if ((addr_byte & 0xFE) == 0xFE) {
                    ESP_LOGD(TAG,"Found ON command of broadcast: %u");
                    processBroadcastCommand(std::nullopt, this);
                }
                break;
            }

            case DALI_COMMAND_GO_TO_SCENE_0:
            case DALI_COMMAND_GO_TO_SCENE_1:
            case DALI_COMMAND_GO_TO_SCENE_2:
            case DALI_COMMAND_GO_TO_SCENE_3:
            case DALI_COMMAND_GO_TO_SCENE_4:
            case DALI_COMMAND_GO_TO_SCENE_5:
            case DALI_COMMAND_GO_TO_SCENE_6:
            case DALI_COMMAND_GO_TO_SCENE_7:
            case DALI_COMMAND_GO_TO_SCENE_8:
            case DALI_COMMAND_GO_TO_SCENE_9:
            case DALI_COMMAND_GO_TO_SCENE_10:
            case DALI_COMMAND_GO_TO_SCENE_11:
            case DALI_COMMAND_GO_TO_SCENE_12:
            case DALI_COMMAND_GO_TO_SCENE_13:
            case DALI_COMMAND_GO_TO_SCENE_14:
            case DALI_COMMAND_GO_TO_SCENE_15: {
                uint8_t scene_id = cmd_byte - DALI_COMMAND_GO_TO_SCENE_0;
                ESP_LOGI(TAG, "Sniffed GO TO SCENE %d", scene_id);

                // TODO: fix scene change levels on devices
                auto const& mqtt = MQTTClient::getInstance();
                auto config = ConfigManager::getInstance().getConfig();
                mqtt.publish(std::format("{}/scene/state", config.mqtt_base_topic), std::to_string(scene_id));
                break;
            }

            default:
                ESP_LOGD(TAG, "Sniffed unhandled command 0x%02X for address byte 0x%02X", cmd_byte, addr_byte);
                break;
        }
    }

    [[noreturn]] void DaliDeviceController::daliEventHandlerTask(void* pvParameters) {
        auto* self = static_cast<DaliDeviceController*>(pvParameters);
        const auto& dali_api = DaliAPI::getInstance();
        QueueHandle_t queue = dali_api.getEventQueue();
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
        auto const& mqtt = MQTTClient::getInstance();

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

                    if (auto level_opt = dali.sendQuery(DALI_ADDRESS_TYPE_SHORT, device.short_address, DALI_COMMAND_QUERY_ACTUAL_LEVEL); level_opt && level_opt.value() != 255) {
                        uint8_t level = level_opt.value();
                        publishState(long_addr, level);
                    } else if (!level_opt.has_value()) {
                        ESP_LOGD(TAG, "No reply from DALI device with short addr %d", device.short_address);
                    }
                    vTaskDelay(pdMS_TO_TICKS(CONFIG_DALI2MQTT_DALI_POLL_DELAY_MS));
                }
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
                        .is_present = true
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
        if (auto it = m_devices.find(longAddress); it != m_devices.end()) {
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