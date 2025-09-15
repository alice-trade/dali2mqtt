#ifndef DALIMQTT_DALIAPI_HXX
#define DALIMQTT_DALIAPI_HXX
#include <optional>
#include <atomic>
#include <mutex>
#include <bitset>

#include <driver/gpio.h>
#include <freertos/FreeRTOS.h>
#include "dalic/include/dali.h"
#include "dalic/include/dali_commands.h"


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

        // Отправка запроса с ожиданием ответа
        [[nodiscard]] std::optional<uint8_t> sendQuery(dali_addressType_t addr_type, uint8_t addr, uint8_t command);

        // Сканирование шины на наличие уже адресованных устройств
        [[nodiscard]] std::bitset<64> scanBus();

        // Процесс инициализации и адресации новых устройств на шине
        [[nodiscard]] std::bitset<64> initializeBus();

        // Добавить в группу
        esp_err_t assignToGroup(uint8_t shortAddress, uint8_t group);

        // Удалить из группы
        esp_err_t removeFromGroup(uint8_t shortAddress, uint8_t group);

        // Получение маски группы
        [[nodiscard]] std::optional<std::bitset<16>> getDeviceGroups(uint8_t shortAddress);

        // Проверка, была ли инициализирована шина DALI
        [[nodiscard]] bool isInitialized() const;

    private:
        DaliAPI() = default;

        std::mutex bus_mutex;
        std::atomic<bool> m_initialized{false};
    };
} // daliMQTT


#endif //DALIMQTT_DALIAPI_HXX