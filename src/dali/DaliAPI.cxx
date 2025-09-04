#include "DaliAPI.hxx"
namespace daliMQTT
{
    DaliAPI& DaliAPI::getInstance() {
        static DaliAPI instance;
        return instance;
    }

    esp_err_t DaliAPI::init(gpio_num_t rx_pin, gpio_num_t tx_pin) {
        return dali_init(rx_pin, tx_pin);
    }

    esp_err_t DaliAPI::sendCommand(dali_addressType_t addr_type, uint8_t addr, uint8_t command, bool send_twice) {
        std::lock_guard<std::mutex> lock(bus_mutex);
        esp_err_t err = dali_transaction(addr_type, addr, true, command, send_twice, DALI_TX_TIMEOUT_DEFAULT_MS, nullptr);
        dali_wait_between_frames();
        return err;
    }

    std::optional<uint8_t> DaliAPI::sendQuery(dali_addressType_t addr_type, uint8_t addr, uint8_t command) {
        std::lock_guard<std::mutex> lock(bus_mutex);
        int result = DALI_RESULT_NO_REPLY;
        esp_err_t err = dali_transaction(addr_type, addr, true, command, false, DALI_TX_TIMEOUT_DEFAULT_MS, &result);
        dali_wait_between_frames();

        if (err == ESP_OK && result != DALI_RESULT_NO_REPLY) {
            return static_cast<uint8_t>(result);
        }

        return std::nullopt;
    }
} // daliMQTT