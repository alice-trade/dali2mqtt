#include "unity.h"
#include "DaliMQTT/wifi/Wifi.hxx"

using namespace daliMQTT;

TEST_CASE("WiFi can start in AP mode", "[wifi]") {
    auto& wifi = Wifi::getInstance();
    TEST_ASSERT_EQUAL(ESP_OK, wifi.init());

    TEST_ASSERT_EQUAL(ESP_OK, wifi.startAP("test-dali-ap", "testpassword"));

    // Даем время на поднятие точки доступа
    vTaskDelay(pdMS_TO_TICKS(1000));

    TEST_ASSERT_EQUAL(Wifi::Status::AP_MODE, wifi.getStatus());

    wifi.disconnect();
    vTaskDelay(pdMS_TO_TICKS(1000));
    TEST_ASSERT_EQUAL(Wifi::Status::DISCONNECTED, wifi.getStatus());
}

extern "C" void run_wifi_manager_tests(void) {
    UNITY_BEGIN();
    RUN_TEST(test_wifi_can_start_in_AP_mode);
    UNITY_END();
}