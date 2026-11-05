// Copyright (c) 2026 Alice-Trade Inc.
// SPDX-License-Identifier: GPL-3.0-or-later

#include "system/OtaService.hxx"
#include <algorithm>
#include <array>
#include <charconv>
#include <cstring>
#include <esp_crt_bundle.h>
#include <esp_http_client.h>
#include <esp_https_ota.h>
#include <esp_log.h>
#include <esp_system.h>
#include <esp_timer.h>
#include <string_view>

namespace daliMQTT {

static constexpr char TAG[] = "OtaService";

namespace {
constexpr std::array<uint32_t, 3> parseSemver(std::string_view ver) noexcept {
    if (ver.starts_with("v") || ver.starts_with("V")) {
        ver.remove_prefix(1);
    }
    std::array<uint32_t, 3> parts{0, 0, 0};
    for (size_t i = 0; i < 3 && !ver.empty(); ++i) {
        uint32_t val = 0;
        const auto [ptr, ec] = std::from_chars(ver.data(), ver.data() + ver.size(), val);
        if (ec == std::errc{}) {
            parts[i] = val;
        }
        const size_t dot = ver.find('.');
        if (dot == std::string_view::npos) {
            break;
        }
        ver.remove_prefix(dot + 1);
    }
    return parts;
}

constexpr bool isNewerVersion(std::string_view latest, std::string_view installed) noexcept {
    return parseSemver(latest) > parseSemver(installed);
}

struct EspHttpStreamReader {
    esp_http_client_handle_t client;

    int read() {
        char c = 0;
        return (esp_http_client_read(client, &c, 1) == 1) ? static_cast<unsigned char>(c) : -1;
    }

    size_t readBytes(char* buffer, size_t length) {
        const int r = esp_http_client_read(client, buffer, static_cast<int>(length));
        return (r > 0) ? static_cast<size_t>(r) : 0;
    }
};
} // namespace

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

    m_targetUrl.clear();
    if (!url || strlen(url) == 0) {
        std::lock_guard<std::mutex> lock(m_infoMutex);
        m_targetUrl = m_versionInfo.firmwareUrl;
    } else {
        m_targetUrl = url;
    }

    if (m_targetUrl.empty()) {
        m_isUpdating.store(false);
        ESP_LOGE(TAG, "Cannot start update: target URL is empty");
        return ESP_ERR_INVALID_ARG;
    }

    m_status.store(OtaStatus::InProgress);

    const BaseType_t res = xTaskCreate(otaTaskRunner, "ota_worker", 10240, this, 4, &m_taskHandle);
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
        m_taskHandle = nullptr;
        m_isUpdating.store(false);
        vTaskDelete(nullptr);
    }

    notifyProgress(OtaStatus::Success, 100, "Update successful. Rebooting...");
    m_taskHandle = nullptr;
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

    uint8_t lastNotifiedPct = 0;
    while (true) {
        const esp_err_t ret = esp_https_ota_perform(otaHandle);
        if (ret == ESP_ERR_HTTPS_OTA_IN_PROGRESS) {
            const int totalBytes = esp_https_ota_get_image_len_read(otaHandle);
            const int imageSize = esp_https_ota_get_image_size(otaHandle);
            if (imageSize > 0) {
                const uint8_t pct = static_cast<uint8_t>(
                    std::clamp<int64_t>((static_cast<int64_t>(totalBytes) * 100) / imageSize, 0, 100));
                if (pct >= lastNotifiedPct + 5 || (pct == 100 && lastNotifiedPct != 100)) {
                    lastNotifiedPct = pct;
                    notifyProgress(OtaStatus::InProgress, pct, "Flashing application firmware...");
                }
            }
            vTaskDelay(1);
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

esp_err_t OtaService::checkForUpdateAsync(const char* manifestUrl, const bool autoInstall, const bool forceInstall) {
    if (!manifestUrl || strlen(manifestUrl) == 0)
        return ESP_ERR_INVALID_ARG;
    if (m_isUpdating.load() || m_isCheckingVersion.exchange(true))
        return ESP_ERR_INVALID_STATE;

    m_autoInstallAfterCheck.store(autoInstall, std::memory_order_relaxed);
    m_forceInstallAfterCheck.store(forceInstall, std::memory_order_relaxed);
    {
        std::lock_guard<std::mutex> lock(m_infoMutex);
        m_manifestUrl = manifestUrl;
    }

    if (xTaskCreate(versionCheckTaskRunner, "ota_ver_chk", 10240, this, 3, nullptr) != pdPASS) {
        m_isCheckingVersion.store(false);
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

void OtaService::versionCheckTaskRunner(void* arg) {
    auto* self = static_cast<OtaService*>(arg);
    etl::string<256> urlCopy;
    {
        std::lock_guard<std::mutex> lock(self->m_infoMutex);
        urlCopy = self->m_manifestUrl;
    }
    const bool autoInstall = self->m_autoInstallAfterCheck.load(std::memory_order_relaxed);
    const bool forceInstall = self->m_forceInstallAfterCheck.load(std::memory_order_relaxed);

    self->performVersionCheck(urlCopy.c_str(), autoInstall, forceInstall);
    self->m_isCheckingVersion.store(false);
    vTaskDelete(nullptr);
}

bool OtaService::parseAndStoreManifest(const JsonDocument& doc, OtaVersionInfo& info) {
    info = {};
    info.lastCheckTsSec = esp_timer_get_time() / 1'000'000;

    if (doc["tag_name"].is<const char*>()) {
        std::string_view tag(doc["tag_name"].as<const char*>());
        if (tag.starts_with("v") || tag.starts_with("V"))
            tag.remove_prefix(1);
        info.latestVersion.assign(tag.data(), tag.size());
    } else if (doc["version"].is<const char*>()) {
        std::string_view ver(doc["version"].as<const char*>());
        if (ver.starts_with("v") || ver.starts_with("V"))
            ver.remove_prefix(1);
        info.latestVersion.assign(ver.data(), ver.size());
    }

    if (doc["html_url"].is<const char*>()) {
        info.releaseUrl = doc["html_url"].as<const char*>();
    } else if (doc["release_url"].is<const char*>()) {
        info.releaseUrl = doc["release_url"].as<const char*>();
    }

    if (doc["firmware_url"].is<const char*>()) {
        info.firmwareUrl = doc["firmware_url"].as<const char*>();
    } else if (doc["url"].is<const char*>()) {
        info.firmwareUrl = doc["url"].as<const char*>();
    } else if (doc["assets"].is<JsonArrayConst>()) {
        for (JsonObjectConst asset : doc["assets"].as<JsonArrayConst>()) {
            if (asset["browser_download_url"].is<const char*>()) {
                auto dlUrl = asset["browser_download_url"].as<const char*>();
                const std::string_view urlSv(dlUrl);
                if (urlSv.ends_with(".bin")) {
#if defined(CONFIG_IDF_TARGET)
                    if (urlSv.find(CONFIG_IDF_TARGET) != std::string_view::npos) {
                        info.firmwareUrl = dlUrl;
                        break;
                    }
#endif
                    if (info.firmwareUrl.empty()) {
                        info.firmwareUrl = dlUrl;
                    }
                } else if (info.firmwareUrl.empty()) {
                    info.firmwareUrl = dlUrl;
                }
            }
        }
    }

    if (info.latestVersion.empty() && info.firmwareUrl.empty()) {
        return false;
    }

    if (info.latestVersion.empty()) {
        info.latestVersion = DALIMQTT_VERSION;
    }

    info.updateAvailable = isNewerVersion(info.latestVersion.c_str(), DALIMQTT_VERSION);

    {
        std::lock_guard<std::mutex> lock(m_infoMutex);
        m_versionInfo = info;
    }

    ESP_LOGI(TAG, "Manifest parsed: Installed=%s, Latest=%s, UpdateAvailable=%s, URL=%s", DALIMQTT_VERSION,
             info.latestVersion.c_str(), info.updateAvailable ? "YES" : "NO", info.firmwareUrl.c_str());

    if (m_versionCb) {
        m_versionCb(info, m_versionCtx);
    }
    return true;
}

esp_err_t OtaService::applyInlineManifest(const JsonDocument& doc, const bool autoInstall, const bool forceInstall) {
    if (m_isUpdating.load()) {
        return ESP_ERR_INVALID_STATE;
    }

    OtaVersionInfo info{};
    if (!parseAndStoreManifest(doc, info)) {
        ESP_LOGW(TAG, "Inline manifest is missing version or firmware_url");
        return ESP_ERR_INVALID_ARG;
    }

    if (autoInstall) {
        if ((info.updateAvailable || forceInstall) && !info.firmwareUrl.empty()) {
            return startUpdate(info.firmwareUrl.c_str());
        }
        ESP_LOGI(TAG, "Inline manifest checked: firmware is already up to date (use force=true to override)");
    }
    return ESP_OK;
}

void OtaService::performVersionCheck(const char* url, const bool autoInstall, const bool forceInstall) {
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

    const int64_t contentLen = esp_http_client_fetch_headers(client);
    const int statusCode = esp_http_client_get_status_code(client);
    if (statusCode != 200 || contentLen == 0) {
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        m_status.store(OtaStatus::Idle);
        return;
    }

    JsonDocument filter;
    filter["tag_name"] = true;
    filter["version"] = true;
    filter["html_url"] = true;
    filter["release_url"] = true;
    filter["firmware_url"] = true;
    filter["url"] = true;
    filter["assets"][0]["browser_download_url"] = true;

    JsonDocument doc;
    EspHttpStreamReader reader{client};
    const DeserializationError err = deserializeJson(doc, reader, DeserializationOption::Filter(filter));
    esp_http_client_close(client);
    esp_http_client_cleanup(client);

    m_status.store(OtaStatus::Idle);

    if (err != DeserializationError::Ok) {
        ESP_LOGW(TAG, "Failed to parse release JSON");
        return;
    }

    OtaVersionInfo info{};
    if (parseAndStoreManifest(doc, info) && autoInstall) {
        if ((info.updateAvailable || forceInstall) && !info.firmwareUrl.empty()) {
            startUpdate(info.firmwareUrl.c_str());
        } else {
            ESP_LOGI(TAG, "Manifest check complete: no newer version to auto-install");
        }
    }
}
} // namespace daliMQTT