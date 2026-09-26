// Copyright (c) 2026 Alice-Trade Inc.
// SPDX-License-Identifier: GPL-3.0-or-later

#include "system/OtaService.hxx"
#include <ArduinoJson.h>
#include <algorithm>
#include <cstring>
#include <esp_crt_bundle.h>
#include <esp_http_client.h>
#include <esp_https_ota.h>
#include <esp_ota_ops.h>
#include <esp_log.h>
#include <esp_partition.h>
#include <esp_system.h>

namespace daliMQTT {

static constexpr char TAG[] = "OtaService";
static constexpr size_t SECTOR_SIZE = 4096;
static constexpr size_t STREAM_BUFFER_SIZE = SECTOR_SIZE;

OtaService::OtaService() = default;

OtaService::~OtaService() {
    if (m_taskHandle) {
        vTaskDelete(m_taskHandle);
    }
}

esp_err_t OtaService::startUpdate(const char* url) {
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

esp_err_t OtaService::processStreamUpdate(OtaStreamReaderFn readFn, void* userCtx, size_t totalLen) {
    if (m_isUpdating.exchange(true)) {
        ESP_LOGW(TAG, "OTA Update already in progress");
        return ESP_ERR_INVALID_STATE;
    }

    if (!readFn || totalLen == 0) {
        m_isUpdating.store(false);
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "Starting firmware direct flash (%zu bytes)...", totalLen);
    notifyProgress(OtaStatus::InProgress, 0, "Inspecting firmware header...");

    alignas(4) char chunkBuf[STREAM_BUFFER_SIZE];
    int received = 0;

    const int firstRead = readFn(userCtx, chunkBuf, std::min(sizeof(chunkBuf), totalLen));
    if (firstRead <= 0) {
        m_isUpdating.store(false);
        return ESP_FAIL;
    }
    received += firstRead;

    if (static_cast<uint8_t>(chunkBuf[0]) != 0xE9) {
        ESP_LOGE(TAG, "Aborted: Not an ESP32 application binary (magic != 0xE9)!");
        m_isUpdating.store(false);
        notifyProgress(OtaStatus::Failed, 0, "Invalid binary magic");
        return ESP_ERR_INVALID_ARG;
    }

    const esp_partition_t* updatePart = esp_ota_get_next_update_partition(nullptr);
    if (!updatePart || totalLen > updatePart->size) {
        m_isUpdating.store(false);
        return ESP_ERR_NO_MEM;
    }

    esp_ota_handle_t otaHandle = 0;
    esp_err_t ret = esp_ota_begin(updatePart, totalLen, &otaHandle);
    if (ret != ESP_OK) {
        m_isUpdating.store(false);
        return ret;
    }

    ret = esp_ota_write(otaHandle, chunkBuf, firstRead);
    while (received < static_cast<int>(totalLen) && ret == ESP_OK) {
        const int r = readFn(userCtx, chunkBuf, std::min(sizeof(chunkBuf), totalLen - received));
        if (r <= 0) {
            ret = ESP_FAIL;
            break;
        }
        ret = esp_ota_write(otaHandle, chunkBuf, r);
        received += r;
        const uint8_t pct = static_cast<uint8_t>((received * 100) / totalLen);
        notifyProgress(OtaStatus::InProgress, pct, "Writing firmware...");
    }

    if (ret == ESP_OK && received == static_cast<int>(totalLen)) {
        ret = esp_ota_end(otaHandle);
        if (ret == ESP_OK) {
            ret = esp_ota_set_boot_partition(updatePart);
        }
    } else {
        esp_ota_abort(otaHandle);
        ret = ESP_ERR_IMAGE_INVALID;
    }

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Flash write aborted or failed!");
        m_isUpdating.store(false);
        notifyProgress(OtaStatus::Failed, 0, "Flash write failed");
        return ret;
    }

    notifyProgress(OtaStatus::Success, 100, "Update successful. Rebooting...");
    return ESP_OK;
}
} // namespace daliMQTT