// Copyright (c) 2026 Alice-Trade Inc.
// SPDX-License-Identifier: GPL-2.0-or-later

#ifndef DALIMQTT_DALIADAPTER_HXX
#define DALIMQTT_DALIADAPTER_HXX

#include "dali/driver/DaliDriver.hxx"
#include "dali/DaliCommands.hxx"
#include "dali/DaliСommon.hxx"

namespace daliMQTT
{
    class DaliAdapter {
    public:
        DaliAdapter(const DaliAdapter&) = delete;
        DaliAdapter& operator=(const DaliAdapter&) = delete;

        static DaliAdapter& Instance() {
            static DaliAdapter instance;
            return instance;
        }

        /**
         * @brief Initialize the Adapter and the underlying RMT driver.
         */
        esp_err_t init(gpio_num_t rx_pin, gpio_num_t tx_pin);

        /**
         * @brief Checks if the DALI bus driver is initialized.
         */
        bool isInitialized() const;

        /**
         * @brief Send a raw DALI frame asynchronously.
         */
        esp_err_t sendRaw(uint32_t data, uint8_t bits = 16);

        /**
         * @brief Send a command. If send_twice is true, it repeats the frame after 10ms.
         */
        esp_err_t sendCommand(DaliAddressType addr_type, uint8_t addr, uint8_t command, bool send_twice = false);

        /**
         * @brief Send DACP (Direct Arc Power Control) level.
         */
        esp_err_t sendDACP(DaliAddressType addr_type, uint8_t addr, uint8_t level);

        /**
         * @brief Send a query and wait for an 8-bit backward frame response.
         * @return uint8_t response or std::nullopt on timeout/collision.
         */
        [[nodiscard]] std::optional<uint8_t> sendQuery(DaliAddressType addr_type, uint8_t addr, uint8_t command);

        /**
         * @brief Send query raw.
         */
        [[nodiscard]] std::optional<uint8_t> sendRawQuery(uint32_t data, uint8_t bits = 16);

        /**
         * @brief Send Input Device Command (24-bit).
         */
        std::optional<uint8_t> sendInputDeviceCommand(uint8_t shortAddress, uint8_t opcode, std::optional<uint8_t> param = std::nullopt);

        uint8_t initializeBus(bool provision_all = true);
        uint8_t initialize24BitDevicesBus();

        /**
         * @brief Adds a device to a group.
         */
        esp_err_t assignToGroup(uint8_t shortAddress, uint8_t group);

        /**
         * @brief Removes a device from a group.
         */
        esp_err_t removeFromGroup(uint8_t shortAddress, uint8_t group);

        /**
         * @brief Gets the group mask for a device.
         */
        [[nodiscard]] std::optional<std::bitset<16>> getDeviceGroups(uint8_t shortAddress);

        [[nodiscard]] std::optional<uint8_t> getDT8Features(uint8_t shortAddress);
        [[nodiscard]] std::optional<uint8_t> getDeviceType(uint8_t shortAddress);
        [[nodiscard]] std::optional<uint8_t> getDeviceStatus(uint8_t shortAddress);
        [[nodiscard]] std::optional<std::string> getGTIN(uint8_t shortAddress);

        /**
         * @brief Reads one byte from Memory Bank.
         */
        [[nodiscard]] std::optional<uint8_t> readMemoryLocation(uint8_t shortAddress, uint8_t bank, uint8_t offset);

        /**
         * @brief Gets current Color Temperature (Tc) from Memory Bank 205.
         */
        [[nodiscard]] std::optional<uint16_t> getDT8ColorTemp(uint8_t shortAddress);

        /**
         * @brief Gets current RGB from Memory Bank 205.
         */
        [[nodiscard]] std::optional<DaliRGB> getDT8RGB(uint8_t shortAddress);

        /**
         * @brief Gets the long address of a device by short address.
         */
        [[nodiscard]] std::optional<DaliLongAddress_t> getLongAddress(uint8_t shortAddress);

        /**
         * @brief Sets DT8 Color Temperature (Tc).
         */
        esp_err_t setDT8ColorTemp(DaliAddressType addr_type, uint8_t addr, uint16_t mireds);

        /**
        * @brief Sets DT8 RGB value (Basic implementation).
        */
        esp_err_t setDT8RGB(DaliAddressType addr_type, uint8_t addr, uint8_t r, uint8_t g, uint8_t b);

        [[nodiscard]] QueueHandle_t getEventQueue() const;

        /**
        * @brief Starts the DALI bus sniffer.
        */
        esp_err_t startSniffer();

        /**
        * @brief Stops the DALI bus sniffer.
        */
        esp_err_t stopSniffer();
    private:
        DaliAdapter() = default;

        // Internal helper task to process Driver events
        [[noreturn]] static void busWorkerTask(void* arg);

        uint32_t findAddressBinarySearch(bool input_devices);
        void setDtr0(uint8_t val);
        void setDtr1(uint8_t val);
        esp_err_t sendSpecialCmdDT8(uint8_t shortAddr, uint8_t cmd);
        std::optional<uint8_t> queryDT8Value(uint8_t shortAddress, uint8_t dtr0_selector);

        Driver::DaliDriver m_driver{}; // Driver Instance

        QueueHandle_t m_dali_event_queue{nullptr}; // Event Queue

        QueueHandle_t m_response_queue{nullptr}; // Queue to pass BackwardFrame to waiting thread
        std::atomic<bool> m_expecting_response{false};
        std::mutex m_transaction_mutex{};

        TaskHandle_t m_worker_task_handle{nullptr};
        std::atomic<bool> m_initialized{false};
        std::atomic<bool> m_sniffer_enabled{false};
    };
} // daliMQTT

#endif // DALIMQTT_DALIADAPTER_HXX