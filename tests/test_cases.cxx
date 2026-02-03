// Copyright (c) 2026 Alice-Trade Inc.
// SPDX-License-Identifier: GPL-2.0-or-later

#include <stdio.h>
#include <string.h>
#include "unity.h"
#include "nvs_flash.h"
#include "esp_event.h"
#include "esp_log.h"

static const char* TAG = "TEST_RUNNER";

void run_config_manager_tests();
void run_dali_logic_tests();
void run_mqtt_logic_tests();
void run_wifi_logic_tests();

extern "C" void app_main(void) {
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    ESP_LOGI(TAG, "Starting Unity Tests...");

    UNITY_BEGIN();

    run_config_manager_tests();
    run_dali_logic_tests();
    run_mqtt_logic_tests();
    run_wifi_logic_tests();

    UNITY_END();

    ESP_LOGI(TAG, "All tests finished. System will verify output.");
}