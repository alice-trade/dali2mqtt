#include "unity.h"
#include "wifi/Wifi.hxx"

using namespace daliMQTT;

static void test_wifi_init() {
    auto& wifi = Wifi::getInstance();

    esp_err_t err = wifi.init();
    TEST_ASSERT_EQUAL(ESP_OK, err);
}

static void test_wifi_ip_string() {
    auto& wifi = Wifi::getInstance();
    std::string ip = wifi.getIpAddress();
    TEST_ASSERT_EQUAL_STRING("0.0.0.0", ip.c_str());
}

void run_wifi_logic_tests() {
    RUN_TEST(test_wifi_init);
    RUN_TEST(test_wifi_ip_string);
}