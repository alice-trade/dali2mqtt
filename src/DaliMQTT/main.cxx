// Copyright (c) 2026 Alice-Trade Inc.
// SPDX-License-Identifier: GPL-2.0-or-later

#include <esp_err.h>
#include <esp_log.h>
#include <string>
#include <mutex>
#include <atomic>
#include <nvs_flash.h>

#include <system/ConfigManager.hxx>
#include <system/AppController.hxx>

static constexpr char  TAG[] = "daliMQTT";
extern "C" void app_main(void) {
    ESP_LOGI("", "DALI-to-MQTT Bridge v.%s (configured at: %s)", DALIMQTT_VERSION, DALIMQTT_CONFIGURED_TIMESTAMP);
    ESP_LOGI(TAG, "DALI-to-MQTT Bridge starting...");

    auto& config = daliMQTT::ConfigManager::Instance();
    ESP_ERROR_CHECK(config.init());
    ESP_ERROR_CHECK(config.load());

    auto& app = daliMQTT::AppController::Instance();

    if (config.isConfigured()) {
        ESP_LOGI(TAG, "Device is configured. Starting normal mode.");
        app.startNormalMode();
    } else {
        ESP_LOGI(TAG, "Device is not configured. Starting provisioning mode.");
        app.startProvisioningMode();
    }

    ESP_LOGI(TAG, "Application setup complete. Logic running in background tasks.");
}