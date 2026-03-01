// Copyright (c) 2026 Alice-Trade Inc.
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef DALIMQTT_DALIADAPTER_HXX
#define DALIMQTT_DALIADAPTER_HXX

#include "dali/driver/DaliDriver.hxx"
#include "dali/DaliCommands.hxx"
#include "dali/DaliCommon.hxx"

namespace daliMQTT
{
    struct AdapterEvent {
        enum class Type { CMD, DRIVER_EVENT };
        struct CmdData {
            uint32_t data;
            uint8_t bits;
            bool is_query;
            bool send_twice;
            TaskHandle_t caller;
        };
        Type type{};
        CmdData cmd{};
        Driver::DaliMessage msg;
    };

    class DaliAdapter {
    public:
        DaliAdapter(uint8_t bus_id, QueueHandle_t central_queue);
        ~DaliAdapter();

        /**
         * @brief Initialize the Adapter and the underlying RMT driver.
         */
        esp_err_t init(gpio_num_t rx_pin, gpio_num_t tx_pin);

        /**
         * @brief Checks if the DALI bus driver is initialized.
         */
        [[nodiscard]] bool isInitialized() const {
            return m_initialized;
        }

        [[nodiscard]] uint8_t getBusId() const { return m_bus_id; }

        /**
         * @brief Send a raw DALI frame asynchronously.
         */
        esp_err_t sendRaw(uint32_t data, uint8_t bits = 16) const;

        /**
         * @brief Send a standard 16-bit command (IEC 62386-102).
         */
        esp_err_t sendCommand(DaliAddressType addr_type, uint8_t addr, Commands::OpCode command, bool send_twice = false) const;

        /**
         * @brief Send a special command (IEC 62386-102).
         * Special commands (like INITIALISE) often require specific handling.
         */
        esp_err_t sendCommand(Commands::SpecialOpCode command, uint8_t data, bool send_twice = false) const;

        /**
         * @brief Send a DT8 command (IEC 62386-209).
         * Note: Usually requires sequence (Enable -> Cmd -> Activate), handled by helpers setDT8.
         */
        esp_err_t sendCommand (const DaliAddressType addr_type, const uint8_t addr, const Commands::DT8OpCode command, const bool send_twice) {
            return sendCommand(addr_type, addr, static_cast<Commands::OpCode>(command), send_twice);
        }

        /**
         * @brief Send DACP (Direct Arc Power Control) level.
         */
        esp_err_t sendDACP(DaliAddressType addr_type, uint8_t addr, uint8_t level) const;

        /**
         * @brief Send a standard query (IEC 62386-102) and wait for an 8-bit backward frame response.
         * @return uint8_t response or std::nullopt on timeout/collision.
         */
        [[nodiscard]] std::optional<uint8_t> sendQuery(DaliAddressType addr_type, uint8_t addr, Commands::OpCode command) const;

        /**
         * @brief Send a query of SPECIAL command and wait for an 8-bit backward frame response.
         * @return uint8_t response or std::nullopt on timeout/collision.
         */
        [[nodiscard]] std::optional<uint8_t> sendQuery(Commands::SpecialOpCode command, uint8_t data) const;

        /**
         * @brief Send a DT8 query (IEC 62386-209) and wait for an 8-bit backward frame response.
         *  @return uint8_t response or std::nullopt on timeout/collision.
         */
        [[nodiscard]] std::optional<uint8_t> sendQuery(DaliAddressType addr_type, uint8_t addr, Commands::DT8OpCode command) const {
            return sendQuery(addr_type, addr, static_cast<Commands::OpCode>(command));
        }

        /**
         * @brief Send query raw.
         */
        [[nodiscard]] std::optional<uint8_t> sendRawQuery(uint32_t data, uint8_t bits = 16) const;

        /**
         * @brief Send Input Device Command (24-bit).
         */
        [[nodiscard]] std::optional<uint8_t> sendInputDeviceCommand(uint8_t shortAddress, uint8_t opcode, std::optional<uint8_t> param = std::nullopt) const;

        uint8_t initializeBus(bool provision_all = true);

        uint8_t initialize24BitDevicesBus();

        /**
         * @brief Adds a device to a group.
         */
        [[nodiscard]] esp_err_t assignToGroup(const uint8_t shortAddress, const uint8_t group) const {
            return sendCommand(DaliAddressType::Short, shortAddress, static_cast<Commands::OpCode>(0x60 + group), true);
        }

        /**
         * @brief Removes a device from a group.
         */
        [[nodiscard]] esp_err_t removeFromGroup(const uint8_t shortAddress, const uint8_t group) const {
            return sendCommand(DaliAddressType::Short, shortAddress, static_cast<Commands::OpCode>(0x70 + group), true);
        }

        /**
         * @brief Gets the group mask for a device.
         */
        [[nodiscard]] std::optional<std::bitset<16>> getDeviceGroups(uint8_t shortAddress);

        [[nodiscard]] std::optional<uint8_t> getDT8Features(uint8_t shortAddress);

        [[nodiscard]] std::optional<uint8_t> getDeviceType(const uint8_t shortAddress) {
            return sendQuery(DaliAddressType::Short, shortAddress, Commands::OpCode::QueryDeviceType);
        }

        [[nodiscard]] std::optional<uint8_t> getDeviceStatus(const uint8_t shortAddress) {
            return sendQuery(DaliAddressType::Short, shortAddress, Commands::OpCode::QueryStatus);
        }

        [[nodiscard]] std::optional<etl::string<16>> getGTIN(uint8_t shortAddress);

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

        [[nodiscard]] QueueHandle_t getEventQueue() const {
            return m_dali_event_queue;
        }

        void lockBus() const { if (m_bus_mutex) xSemaphoreTakeRecursive(m_bus_mutex, portMAX_DELAY); }
        void unlockBus() const { if (m_bus_mutex) xSemaphoreGiveRecursive(m_bus_mutex); }

        esp_err_t startSniffer();

        esp_err_t stopSniffer();

        void onDriverEvent(const Driver::DaliMessage& msg) const;

    private:
        uint8_t m_bus_id{};

        // Internal helper task to process Driver events
        [[noreturn]] static void busWorkerTask(void* arg);

        [[nodiscard]] uint32_t findAddressBinarySearch(bool input_devices) const;

        void setDtr0(const uint8_t val) { sendRaw(Commands::Factory::Special(Commands::SpecialOpCode::Dtr0, val).data, 16); }
        void setDtr1(const uint8_t val) { sendRaw(Commands::Factory::Special(Commands::SpecialOpCode::Dtr1, val).data, 16); }

        esp_err_t sendDT8Cmd(uint8_t shortAddr, Commands::DT8OpCode cmd);
        std::optional<uint8_t> queryDT8Value(uint8_t shortAddress, uint8_t dtr0_selector);

        Driver::DaliDriver m_driver{};
        QueueHandle_t m_event_queue{nullptr};
        QueueHandle_t m_dali_event_queue{nullptr};
        SemaphoreHandle_t m_bus_mutex{nullptr};

        etl::queue<AdapterEvent::CmdData, 16> m_cmd_buffer;
        TaskHandle_t m_worker_task_handle{nullptr};
        std::atomic<bool> m_initialized{false};
        std::atomic<bool> m_sniffer_enabled{false};

        static constexpr UBaseType_t NOTIFY_IDX = 0; // Use idx 1 ??
    };
} // daliMQTT

#endif // DALIMQTT_DALIADAPTER_HXX