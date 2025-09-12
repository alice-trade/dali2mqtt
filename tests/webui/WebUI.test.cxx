#include "unity.h"
#include "DaliMQTT/webui/cxx/WebUI.hxx"
#include "DaliMQTT/wifi/Wifi.hxx"
#include "DaliMQTT/config/ConfigManager.hxx"
#include "esp_http_client.h"
#include "mbedtls/base64.h"

using namespace daliMQTT;

static const char* TEST_USER = "test_admin";
static const char* TEST_PASS = "test_password";

// --- Test Group Setup ---
static void webui_setup(void) {
    auto& wifi = Wifi::getInstance();
    wifi.init();
    wifi.startAP("webui-test-ap", "password");

    // Устанавливаем известные креды для теста
    auto& cfg = ConfigManager::getInstance();
    cfg.init();
    AppConfig app_cfg = cfg.getConfig();
    app_cfg.http_user = TEST_USER;
    app_cfg.http_pass = TEST_PASS;
    cfg.setConfig(app_cfg);

    auto& webui = WebUI::getInstance();
    webui.start();

    // Ждем, пока поднимется AP и сервер
    vTaskDelay(pdMS_TO_TICKS(2000));
}

static void webui_teardown(void) {
    WebUI::getInstance().stop();
    Wifi::getInstance().disconnect();
    vTaskDelay(pdMS_TO_TICKS(1000));
}

// --- Test Cases ---
TEST_CASE("WebUI API requires authentication", "[webui]") {
    webui_setup();
    esp_http_client_config_t config = { .host = "192.168.4.1", .path = "/api/info" };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    esp_http_client_perform(client);

    TEST_ASSERT_EQUAL(401, esp_http_client_get_status_code(client));

    esp_http_client_cleanup(client);
    webui_teardown();
}

TEST_CASE("WebUI API authentication succeeds with correct credentials", "[webui]") {
    webui_setup();
    esp_http_client_config_t config = { .host = "192.168.4.1", .path = "/api/info" };
    esp_http_client_handle_t client = esp_http_client_init(&config);

    // Готовим заголовок Basic Auth
    std::string credentials = std::string(TEST_USER) + ":" + std::string(TEST_PASS);
    unsigned char base64_buf[128];
    size_t encoded_len;
    mbedtls_base64_encode(base64_buf, sizeof(base64_buf), &encoded_len, (const unsigned char*)credentials.c_str(), credentials.length());
    esp_http_client_set_header(client, "Authorization", ("Basic " + std::string((char*)base64_buf, encoded_len)).c_str());

    esp_http_client_perform(client);

    TEST_ASSERT_EQUAL(200, esp_http_client_get_status_code(client));

    esp_http_client_cleanup(client);
    webui_teardown();
}

// --- Test Runner ---
extern "C" void run_webui_tests(void) {
    UNITY_BEGIN();
    RUN_TEST(test_webui_API_requires_authentication);
    RUN_TEST(test_webui_API_authentication_succeeds_with_correct_credentials);
    UNITY_END();
}