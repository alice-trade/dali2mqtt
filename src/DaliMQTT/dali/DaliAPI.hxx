#ifndef DALIMQTT_DALIAPI_HXX
#define DALIMQTT_DALIAPI_HXX
#include <optional>
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


    private:
        DaliAPI() = default;

        std::mutex bus_mutex;
    };
} // daliMQTT


#endif //DALIMQTT_DALIAPI_HXX