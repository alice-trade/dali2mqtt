#include "AppUpdateManager.hxx"
#include <esp_log.h>
#include <esp_http_client.h>
#include <esp_https_ota.h>
#include <esp_crt_bundle.h>
#include <esp_system.h>
#include <esp_timer.h>

namespace daliMQTT {
    static constexpr char TAG[] = "AppUpdate";

    bool AppUpdateManager::startUpdateAsync(const std::string& url) {
        if (m_is_updating.exchange(true)) {
            ESP_LOGW(TAG, "OTA update already in progress.");
            return false;
        }

        if (url.empty()) {
            ESP_LOGE(TAG, "OTA URL is empty.");
            m_is_updating = false;
            return false;
        }

        // Копируем URL в новую строку для передачи в задачу
        std::string* url_copy = new std::string(url);

        BaseType_t ret = xTaskCreate(otaTask, "ota_task", 8192, url_copy, 5, nullptr);
        if (ret != pdPASS) {
            ESP_LOGE(TAG, "Failed to create OTA task.");
            delete url_copy;
            m_is_updating = false;
            return false;
        }

        return true;
    }

    bool AppUpdateManager::isUpdateInProgress() const {
        return m_is_updating;
    }

    void AppUpdateManager::otaTask(void* pvParameter) {
        std::string* url_ptr = static_cast<std::string*>(pvParameter);
        std::string url = *url_ptr;
        delete url_ptr;

        AppUpdateManager::getInstance().performUpdate(url);

        vTaskDelete(nullptr);
    }

    void AppUpdateManager::performUpdate(const std::string& url) {
        ESP_LOGI(TAG, "Starting OTA update from: %s", url.c_str());

        esp_http_client_config_t config = {};
        config.url = url.c_str();
        config.timeout_ms = 10000;
        config.crt_bundle_attach = esp_crt_bundle_attach;
        config.keep_alive_enable = true;

        esp_https_ota_config_t ota_config = {};
        ota_config.http_config = &config;

        esp_err_t ret = esp_https_ota(&ota_config);

        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "OTA Upgrade successful. Rebooting in 3 seconds...");
            vTaskDelay(pdMS_TO_TICKS(3000));
            esp_restart();
        } else {
            ESP_LOGE(TAG, "OTA Upgrade failed: %s", esp_err_to_name(ret));
        }

        m_is_updating = false;
    }
}