//
// Unity tests entrypoint
//
#include <stdio.h>
#include <string.h>
#include "unity.h"
#include "nvs_flash.h"
#include "esp_event.h"

#ifdef __cplusplus
extern "C" {
#endif

    void run_config_manager_tests(void);
    void run_dali_api_tests(void);
    void run_mqtt_client_tests(void);
    void run_wifi_manager_tests(void);
    void run_webui_tests(void);
    void run_lifecycle_tests(void);

#ifdef __cplusplus
}
#endif


static void print_banner(const char* text) {
    printf("\n================================================================================\n");
    printf("     %s\n", text);
    printf("================================================================================\n");
}

void app_main(void) {
    // Инициализируем базовые сервисы, необходимые для многих тестов
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    // Задержка для стабилизации системы перед запуском тестов
    vTaskDelay(pdMS_TO_TICKS(2000));

    print_banner("RUNNING CONFIG MANAGER TESTS");
    run_config_manager_tests();

    print_banner("RUNNING MQTT CLIENT LOGIC TESTS");
    run_mqtt_client_tests();

    print_banner("RUNNING WIFI MANAGER LOGIC TESTS");
    run_wifi_manager_tests();

    print_banner("RUNNING WEBUI API TESTS (requires WiFi AP)");
    run_webui_tests();

    print_banner("RUNNING LIFECYCLE LOGIC TESTS");
    run_lifecycle_tests();

    // DALI тесты требуют физического подключения или будут падать по таймауту.
    print_banner("RUNNING DALI API SMOKE TESTS (requires hardware loopback or device)");
    run_dali_api_tests();

    print_banner("ALL TESTS FINISHED");
}
