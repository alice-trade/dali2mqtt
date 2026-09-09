// Copyright (c) 2026 Alice-Trade Inc.
// SPDX-License-Identifier: GPL-3.0-or-later

#include "system/OtaService.hxx"
#include <ArduinoJson.h>
#include <algorithm>
#include <cstring>
#include <esp_crt_bundle.h>
#include <esp_http_client.h>
#include <esp_https_ota.h>
#include <esp_log.h>
#include <esp_partition.h>
#include <esp_system.h>

namespace daliMQTT {

static constexpr char TAG[] = "OtaService";
static constexpr char FS_PARTITION_LABEL[] = CONFIG_DALI2MQTT_WEBUI_SPIFFS_PARTITION_LABEL;
static constexpr size_t STREAM_BUFFER_SIZE = 4096;

OtaService::OtaService() = default;

OtaService::~OtaService() {
    if (m_taskHandle) {
        vTaskDelete(m_taskHandle);
    }
}

esp_err_t OtaService::startUpdate(const char* url, const bool updateWebFs) {
    if (m_isUpdating.exchange(true)) {
        ESP_LOGW(TAG, "OTA Update already in progress");
        return ESP_ERR_INVALID_STATE;
    }

    if (!url || strlen(url) == 0) {
        std::lock_guard<std::mutex> lock(m_infoMutex);
        if (!m_versionInfo.firmwareUrl.empty()) {
            m_targetUrl = m_versionInfo.firmwareUrl;
        }
    } else {
        m_targetUrl = url;
    }

    if (m_targetUrl.empty()) {
        m_isUpdating.store(false);
        ESP_LOGE(TAG, "Cannot start update: target URL is empty");
        return ESP_ERR_INVALID_ARG;
    }

    m_updateWebFs = updateWebFs;
    m_status.store(OtaStatus::InProgress);

    const BaseType_t res = xTaskCreate(otaTaskRunner, "ota_worker", 8192, this, 4, &m_taskHandle);
    if (res != pdPASS) {
        ESP_LOGE(TAG, "Failed to create OTA task");
        m_isUpdating.store(false);
        m_status.store(OtaStatus::Failed);
        return ESP_ERR_NO_MEM;
    }

    return ESP_OK;
}

void OtaService::notifyProgress(const OtaStatus status, const uint8_t percentage, const char* desc) {
    m_status.store(status);
    if (m_progressCb) {
        OtaProgressEvent ev{.status = status, .percentage = percentage, .stepDescription = desc};
        m_progressCb(ev, m_progressCtx);
    }
}

void OtaService::otaTaskRunner(void* arg) {
    static_cast<OtaService*>(arg)->otaWorkerLoop();
}

[[noreturn]] void OtaService::otaWorkerLoop() {
    ESP_LOGI(TAG, "Starting OTA update from: %s", m_targetUrl.c_str());
    notifyProgress(OtaStatus::InProgress, 0, "Connecting to firmware server...");

    esp_err_t err = performAppOta(m_targetUrl.c_str());
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "App OTA Failed: %s", esp_err_to_name(err));
        notifyProgress(OtaStatus::Failed, 0, "Firmware update failed");
        m_isUpdating.store(false);
        vTaskDelete(nullptr);
    }

    if (m_updateWebFs) {
        etl::string<160> fsUrl = m_targetUrl;
        const size_t pos = fsUrl.find("firmware.bin");
        if (pos != etl::string<160>::npos) {
            fsUrl.replace(pos, 12, "web_storage.bin");
        }

        err = performFsOta(fsUrl.c_str());
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "WebUI LittleFS update skipped/failed: %s", esp_err_to_name(err));
        }
    }

    notifyProgress(OtaStatus::Success, 100, "Update successful. Rebooting...");
    vTaskDelay(pdMS_TO_TICKS(2000));
    esp_restart();
}

esp_err_t OtaService::performAppOta(const char* appUrl) {
    esp_http_client_config_t httpCfg{};
    httpCfg.url = appUrl;
    httpCfg.timeout_ms = 20000;
    httpCfg.keep_alive_enable = true;
    httpCfg.max_redirection_count = 5;
    httpCfg.crt_bundle_attach = esp_crt_bundle_attach;

    esp_https_ota_config_t otaCfg{};
    otaCfg.http_config = &httpCfg;

    esp_https_ota_handle_t otaHandle{nullptr};
    ESP_RETURN_ON_ERROR(esp_https_ota_begin(&otaCfg, &otaHandle), TAG, "HTTPS OTA Begin failed");

    while (true) {
        const esp_err_t ret = esp_https_ota_perform(otaHandle);
        if (ret == ESP_ERR_HTTPS_OTA_IN_PROGRESS) {
            const int totalBytes = esp_https_ota_get_image_len_read(otaHandle);
            const int imageSize = esp_https_ota_get_image_size(otaHandle);
            if (imageSize > 0) {
                const uint8_t pct = static_cast<uint8_t>(std::clamp((totalBytes * 80) / imageSize, 0, 80));
                notifyProgress(OtaStatus::InProgress, pct, "Flashing application firmware...");
            }
            vTaskDelay(pdMS_TO_TICKS(5));
        } else if (ret == ESP_OK) {
            ESP_LOGI(TAG, "App firmware written successfully");
            break;
        } else {
            esp_https_ota_abort(otaHandle);
            return ret;
        }
    }

    return esp_https_ota_finish(otaHandle);
}

esp_err_t OtaService::performFsOta(const char* fsUrl) {
    const esp_partition_t* part =
        esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, FS_PARTITION_LABEL);
    if (!part)
        return ESP_ERR_NOT_FOUND;

    esp_http_client_config_t httpCfg{};
    httpCfg.url = fsUrl;
    httpCfg.timeout_ms = 20000;
    httpCfg.max_redirection_count = 5;
    httpCfg.crt_bundle_attach = esp_crt_bundle_attach;

    esp_http_client_handle_t client = esp_http_client_init(&httpCfg);
    if (!client)
        return ESP_FAIL;

    if (esp_http_client_open(client, 0) != ESP_OK) {
        esp_http_client_cleanup(client);
        return ESP_FAIL;
    }

    const int contentLen = esp_http_client_fetch_headers(client);
    if (contentLen <= 0 || contentLen > static_cast<int>(part->size)) {
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return ESP_ERR_INVALID_SIZE;
    }

    notifyProgress(OtaStatus::InProgress, 82, "Erasing WebUI storage...");
    ESP_RETURN_ON_ERROR(esp_partition_erase_range(part, 0, part->size), TAG, "Partition erase failed");

    char streamBuffer[STREAM_BUFFER_SIZE];
    int bytesRead = 0;
    int totalWritten = 0;

    while ((bytesRead = esp_http_client_read(client, streamBuffer, sizeof(streamBuffer))) > 0) {
        if (esp_partition_write(part, totalWritten, streamBuffer, bytesRead) != ESP_OK) {
            esp_http_client_close(client);
            esp_http_client_cleanup(client);
            return ESP_FAIL;
        }
        totalWritten += bytesRead;
        const uint8_t pct = static_cast<uint8_t>(80 + (totalWritten * 18) / contentLen);
        notifyProgress(OtaStatus::InProgress, pct, "Flashing WebUI assets...");
        vTaskDelay(pdMS_TO_TICKS(5));
    }

    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    return ESP_OK;
}

esp_err_t OtaService::checkForUpdateAsync(const char* manifestUrl) {
    if (m_isUpdating.load())
        return ESP_ERR_INVALID_STATE;
    if (!manifestUrl || strlen(manifestUrl) == 0)
        return ESP_ERR_INVALID_ARG;

    m_targetUrl = manifestUrl;
    xTaskCreate(versionCheckTaskRunner, "ota_ver_chk", 5120, this, 3, nullptr);
    return ESP_OK;
}

void OtaService::versionCheckTaskRunner(void* arg) {
    auto* self = static_cast<OtaService*>(arg);
    self->performVersionCheck(self->m_targetUrl.c_str());
    vTaskDelete(nullptr);
}

void OtaService::performVersionCheck(const char* url) {
    ESP_LOGI(TAG, "Checking for updates at: %s", url);
    m_status.store(OtaStatus::CheckingVersion);

    esp_http_client_config_t httpCfg{};
    httpCfg.url = url;
    httpCfg.timeout_ms = 12000;
    httpCfg.max_redirection_count = 5;
    httpCfg.crt_bundle_attach = esp_crt_bundle_attach;

    esp_http_client_handle_t client = esp_http_client_init(&httpCfg);
    if (!client) {
        m_status.store(OtaStatus::Idle);
        return;
    }

    esp_http_client_set_header(client, "User-Agent", "ESP32-DaliBridge");
    esp_http_client_set_header(client, "Accept", "application/json");

    if (esp_http_client_open(client, 0) != ESP_OK) {
        esp_http_client_cleanup(client);
        m_status.store(OtaStatus::Idle);
        return;
    }

    esp_http_client_fetch_headers(client);

    char responseBuffer[2048];
    const int readBytes = esp_http_client_read_response(client, responseBuffer, sizeof(responseBuffer) - 1);
    esp_http_client_close(client);
    esp_http_client_cleanup(client);

    m_status.store(OtaStatus::Idle);

    if (readBytes <= 0)
        return;
    responseBuffer[readBytes] = '\0';

    JsonDocument doc;
    if (deserializeJson(doc, responseBuffer) != DeserializationError::Ok) {
        ESP_LOGW(TAG, "Failed to parse release JSON");
        return;
    }

    OtaVersionInfo info{};
    info.lastCheckTsSec = esp_timer_get_time() / 1000000;

    if (doc["tag_name"].is<const char*>()) {
        std::string_view tag(doc["tag_name"].as<const char*>());
        if (tag.starts_with("v") || tag.starts_with("V"))
            tag.remove_prefix(1);
        info.latestVersion = tag.data();
    } else if (doc["version"].is<const char*>()) {
        info.latestVersion = doc["version"].as<const char*>();
    }

    if (doc["html_url"].is<const char*>()) {
        info.releaseUrl = doc["html_url"].as<const char*>();
    } else if (doc["release_url"].is<const char*>()) {
        info.releaseUrl = doc["release_url"].as<const char*>();
    }

    if (doc["firmware_url"].is<const char*>()) {
        info.firmwareUrl = doc["firmware_url"].as<const char*>();
    }

    if (!info.latestVersion.empty()) {
        info.updateAvailable = (info.latestVersion != DALIMQTT_VERSION);

        {
            std::lock_guard<std::mutex> lock(m_infoMutex);
            m_versionInfo = info;
        }

        ESP_LOGI(TAG, "Version Check: Installed=%s, Latest=%s, UpdateAvailable=%s", DALIMQTT_VERSION,
                 info.latestVersion.c_str(), info.updateAvailable ? "YES" : "NO");

        if (m_versionCb) {
            m_versionCb(info, m_versionCtx);
        }
    }
}

} // namespace daliMQTT