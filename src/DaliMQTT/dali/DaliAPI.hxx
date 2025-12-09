#ifndef DALIMQTT_DALIAPI_HXX
#define DALIMQTT_DALIAPI_HXX
#include "DaliDriver.hxx"
#include "dali_commands.h"
#include "driver/gptimer.h"
    #include "DaliTypes.hxx"


namespace daliMQTT
{

    class DaliAPI {
    public:
        DaliAPI(const DaliAPI&) = delete;
        DaliAPI& operator=(const DaliAPI&) = delete;

        static DaliAPI& getInstance() {
            static DaliAPI instance;
            return instance;
        }
        esp_err_t init(gpio_num_t rx_pin, gpio_num_t tx_pin);

        // Отправка команды без ожидания ответа
        esp_err_t sendCommand(dali_addressType_t addr_type, uint8_t addr, uint8_t command, bool send_twice = false);
        // DACP - Direct Arc Power Control
        esp_err_t sendDACP(dali_addressType_t addr_type, uint8_t addr, uint8_t level);
        // Отправка запроса с ожиданием ответа
        [[nodiscard]] std::optional<uint8_t> sendQuery(dali_addressType_t addr_type, uint8_t addr, uint8_t command);
        std::optional<uint8_t> sendInputDeviceCommand(uint8_t shortAddress, uint8_t opcode, std::optional<uint8_t> param = std::nullopt);
        // Raw send command
        [[nodiscard]] std::optional<uint8_t> sendRaw(uint32_t data, uint8_t bits = 16);
        // DT8 Set Color Temperature (Tc)
        esp_err_t setDT8ColorTemp(dali_addressType_t addr_type, uint8_t addr, uint16_t mireds);
        // DT8 Set RGB (Basic implementation)
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

        // Процесс инициализации и адресации новых устройств на шине
        uint8_t initializeBus();
        uint8_t initialize24BitDevicesBus();

        // Добавить в группу
        esp_err_t assignToGroup(uint8_t shortAddress, uint8_t group);

        // Удалить из группы
        esp_err_t removeFromGroup(uint8_t shortAddress, uint8_t group);

        // Получение маски группы
        [[nodiscard]] std::optional<std::bitset<16>> getDeviceGroups(uint8_t shortAddress);

        [[nodiscard]] std::optional<uint8_t> getDT8Features(uint8_t shortAddress);

        [[nodiscard]] std::optional<uint8_t> readInputDeviceMemory(uint8_t shortAddress, uint8_t bank, uint8_t offset);

        std::optional<uint8_t> getDeviceType(uint8_t shortAddress);

        std::optional<std::string> getGTIN(uint8_t shortAddress);

        std::optional<uint8_t> getDeviceStatus(uint8_t shortAddress);

        // Чтение одного байта из Memory Bank
        [[nodiscard]] std::optional<uint8_t> readMemoryLocation(uint8_t shortAddress, uint8_t bank, uint8_t offset);

        // Получить текущую цветовую температуру (Tc) из Memory Bank 205
        [[nodiscard]] std::optional<uint16_t> getDT8ColorTemp(uint8_t shortAddress);

        // Получить текущий RGB из Memory Bank 205
        [[nodiscard]] std::optional<DaliRGB> getDT8RGB(uint8_t shortAddress);

        // Получение long address устройства по short address
        [[nodiscard]] std::optional<DaliLongAddress_t> getLongAddress(uint8_t shortAddress);

        // Проверка, была ли инициализирована шина DALI
        [[nodiscard]] bool isInitialized() const;

    private:
        DaliAPI() = default;
        [[noreturn]] static void dali_sniffer_task(void* arg);
        esp_err_t sendSpecialCmdDT8(uint8_t shortAddr, uint8_t cmd);


        Dali m_dali_impl;
        gptimer_handle_t m_dali_timer{nullptr};
        TaskHandle_t m_sniffer_task_handle{nullptr};
        std::recursive_mutex bus_mutex;
        std::atomic<bool> m_initialized{false};
        QueueHandle_t m_dali_event_queue{nullptr};
        static void IRAM_ATTR rx_complete_isr(void* arg);

    };
} // daliMQTT


#endif //DALIMQTT_DALIAPI_HXX