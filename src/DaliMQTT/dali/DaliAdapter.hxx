#ifndef DALIMQTT_DALIAPI_HXX
#define DALIMQTT_DALIAPI_HXX
#include "dali/driver/DaliDriver.hxx"
#include "dali/driver/dali_commands.h"
#include "driver/gptimer.h"
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
        esp_err_t init(gpio_num_t rx_pin, gpio_num_t tx_pin);

        /**
         * @brief Sends a command without waiting for a reply.
         */
        esp_err_t sendCommand(dali_addressType_t addr_type, uint8_t addr, uint8_t command, bool send_twice = false);

        /**
         * @brief DACP - Direct Arc Power Control.
         */
        esp_err_t sendDACP(dali_addressType_t addr_type, uint8_t addr, uint8_t level);

        /**
         * @brief Sends a query waiting for a reply.
         */
        [[nodiscard]] std::optional<uint8_t> sendQuery(dali_addressType_t addr_type, uint8_t addr, uint8_t command);
        std::optional<uint8_t> sendInputDeviceCommand(uint8_t shortAddress, uint8_t opcode, std::optional<uint8_t> param = std::nullopt);

        /**
         * @brief Sends a raw command.
         */
        [[nodiscard]] std::optional<uint8_t> sendRaw(uint32_t data, uint8_t bits = 16, bool reply = true);

        /**
         * @brief Sets DT8 Color Temperature (Tc).
         */
        esp_err_t setDT8ColorTemp(dali_addressType_t addr_type, uint8_t addr, uint16_t mireds);

        /**
         * @brief Sets DT8 RGB value (Basic implementation).
         */
        esp_err_t setDT8RGB(dali_addressType_t addr_type, uint8_t addr, uint8_t r, uint8_t g, uint8_t b);

        /**
      * @brief Starts the DALI bus sniffer.
      *
      * @return esp_err_t ESP_OK on success.
      */
        esp_err_t startSniffer();

        /**
         * @brief Stops the DALI bus sniffer.
         *
         * @return esp_err_t ESP_OK on success.
         */
        esp_err_t stopSniffer();

        /**
         * @brief Gets the handle to the DALI event queue.
         * The sniffer pushes decoded dali_frame_t items into this queue.
         *
         * @return QueueHandle_t Handle to the event queue, or nullptr if not initialized.
         */
        [[nodiscard]] QueueHandle_t getEventQueue() const;

        /**
         * @brief Initialization and addressing process for new devices on the bus.
         */
        uint8_t initializeBus();
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

        std::optional<uint8_t> getDeviceType(uint8_t shortAddress);

        std::optional<std::string> getGTIN(uint8_t shortAddress);

        std::optional<uint8_t> getDeviceStatus(uint8_t shortAddress);

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
         * @brief Checks if the DALI bus driver is initialized.
         */
        [[nodiscard]] bool isInitialized() const;

    private:
        DaliAdapter() = default;
        [[noreturn]] static void dali_sniffer_task(void* arg);
        esp_err_t sendSpecialCmdDT8(uint8_t shortAddr, uint8_t cmd);
        std::optional<uint8_t> queryDT8Value(uint8_t shortAddress, uint8_t dtr0_selector);
        static void IRAM_ATTR rx_complete_isr(void* arg);

        Dali m_dali_impl{};
        gptimer_handle_t m_dali_timer{nullptr};
        TaskHandle_t m_sniffer_task_handle{nullptr};
        std::recursive_mutex bus_mutex{};
        std::atomic<bool> m_initialized{false};
        QueueHandle_t m_dali_event_queue{nullptr};

    };
} // daliMQTT


#endif //DALIMQTT_DALIAPI_HXX