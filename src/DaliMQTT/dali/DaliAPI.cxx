#include "DaliAPI.hxx"
#include "sdkconfig.h"

namespace daliMQTT
{
    static constexpr char TAG[] = "DaliAPI";
    constexpr int DALI_EVENT_QUEUE_SIZE = 20;

    esp_err_t DaliAPI::init(gpio_num_t rx_pin, gpio_num_t tx_pin) {
        if (m_initialized) {
            return ESP_OK;
        }
        if (!m_dali_event_queue) {
            m_dali_event_queue = xQueueCreate(DALI_EVENT_QUEUE_SIZE, sizeof(dali_frame_t));
            if (!m_dali_event_queue) {
                ESP_LOGE(TAG, "Failed to create DALI event queue");
                return ESP_ERR_NO_MEM;
            }
        }

        esp_err_t err = dali_init(rx_pin, tx_pin);
        if (err == ESP_OK) {
            m_initialized = true;
        }
        return err;
    }
    esp_err_t DaliAPI::startSniffer() {
        if (!m_initialized || !m_dali_event_queue) {
            return ESP_ERR_INVALID_STATE;
        }
        ESP_LOGI(TAG, "Starting DALI sniffer...");
        return dali_sniffer_start(m_dali_event_queue);
    }

    esp_err_t DaliAPI::stopSniffer() {
        if (!m_initialized) {
            return ESP_OK;
        }
        ESP_LOGI(TAG, "Stopping DALI sniffer...");
        return dali_sniffer_stop();
    }

    QueueHandle_t DaliAPI::getEventQueue() const {
        return m_dali_event_queue;
    }
    esp_err_t DaliAPI::sendCommand(dali_addressType_t addr_type, uint8_t addr, uint8_t command, bool send_twice) {
        esp_err_t err = dali_transaction(addr_type, addr, true, command, send_twice, CONFIG_DALI2MQTT_DALI_TRANSACTION_TIMEOUT_MS, nullptr);
        dali_wait_between_frames();
        return err;
    }

    std::optional<uint8_t> DaliAPI::sendQuery(dali_addressType_t addr_type, uint8_t addr, uint8_t command) {
        int result = DALI_RESULT_NO_REPLY;
        esp_err_t err = dali_transaction(addr_type, addr, true, command, false, CONFIG_DALI2MQTT_DALI_TRANSACTION_TIMEOUT_MS, &result);
        dali_wait_between_frames();

        if (err == ESP_OK && result != DALI_RESULT_NO_REPLY) {
            return static_cast<uint8_t>(result);
        }
        return std::nullopt;
    }

    std::bitset<64> DaliAPI::scanBus() {
        std::bitset<64> found_devices;
        ESP_LOGI(TAG, "Starting DALI bus scan...");
        for (uint8_t i = 0; i < 64; ++i) {
            if (auto response = sendQuery(DALI_ADDRESS_TYPE_SHORT, i, DALI_COMMAND_QUERY_CONTROL_GEAR); response.has_value() && response.value() == 255) {
                ESP_LOGI(TAG, "Device found at short address %d", i);
                found_devices.set(i);
            }
            vTaskDelay(pdMS_TO_TICKS(10));
        }
        ESP_LOGI(TAG, "Scan finished. Found %zu devices.", found_devices.count());
        return found_devices;
    }

    std::bitset<64> DaliAPI::initializeBus() {
        std::lock_guard lock(bus_mutex);
        ESP_LOGI(TAG, "Starting DALI commissioning process...");

        // Step 1: INITIALISE
        sendCommand(DALI_ADDRESS_TYPE_SPECIAL_CMD, DALI_SPECIAL_COMMAND_INITIALISE, 0xFF, true);
        vTaskDelay(pdMS_TO_TICKS(100)); // Wait for devices to react

        // Step 2: RANDOMISE
        sendCommand(DALI_ADDRESS_TYPE_SPECIAL_CMD, DALI_SPECIAL_COMMAND_RANDOMISE, 0, true);
        vTaskDelay(pdMS_TO_TICKS(100));

        uint8_t assigned_addresses = 0;
        uint32_t highest_search_addr = 0xFFFFFF;

        for (uint8_t short_addr = 0; short_addr < 64; ++short_addr) {
            uint32_t search_addr = 0;
            uint32_t current_search_space = highest_search_addr;

            // Step 3: Binary search for the lowest random address
            ESP_LOGD(TAG, "Searching for next device...");
            sendCommand(DALI_ADDRESS_TYPE_SPECIAL_CMD, DALI_SPECIAL_COMMAND_SEARCHADDRH, (highest_search_addr >> 16) & 0xFF);
            sendCommand(DALI_ADDRESS_TYPE_SPECIAL_CMD, DALI_SPECIAL_COMMAND_SEARCHADDRM, (highest_search_addr >> 8) & 0xFF);
            sendCommand(DALI_ADDRESS_TYPE_SPECIAL_CMD, DALI_SPECIAL_COMMAND_SEARCHADDRL, highest_search_addr & 0xFF);

            if (auto response = sendQuery(DALI_ADDRESS_TYPE_SPECIAL_CMD, DALI_SPECIAL_COMMAND_COMPARE, 0); !response || response.value() != 0xFF) {
                ESP_LOGI(TAG, "No more devices found.");
                break;
            }

            // Perform binary search
            for (int i = 23; i >= 0; --i) {
                uint32_t test_addr = search_addr | (1UL << i);
                if (test_addr > current_search_space) continue;

                sendCommand(DALI_ADDRESS_TYPE_SPECIAL_CMD, DALI_SPECIAL_COMMAND_SEARCHADDRH, (test_addr >> 16) & 0xFF);
                sendCommand(DALI_ADDRESS_TYPE_SPECIAL_CMD, DALI_SPECIAL_COMMAND_SEARCHADDRM, (test_addr >> 8) & 0xFF);
                sendCommand(DALI_ADDRESS_TYPE_SPECIAL_CMD, DALI_SPECIAL_COMMAND_SEARCHADDRL, test_addr & 0xFF);

                if (auto response = sendQuery(DALI_ADDRESS_TYPE_SPECIAL_CMD, DALI_SPECIAL_COMMAND_COMPARE, 0); response && response.value() == 0xFF) {
                    search_addr = test_addr;
                }
            }

            ESP_LOGI(TAG, "Found device with random address 0x%06X. Assigning short address %d.", search_addr, short_addr);

            // Step 4: PROGRAM SHORT ADDRESS
            uint8_t addr_byte = (short_addr << 1) | 1;
            sendCommand(DALI_ADDRESS_TYPE_SPECIAL_CMD, DALI_SPECIAL_COMMAND_PROGRAM_SHORT_ADDRESS, addr_byte);

            // Step 5: WITHDRAW
            sendCommand(DALI_ADDRESS_TYPE_SPECIAL_CMD, DALI_SPECIAL_COMMAND_WITHDRAW, 0);
            assigned_addresses++;
        }

        // Step 6: TERMINATE
        sendCommand(DALI_ADDRESS_TYPE_SPECIAL_CMD, DALI_SPECIAL_COMMAND_TERMINATE, 0);
        ESP_LOGI(TAG, "Commissioning finished. Assigned addresses to %d devices.", assigned_addresses);

        return scanBus();
    }

    esp_err_t DaliAPI::assignToGroup(const uint8_t shortAddress, const uint8_t group) {
        if (group >= 16) return ESP_ERR_INVALID_ARG;
        return sendCommand(DALI_ADDRESS_TYPE_SHORT, shortAddress, DALI_COMMAND_ADD_TO_GROUP_0 + group);
    }

    esp_err_t DaliAPI::removeFromGroup(const uint8_t shortAddress, const uint8_t group) {
        if (group >= 16) return ESP_ERR_INVALID_ARG;
        return sendCommand(DALI_ADDRESS_TYPE_SHORT, shortAddress, DALI_COMMAND_REMOVE_FROM_GROUP_0 + group);
    }

    std::optional<std::bitset<16>> DaliAPI::getDeviceGroups(const uint8_t shortAddress) {
        const auto groups_0_7 = sendQuery(DALI_ADDRESS_TYPE_SHORT, shortAddress, DALI_COMMAND_QUERY_GROUPS_0_7);
        vTaskDelay(pdMS_TO_TICKS(5));

        if (const auto groups_8_15 = sendQuery(DALI_ADDRESS_TYPE_SHORT, shortAddress, DALI_COMMAND_QUERY_GROUPS_8_15); groups_0_7.has_value() && groups_8_15.has_value()) {
            uint16_t combined = (groups_8_15.value() << 8) | groups_0_7.value();
            return std::bitset<16>(combined);
        }

        return std::nullopt;
    }

    bool DaliAPI::isInitialized() const {
        return m_initialized;
    }
} // daliMQTT