#include "DaliDeviceController.hxx"
#include "ConfigManager.hxx"
#include "DaliAPI.hxx"
#include "MQTTClient.hxx"
#include <esp_log.h>
#include <format>
#include <ranges>

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

    void DaliDeviceController::startPolling() {
        if (!m_poll_task_handle) {
            xTaskCreate(daliPollTask, "dali_poll", CONFIG_DALI2MQTT_DALI_POLL_TASK_STACK_SIZE, this, CONFIG_DALI2MQTT_DALI_POLL_TASK_PRIORITY, &m_poll_task_handle);
        }
    }

    std::bitset<64> DaliDeviceController::performFullInitialization() {
        std::lock_guard lock(m_devices_mutex);
        m_discovered_devices = DaliAPI::getInstance().initializeBus();
        saveDeviceMask();
        return m_discovered_devices;
    }

    std::bitset<64> DaliDeviceController::performScan() {
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

    void DaliDeviceController::saveDeviceMask() {
        auto& config_manager = ConfigManager::getInstance();
        auto config = config_manager.getConfig();
        config.dali_devices_mask = m_discovered_devices.to_ullong();
        config_manager.setConfig(config);
        config_manager.save();
        ESP_LOGI(TAG, "Saved DALI device mask to NVS.");
    }

    [[noreturn]] void DaliDeviceController::daliPollTask(void* pvParameters) {
        auto* self = static_cast<DaliDeviceController*>(pvParameters);
        if (!self) { vTaskDelete(nullptr); }

        auto config = ConfigManager::getInstance().getConfig();
        auto& dali = DaliAPI::getInstance();
        auto const& mqtt = MQTTClient::getInstance();

        ESP_LOGI(TAG, "DALI polling task started.");

        while (true) {
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
            vTaskDelay(pdMS_TO_TICKS(config.dali_poll_interval_ms));
        }
    }
}// daliMQTT