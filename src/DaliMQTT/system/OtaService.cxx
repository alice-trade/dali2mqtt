// Copyright (c) 2026 Alice-Trade Inc.
// SPDX-License-Identifier: GPL-3.0-or-later

#include "system/OtaService.hxx"
#include <ArduinoJson.h>
#include <algorithm>
#include <cstring>
#include <esp_crt_bundle.h>
#include <esp_http_client.h>
#include <esp_https_ota.h>
#include <esp_littlefs.h>
#include <esp_ota_ops.h>
#include <esp_log.h>
#include <esp_partition.h>
#include <esp_system.h>

namespace daliMQTT {

static constexpr char TAG[] = "OtaService";
static constexpr char FS_PARTITION_LABEL[] = CONFIG_DALI2MQTT_WEBUI_SPIFFS_PARTITION_LABEL;
static constexpr size_t SECTOR_SIZE = 4096;
static constexpr size_t STREAM_BUFFER_SIZE = SECTOR_SIZE;

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
        bool fsUrlResolved = false;

        const size_t pos = fsUrl.find("firmware.bin");
        if (pos != etl::string<160>::npos) {
            fsUrl.replace(pos, 12, "web_storage.bin");
            fsUrlResolved = true;
        } else {
            const size_t lastSlash = fsUrl.rfind('/');
            if (lastSlash != etl::string<160>::npos) {
                fsUrl.resize(lastSlash + 1);
                fsUrl.append("web_storage.bin");
                fsUrlResolved = true;
            }
        }

        if (fsUrlResolved && fsUrl != m_targetUrl) {
            ESP_LOGI(TAG, "Attempting WebUI FS update from: %s", fsUrl.c_str());
            err = performFsOta(fsUrl.c_str());
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "WebUI LittleFS update failed: %s. ABORTING REBOOT to protect system state!",
                         esp_err_to_name(err));
                notifyProgress(OtaStatus::Failed, 0, "WebUI update failed");
                m_isUpdating.store(false);
                vTaskDelete(nullptr);
            }
        } else {
            ESP_LOGW(TAG,
                     "Could not resolve distinct web_storage URL. Skipping FS OTA to prevent partition corruption.");
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
    if (!part) {
        ESP_LOGE(TAG, "Partition '%s' not found in partition table", FS_PARTITION_LABEL);
        return ESP_ERR_NOT_FOUND;
    }

    esp_http_client_config_t httpCfg{};
    httpCfg.url = fsUrl;
    httpCfg.timeout_ms = 15000;
    httpCfg.keep_alive_enable = true;
    httpCfg.max_redirection_count = 5;
    httpCfg.crt_bundle_attach = esp_crt_bundle_attach;

    esp_http_client_handle_t client = esp_http_client_init(&httpCfg);
    if (!client) {
        return ESP_FAIL;
    }

    if (esp_http_client_open(client, 0) != ESP_OK) {
        ESP_LOGE(TAG, "Cannot connect to WebUI asset URL: %s", fsUrl);
        esp_http_client_cleanup(client);
        return ESP_FAIL;
    }

    const int contentLen = esp_http_client_fetch_headers(client);
    const int statusCode = esp_http_client_get_status_code(client);

    if (statusCode != 200) {
        ESP_LOGE(TAG, "HTTP pre-flight failed. HTTP Status: %d", statusCode);
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return ESP_ERR_HTTP_INVALID_TRANSPORT;
    }

    if (contentLen <= 0 || contentLen > static_cast<int>(part->size)) {
        ESP_LOGE(TAG, "Invalid Content-Length: %d (Partition capacity: %u)", contentLen, (unsigned)part->size);
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return ESP_ERR_INVALID_SIZE;
    }

    alignas(4) char firstSectorBuf[SECTOR_SIZE];
    int firstSectorRead = 0;
    while (firstSectorRead < static_cast<int>(SECTOR_SIZE) && firstSectorRead < contentLen) {
        const int r = esp_http_client_read(
            client, firstSectorBuf + firstSectorRead,
            std::min(SECTOR_SIZE - firstSectorRead, static_cast<size_t>(contentLen - firstSectorRead)));
        if (r <= 0)
            break;
        firstSectorRead += r;
    }

    if (firstSectorRead < 64) {
        ESP_LOGE(TAG, "Truncated payload on header read");
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return ESP_ERR_IMAGE_INVALID;
    }

    if (static_cast<uint8_t>(firstSectorBuf[0]) == 0xE9) {
        ESP_LOGE(TAG, "Aborted: URL points to an ESP32 application binary, not LittleFS image!");
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return ESP_ERR_INVALID_ARG;
    }

    if (firstSectorBuf[0] == '<' || memcmp(firstSectorBuf, "<!DOCTYPE", 9) == 0) {
        ESP_LOGE(TAG, "Aborted: Server returned HTML error page instead of binary image!");
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return ESP_ERR_INVALID_RESPONSE;
    }

    bool hasLittleFsMagic = false;
    for (size_t i = 0; i <= 56; ++i) {
        if (memcmp(firstSectorBuf + i, "littlefs", 8) == 0) {
            hasLittleFsMagic = true;
            break;
        }
    }

    if (!hasLittleFsMagic) {
        ESP_LOGE(TAG, "Aborted: First sector does not contain LittleFS superblock signature!");
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return ESP_ERR_INVALID_CRC;
    }

    ESP_LOGI(TAG, "Image validated successfully. Starting flash write process...");
    notifyProgress(OtaStatus::InProgress, 81, "Updating WebUI storage...");

    esp_vfs_littlefs_unregister(FS_PARTITION_LABEL);

    alignas(4) char streamBuffer[SECTOR_SIZE];
    int totalWritten = 0;
    int retryCount = 0;
    constexpr int MAX_RETRIES = 3;

    size_t erasedUpTo = 0;
    esp_err_t err = ESP_OK;
    while (erasedUpTo < static_cast<size_t>(firstSectorRead)) {
        err = esp_partition_erase_range(part, erasedUpTo, SECTOR_SIZE);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to erase sector at %zu: %s", erasedUpTo, esp_err_to_name(err));
            esp_http_client_close(client);
            esp_http_client_cleanup(client);
            return err;
        }
        erasedUpTo += SECTOR_SIZE;
    }

    err = esp_partition_write(part, 0, firstSectorBuf, firstSectorRead);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to write sector 0: %s", esp_err_to_name(err));
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return err;
    }
    totalWritten += firstSectorRead;

    while (totalWritten < contentLen) {
        const size_t bytesToRead = std::min(sizeof(streamBuffer), static_cast<size_t>(contentLen - totalWritten));
        const int bytesRead = esp_http_client_read(client, streamBuffer, bytesToRead);

        if (bytesRead > 0) {
            while (erasedUpTo < static_cast<size_t>(totalWritten + bytesRead)) {
                err = esp_partition_erase_range(part, erasedUpTo, SECTOR_SIZE);
                if (err != ESP_OK) {
                    ESP_LOGE(TAG, "Flash erase failed at offset %zu: %s", erasedUpTo, esp_err_to_name(err));
                    break;
                }
                erasedUpTo += SECTOR_SIZE;
            }
            if (err != ESP_OK) break;

            err = esp_partition_write(part, totalWritten, streamBuffer, bytesRead);
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "Flash write failed at offset %d: %s", totalWritten, esp_err_to_name(err));
                break;
            }
            totalWritten += bytesRead;
            const uint8_t pct = static_cast<uint8_t>(80 + (totalWritten * 19) / contentLen);
            notifyProgress(OtaStatus::InProgress, pct, "Flashing WebUI assets...");
        } else if (bytesRead == 0) {
            if (++retryCount > MAX_RETRIES) {
                ESP_LOGE(TAG, "HTTP connection lost, retry limit reached");
                break;
            }

            ESP_LOGW(TAG, "Connection interrupted at %d bytes. Reconnecting (attempt %d/%d)...", totalWritten,
                     retryCount, MAX_RETRIES);
            esp_http_client_close(client);

            char rangeHeader[32];
            snprintf(rangeHeader, sizeof(rangeHeader), "bytes=%d-", totalWritten);
            esp_http_client_set_header(client, "Range", rangeHeader);

            if (esp_http_client_open(client, 0) != ESP_OK) {
                vTaskDelay(pdMS_TO_TICKS(1000));
                continue;
            }
            esp_http_client_fetch_headers(client);
            const int resCode = esp_http_client_get_status_code(client);
            if (resCode != 206 && resCode != 200) {
                ESP_LOGE(TAG, "Server rejected resume request with status %d", resCode);
                break;
            }
        } else {
            break;
        }
    }

    esp_http_client_close(client);
    esp_http_client_cleanup(client);

    if (totalWritten != contentLen) {
        ESP_LOGE(TAG, "FS OTA Failed: written %d of %d bytes", totalWritten, contentLen);
        return ESP_ERR_IMAGE_INVALID;
    }

    esp_vfs_littlefs_conf_t conf = {
        .base_path = "/littlefs",
        .partition_label = FS_PARTITION_LABEL,
        .format_if_mount_failed = false,
        .dont_mount = false,
    };
    err = esp_vfs_littlefs_register(&conf);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "LittleFS mount test failed: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "LittleFS successfully updated and mounted (%d bytes)", totalWritten);
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

esp_err_t OtaService::processStreamUpdate(OtaStreamReaderFn readFn, void* userCtx, size_t totalLen) {
    if (m_isUpdating.exchange(true)) {
        ESP_LOGW(TAG, "OTA Update already in progress");
        return ESP_ERR_INVALID_STATE;
    }

    if (!readFn || totalLen == 0) {
        m_isUpdating.store(false);
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "Starting stream-based direct flash (%zu bytes)...", totalLen);
    notifyProgress(OtaStatus::InProgress, 0, "Inspecting image header...");

    alignas(4) char chunkBuf[STREAM_BUFFER_SIZE];
    int received = 0;

    const int firstRead = readFn(userCtx, chunkBuf, std::min(sizeof(chunkBuf), totalLen));
    if (firstRead <= 0) {
        m_isUpdating.store(false);
        notifyProgress(OtaStatus::Failed, 0, "Socket read error on header");
        return ESP_FAIL;
    }
    received += firstRead;

    const bool isAppFirmware = (static_cast<uint8_t>(chunkBuf[0]) == 0xE9);
    bool isLittleFs = false;
    for (size_t i = 0; i <= 56 && i < static_cast<size_t>(firstRead); ++i) {
        if (memcmp(chunkBuf + i, "littlefs", 8) == 0) {
            isLittleFs = true;
            break;
        }
    }

    if (!isAppFirmware && !isLittleFs) {
        ESP_LOGE(TAG, "Unknown image signature. Aborted without touching Flash.");
        m_isUpdating.store(false);
        notifyProgress(OtaStatus::Failed, 0, "Invalid binary signature");
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t ret = ESP_OK;

    if (isAppFirmware) {
        const esp_partition_t* updatePart = esp_ota_get_next_update_partition(nullptr);
        if (!updatePart || totalLen > updatePart->size) {
            m_isUpdating.store(false);
            return ESP_ERR_NO_MEM;
        }

        esp_ota_handle_t otaHandle = 0;
        ret = esp_ota_begin(updatePart, totalLen, &otaHandle);
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
    }

    if (ret != ESP_OK || received != static_cast<int>(totalLen)) {
        ESP_LOGE(TAG, "Streaming OTA Failed or truncated!");
        m_isUpdating.store(false);
        notifyProgress(OtaStatus::Failed, 0, "Flash write aborted");
        return ESP_FAIL;
    }

    notifyProgress(OtaStatus::Success, 100, "Update successful. Rebooting...");
    return ESP_OK;
}
} // namespace daliMQTT