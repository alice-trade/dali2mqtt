// Copyright (c) 2026 Alice-Trade Inc.
// SPDX-License-Identifier: GPL-2.0-or-later

#include <unity.h>
#include <esp_event.h>
#include <esp_log.h>
#include <nvs_flash.h>
#include <nvs.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

static constexpr char TAG[] = "TestRunner";

void run_config_store_tests();
void run_dali_hardware_and_registry_tests();
void run_mqtt_and_bridge_tests();
void run_wifi_and_system_tests();
void run_http_stream_tests();

static void erase_namespace(const char* ns) {
    nvs_handle_t h;
    if (nvs_open(ns, NVS_READWRITE, &h) == ESP_OK) {
        nvs_erase_all(h);
        nvs_commit(h);
        nvs_close(h);
        ESP_LOGI(TAG, "Test namespace '%s' cleared.", ns);
    }
}

static void unity_test_task(void* pvParameters) {
    ESP_LOGI(TAG, "Starting Unity Tests in dedicated task...");
    vTaskDelay(pdMS_TO_TICKS(100));

    UNITY_BEGIN();

    run_config_store_tests();
    run_dali_hardware_and_registry_tests();
    run_mqtt_and_bridge_tests();
    run_wifi_and_system_tests();
    run_http_stream_tests();

    UNITY_END();

    ESP_LOGI(TAG, "All unit tests executed successfully.");
    vTaskDelete(nullptr);
}

extern "C" void app_main(void) {
    ESP_LOGI(TAG, "Initializing hardware environment for Unity tests...");

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    erase_namespace("tst_cfg");
    erase_namespace("tst_reg");

    xTaskCreate(unity_test_task, "unity_task", 24576, nullptr, 5, nullptr);
}