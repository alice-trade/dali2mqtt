#include "unity.h"
#include "DaliMQTT/dali/DaliAPI.hxx"
#include "sdkconfig.h"

using namespace daliMQTT;

// ПИНЫ для теста. Убедитесь, что они свободны на вашей плате.
#define TEST_DALI_RX_PIN (gpio_num_t)CONFIG_DALI2MQTT_DALI_RX_PIN
#define TEST_DALI_TX_PIN (gpio_num_t)CONFIG_DALI2MQTT_DALI_TX_PIN

TEST_CASE("DaliAPI can be initialized", "[dali][hardware]") {
    auto& dali = DaliAPI::getInstance();
    // Инициализация должна пройти успешно
    TEST_ASSERT_EQUAL(ESP_OK, dali.init(TEST_DALI_RX_PIN, TEST_DALI_TX_PIN));
}

TEST_CASE("DaliAPI sendCommand does not crash without devices", "[dali][hardware]") {
    auto& dali = DaliAPI::getInstance();
    dali.init(TEST_DALI_RX_PIN, TEST_DALI_TX_PIN);

    // Отправка команды на широковещательный адрес. Ошибки быть не должно,
    // даже если никто не слушает.
    esp_err_t err = dali.sendCommand(DALI_ADDRESS_TYPE_BROADCAST, 0, DALI_COMMAND_OFF, false);
    TEST_ASSERT_EQUAL(ESP_OK, err);
}

TEST_CASE("DaliAPI sendQuery returns nullopt on timeout", "[dali][hardware]") {
    auto& dali = DaliAPI::getInstance();
    dali.init(TEST_DALI_RX_PIN, TEST_DALI_TX_PIN);

    // Запрос на адрес 0. Без устройства на линии мы должны получить таймаут.
    // API должен обработать это и вернуть std::nullopt.
    auto result = dali.sendQuery(DALI_ADDRESS_TYPE_SHORT, 0, DALI_COMMAND_QUERY_ACTUAL_LEVEL);
    TEST_ASSERT_FALSE(result.has_value());
}

// --- Test Runner ---
extern "C" void run_dali_api_tests(void) {
    UNITY_BEGIN();
    RUN_TEST(test_dali_api_can_be_initialized);
    RUN_TEST(test_dali_api_sendCommand_does_not_crash_without_devices);
    RUN_TEST(test_dali_api_sendQuery_returns_nullopt_on_timeout);
    UNITY_END();
}