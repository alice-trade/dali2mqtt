#include "AppUpdateManager.hxx"
#include <esp_log.h>
#include <esp_http_client.h>
#include <esp_https_ota.h>
#include <esp_crt_bundle.h>
#include <esp_system.h>
#include <esp_timer.h>
#include <esp_partition.h>

namespace daliMQTT {
    static constexpr char TAG[] = "AppUpdate";
    static constexpr char SPIFFS_PARTITION_LABEL[] = CONFIG_DALI2MQTT_WEBUI_SPIFFS_PARTITION_LABEL;

    bool AppUpdateManager::startUpdateAsync(const std::string& url, int type) {
        if (m_is_updating.exchange(true)) {
            ESP_LOGW(TAG, "Update already in progress.");
            return false;
        }

        if (url.empty()) {
            ESP_LOGE(TAG, "URL is empty.");
            m_is_updating = false;
            return false;
        }

        auto* params = new TaskParams{url, type};

        BaseType_t ret = xTaskCreate(otaTask, "ota_task", 8192, params, 5, nullptr);
        if (ret != pdPASS) {
            ESP_LOGE(TAG, "Failed to create OTA task.");
            delete params;
            m_is_updating = false;
            return false;
        }

        return true;
    }


    bool AppUpdateManager::isUpdateInProgress() const {
        return m_is_updating;
    }

    void AppUpdateManager::otaTask(void* pvParameter) {
        const auto* params = static_cast<TaskParams*>(pvParameter);

        if (params->type == 1) {
            AppUpdateManager::getInstance().performSpiffsUpdate(params->url);
        } else {
            AppUpdateManager::getInstance().performUpdate(params->url);
        }

        delete params;
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

    void AppUpdateManager::performSpiffsUpdate(const std::string& url) {
        ESP_LOGI(TAG, "Starting SPIFFS update from: %s", url.c_str());

        const esp_partition_t* part = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_SPIFFS, SPIFFS_PARTITION_LABEL);
        if (!part) {
            ESP_LOGE(TAG, "SPIFFS partition not found!");
            m_is_updating = false;
            return;
        }

        esp_http_client_config_t config = {};
        config.url = url.c_str();
        config.timeout_ms = 10000;
        config.buffer_size = 4096;

        esp_http_client_handle_t client = esp_http_client_init(&config);
        if (!client) {
            ESP_LOGE(TAG, "Failed to init HTTP client");
            m_is_updating = false;
            return;
        }

        esp_err_t err = esp_http_client_open(client, 0);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to open HTTP connection: %s", esp_err_to_name(err));
            esp_http_client_cleanup(client);
            m_is_updating = false;
            return;
        }

        int content_len = static_cast<int> (esp_http_client_fetch_headers(client));
        if (content_len <= 0) {
            ESP_LOGW(TAG, "Content length invalid or zero, proceeding cautiously");
        }

        constexpr int buffer_len = 4096;
        auto buffer = static_cast<char *>(malloc(buffer_len));
        if (!buffer) {
            ESP_LOGE(TAG, "OOM: Failed to allocate buffer");
            esp_http_client_cleanup(client);
            m_is_updating = false;
            return;
        }

        err = esp_partition_erase_range(part, 0, part->size);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to erase partition: %s", esp_err_to_name(err));
            free(buffer);
            esp_http_client_cleanup(client);
            m_is_updating = false;
            return;
        }

        int total_read = 0;
        int read_len = 0;
        while (true) {
            read_len = esp_http_client_read(client, buffer, buffer_len);
            if (read_len > 0) {
                err = esp_partition_write(part, total_read, buffer, read_len);
                if (err != ESP_OK) {
                    ESP_LOGE(TAG, "Partition write failed at offset %d: %s", total_read, esp_err_to_name(err));
                    break;
                }
                total_read += read_len;
                if (total_read % 65536 == 0) ESP_LOGI(TAG, "Written %d bytes...", total_read);
            } else if (read_len == 0) {
                break; // Done
            } else {
                ESP_LOGE(TAG, "HTTP read error");
                err = ESP_FAIL;
                break;
            }
        }

        ESP_LOGI(TAG, "SPIFFS Update finished. Total: %d bytes. Result: %s", total_read, esp_err_to_name(err));

        free(buffer);
        esp_http_client_close(client);
        esp_http_client_cleanup(client);

        if (err == ESP_OK) {
            ESP_LOGI(TAG, "SPIFFS Updated. Rebooting...");
            vTaskDelay(pdMS_TO_TICKS(1000));
            esp_restart();
        } else {
            m_is_updating = false;
        }
    }
}