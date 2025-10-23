#include "DaliDeviceController.hxx"
#include <DaliGroupManagement.hxx>
#include "ConfigManager.hxx"
#include "DaliAPI.hxx"
#include "MQTTClient.hxx"

namespace daliMQTT
{
    static constexpr char TAG[] = "DaliDeviceController";

    void DaliDeviceController::init() {
        ESP_LOGI(TAG, "Initializing DALI Device Controller...");
        loadDeviceMask();

        if (m_discovered_devices.none()) {
            ESP_LOGI(TAG, "No DALI devices configured in NVS. Performing initial scan.");
            performScan();
        } else {
            ESP_LOGI(TAG, "Loaded %zu DALI devices from NVS.", m_discovered_devices.count());
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
            ESP_LOGI(TAG, "DALI monitoring and sync tasks started.");
        } else {
            ESP_LOGE(TAG, "Cannot start DALI tasks: DaliAPI not initialized.");
        }
    }
    static void publishState(uint8_t short_addr, uint8_t level) {
        auto const& mqtt = MQTTClient::getInstance();
        auto config = ConfigManager::getInstance().getConfig();

        std::string state_topic = std::format("{}/light/short/{}/state", config.mqtt_base_topic, short_addr);
        std::string payload = std::format(R"({{"state":"{}","brightness":{}}})", (level > 0 ? "ON" : "OFF"), level);

        ESP_LOGD(TAG, "Publishing to %s: %s", state_topic.c_str(), payload.c_str());
        mqtt.publish(state_topic, payload);
    }


    static void processGroupCommand(uint8_t group_addr, std::optional<uint8_t> known_level) {
        auto& dali = DaliAPI::getInstance();
        auto const& group_manager = DaliGroupManagement::getInstance();
        auto all_assignments = group_manager.getAllAssignments();

        for (const auto& [device_addr, groups] : all_assignments) {
            if (groups.test(group_addr)) {
                if (known_level.has_value()) {
                    publishState(device_addr, *known_level);
                } else if (auto level_opt = dali.sendQuery(DALI_ADDRESS_TYPE_SHORT, device_addr, DALI_COMMAND_QUERY_ACTUAL_LEVEL); level_opt) {
                     publishState(device_addr, level_opt.value());
                }
            }
        }
    }

    static void processBroadcastCommand(std::optional<uint8_t> known_level) {
        auto& dali = DaliAPI::getInstance();
        auto const& controller = DaliDeviceController::getInstance();
        const auto discovered_devices = controller.getDiscoveredDevices();

        for (uint8_t i = 0; i < 64; ++i) {
            if (discovered_devices.test(i)) {
                if (known_level.has_value()) {
                    publishState(i, *known_level);
                } else if (auto level_opt = dali.sendQuery(DALI_ADDRESS_TYPE_SHORT, i, DALI_COMMAND_QUERY_ACTUAL_LEVEL); level_opt) {
                     publishState(i, level_opt.value());
                }
            }
        }
    }
     void DaliDeviceController::processDaliFrame(const dali_frame_t& frame) {
        if (frame.is_backward_frame) {
            ESP_LOGD(TAG, "Sniffed Backward Frame: 0x%02X", frame.data & 0xFF);
            return;
        }

        ESP_LOGD(TAG, "Sniffed Forward Frame: 0x%04X", frame.data);

        uint8_t addr_byte = (frame.data >> 8) & 0xFF;
        uint8_t cmd_byte = frame.data & 0xFF;

        if ((addr_byte & 0x01) == 0) {
            uint8_t level = cmd_byte;
            if ((addr_byte & 0x80) == 0) {
                uint8_t short_addr = (addr_byte >> 1) & 0x3F;
                publishState(short_addr, level);
            }
            // Групповой адрес
            else if ((addr_byte & 0xE0) == 0x80) {
                uint8_t group_addr = (addr_byte >> 1) & 0x0F;
                processGroupCommand(group_addr, level);
            }
            // Broadcast
            else if (addr_byte == 0xFE) {
                processBroadcastCommand(level);
            }
            return;
        }

        switch (cmd_byte) {
            case DALI_COMMAND_OFF:
            case DALI_COMMAND_RECALL_MIN_LEVEL:
            case DALI_COMMAND_STEP_DOWN_AND_OFF: {
                std::optional<uint8_t> known_level = (cmd_byte == DALI_COMMAND_OFF) ? std::optional(0) : std::nullopt;
                if ((addr_byte & 0x80) == 0) {
                    publishState((addr_byte >> 1) & 0x3F, 0);
                } else if ((addr_byte & 0xE0) == 0x80) {
                    processGroupCommand((addr_byte >> 1) & 0x0F, known_level);
                } else if ((addr_byte & 0xFE) == 0xFE) {
                     processBroadcastCommand(known_level);
                }
                break;
            }
            case DALI_COMMAND_ON_AND_STEP_UP:
            case DALI_COMMAND_RECALL_MAX_LEVEL: {
                if ((addr_byte & 0x80) == 0) {
                    uint8_t short_addr = (addr_byte >> 1) & 0x3F;
                    if (auto level_opt = DaliAPI::getInstance().sendQuery(DALI_ADDRESS_TYPE_SHORT, short_addr, DALI_COMMAND_QUERY_ACTUAL_LEVEL); level_opt) {
                        publishState(short_addr, level_opt.value());
                    }
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
        auto& dali_api = DaliAPI::getInstance();
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

            std::vector<uint8_t> active_devices;
            {
                std::lock_guard lock(self->m_devices_mutex);
                for (uint8_t i = 0; i < 64; ++i) {
                    if (self->m_discovered_devices.test(i)) {
                        active_devices.push_back(i);
                    }
                }
            }

            if (active_devices.empty()) {
                ESP_LOGD(TAG, "No active devices to poll.");
            } else {
                ESP_LOGD(TAG, "Polling %zu active DALI devices...", active_devices.size());
                for (const auto i : active_devices) {
                    if (auto level_opt = dali.sendQuery(DALI_ADDRESS_TYPE_SHORT, i, DALI_COMMAND_QUERY_ACTUAL_LEVEL); level_opt && level_opt.value() != 255) {
                        uint8_t level = level_opt.value();
                        std::string state_topic = std::format("{}/light/short/{}/state", config.mqtt_base_topic, i);
                        std::string payload = std::format(R"({{"state":"{}","brightness":{}}})", (level > 0 ? "ON" : "OFF"), level);
                        ESP_LOGD(TAG, "Reply from DALI %d: %s", i, payload.c_str());
                        mqtt.publish(state_topic, payload);
                    } else if (!level_opt.has_value()) {
                        ESP_LOGD(TAG, "No reply from DALI device %d", i);
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
        std::lock_guard lock(m_devices_mutex);
        m_discovered_devices = DaliAPI::getInstance().initializeBus();
        saveDeviceMask();
        return m_discovered_devices;
    }

    std::bitset<64> DaliDeviceController::performScan() {
        if (!DaliAPI::getInstance().isInitialized()) {
            ESP_LOGE(TAG, "Cannot scan DALI bus: DALI driver is not initialized (device might be in provisioning mode).");
            return {};
        }
        std::lock_guard lock(m_devices_mutex);
        m_discovered_devices = DaliAPI::getInstance().scanBus();
        saveDeviceMask();
        return m_discovered_devices;
    }

    std::bitset<64> DaliDeviceController::getDiscoveredDevices() const {
        std::lock_guard lock(m_devices_mutex);
        return m_discovered_devices;
    }

    void DaliDeviceController::loadDeviceMask() {
        std::lock_guard lock(m_devices_mutex);
        const auto config = ConfigManager::getInstance().getConfig();
        m_discovered_devices = std::bitset<64>(config.dali_devices_mask);
    }

    void DaliDeviceController::saveDeviceMask() const
    {
        auto& config_manager = ConfigManager::getInstance();
        config_manager.saveDaliDeviceMask(m_discovered_devices.to_ullong());
        ESP_LOGI(TAG, "Saved DALI device mask to NVS.");
    }
}// daliMQTT