// Copyright (c) 2026 Alice-Trade Inc.
// SPDX-License-Identifier: GPL-3.0-or-later

#include "system/ConfigStore.hxx"

#include "network/NetworkPlatform.hxx"
#include "utils/NvsHandle.hxx"
#include <esp_littlefs.h>
#include <esp_log.h>
#include <esp_mac.h>
#include <nvs_flash.h>

namespace daliMQTT {

static constexpr char TAG[] = "ConfigStore";
static constexpr char NVS_NAMESPACE[] = CONFIG_DALI2MQTT_NVS_NAMESPACE;
static constexpr char FS_PARTITION_LABEL[] = CONFIG_DALI2MQTT_WEBUI_SPIFFS_PARTITION_LABEL;

esp_err_t ConfigStore::init() {
    if (m_initialized)
        return ESP_OK;

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS partition truncated/new version, erasing...");
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_RETURN_ON_ERROR(ret, TAG, "NVS Flash Init failed");

    ret = mountLittleFs();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "LittleFS mount warning: %s", esp_err_to_name(ret));
    }

    m_initialized = true;
    return ESP_OK;
}

esp_err_t ConfigStore::mountLittleFs() {
    esp_vfs_littlefs_conf_t conf = {
        .base_path = "/littlefs",
        .partition_label = FS_PARTITION_LABEL,
        .format_if_mount_failed = false,
        .dont_mount = false,
    };
    return esp_vfs_littlefs_register(&conf);
}

void ConfigStore::generateDefaultClientId(ConfigStructure& cfg) {
    if (cfg.clientId.empty()) {
        uint8_t mac[6]{0};
        esp_read_mac(mac, ESP_MAC_WIFI_STA);
        char cidBuf[32];
        snprintf(cidBuf, sizeof(cidBuf), "dali_%02x%02x%02x", mac[3], mac[4], mac[5]);
        cfg.clientId = cidBuf;
    }
}

esp_err_t ConfigStore::load() {
    std::lock_guard<std::mutex> lock(m_mutex);
    const NvsHandle nvs(NVS_NAMESPACE, NVS_READONLY);

    const auto cfg = std::make_shared<ConfigStructure>(makeDefaultConfig());

    if (nvs) {
        auto readStr = [&](const char* key, auto& dest) {
            size_t reqSize = 0;
            if (nvs_get_str(nvs.get(), key, nullptr, &reqSize) == ESP_OK && reqSize > 1) {
                char buf[128];
                if (reqSize <= sizeof(buf) && nvs_get_str(nvs.get(), key, buf, &reqSize) == ESP_OK) {
                    dest = buf;
                }
            }
        };

        readStr("wifi_ssid", cfg->wifiSsid);
        readStr("wifi_pass", cfg->wifiPass);
        readStr("mqtt_uri", cfg->mqttUri);
        readStr("mqtt_user", cfg->mqttUser);
        readStr("mqtt_pass", cfg->mqttPass);
        readStr("mqtt_base", cfg->mqttBaseTopic);
        readStr("client_id", cfg->clientId);
        readStr("http_domain", cfg->httpDomain);
        readStr("http_user", cfg->httpUser);
        readStr("http_pass", cfg->httpPass);
        readStr("ota_url", cfg->otaBaseUrl);
        readStr("syslog_srv", cfg->syslogServer);

        size_t certSize = 0;
        if (nvs_get_str(nvs.get(), "mqtt_cert", nullptr, &certSize) == ESP_OK && certSize > 1) {
            if (certSize <= cfg->mqttCaCert.max_size() + 1) {
                char certBuf[1536];
                if (nvs_get_str(nvs.get(), "mqtt_cert", certBuf, &certSize) == ESP_OK) {
                    cfg->mqttCaCert = certBuf;
                }
            }
        }

        uint8_t flagU8 = 0;
        if (nvs_get_u8(nvs.get(), "configured", &flagU8) == ESP_OK)
            cfg->configuredFlag = (flagU8 == 1);
        if (cfg->configuredFlag && (!NetworkPlatform::isConfigured(*cfg) || !cfg->isMqttConfigured())) {
            ESP_LOGW(TAG, "Configured flag was TRUE, but Wi-Fi SSID or MQTT URI is empty! Invalidating state.");
            cfg->configuredFlag = false;
        }
        if (nvs_get_u8(nvs.get(), "hass_disc", &flagU8) == ESP_OK)
            cfg->hassDiscoveryEnabled = (flagU8 == 1);
        if (nvs_get_u8(nvs.get(), "syslog_en", &flagU8) == ESP_OK)
            cfg->syslogEnabled = (flagU8 == 1);

        uint32_t pollMs = 0;
        if (nvs_get_u32(nvs.get(), "dali_poll", &pollMs) == ESP_OK && pollMs >= 1000) {
            cfg->daliPollIntervalMs = pollMs;
        }

        nvs_get_u32(nvs.get(), "telem_sec", &cfg->telemetryIntervalSec);
        nvs_get_u8(nvs.get(), "ota_days", &cfg->otaCheckIntervalDays);
        nvs_get_i8(nvs.get(), "b0_rx", &cfg->buses[0].rxPin);
        nvs_get_i8(nvs.get(), "b0_tx", &cfg->buses[0].txPin);
    }

    generateDefaultClientId(*cfg);
    m_config = cfg;

    ESP_LOGI(TAG, "Configuration loaded (Mode: %s, Base Topic: '%s')",
             cfg->isFullyConfigured() ? "NORMAL" : "PROVISIONING", cfg->mqttBaseTopic.c_str());

    return ESP_OK;
}

esp_err_t ConfigStore::save(const ConfigStructure& newConfig) {
    std::lock_guard<std::mutex> lock(m_mutex);
    const NvsHandle nvs(NVS_NAMESPACE, NVS_READWRITE);
    if (!nvs)
        return ESP_FAIL;

    auto writeStr = [&](const char* key, const auto& src) {
        if (!src.empty())
            nvs_set_str(nvs.get(), key, src.c_str());
    };

    writeStr("wifi_ssid", newConfig.wifiSsid);
    writeStr("wifi_pass", newConfig.wifiPass);
    writeStr("mqtt_uri", newConfig.mqttUri);
    writeStr("mqtt_user", newConfig.mqttUser);
    writeStr("mqtt_pass", newConfig.mqttPass);
    writeStr("mqtt_base", newConfig.mqttBaseTopic);
    writeStr("mqtt_cert", newConfig.mqttCaCert);
    writeStr("client_id", newConfig.clientId);
    writeStr("http_domain", newConfig.httpDomain);
    writeStr("http_user", newConfig.httpUser);
    writeStr("http_pass", newConfig.httpPass);
    writeStr("ota_url", newConfig.otaBaseUrl);
    writeStr("syslog_srv", newConfig.syslogServer);

    const bool isComplete = NetworkPlatform::isConfigured(newConfig) && newConfig.isMqttConfigured();
    nvs_set_u8(nvs.get(), "configured", isComplete ? 1 : 0);
    nvs_set_u8(nvs.get(), "hass_disc", newConfig.hassDiscoveryEnabled ? 1 : 0);
    nvs_set_u8(nvs.get(), "syslog_en", newConfig.syslogEnabled ? 1 : 0);
    nvs_set_u8(nvs.get(), "ota_days", newConfig.otaCheckIntervalDays);

    nvs_set_u32(nvs.get(), "dali_poll", newConfig.daliPollIntervalMs);
    nvs_set_u32(nvs.get(), "telem_sec", newConfig.telemetryIntervalSec);

    nvs_set_i8(nvs.get(), "b0_rx", newConfig.buses[0].rxPin);
    nvs_set_i8(nvs.get(), "b0_tx", newConfig.buses[0].txPin);

    const esp_err_t err = nvs_commit(nvs.get());
    if (err == ESP_OK) {
        const auto updated = std::make_shared<ConfigStructure>(newConfig);
        updated->configuredFlag = true;
        m_config = updated;
        ESP_LOGI(TAG, "Configuration successfully saved to NVS");
    }
    return err;
}

esp_err_t ConfigStore::factoryReset() {
    std::lock_guard<std::mutex> lock(m_mutex);
    const NvsHandle nvs(NVS_NAMESPACE, NVS_READWRITE);
    if (!nvs)
        return ESP_FAIL;

    nvs_erase_all(nvs.get());
    const esp_err_t err = nvs_commit(nvs.get());

    {
        const NvsHandle namesNvs("dali_names", NVS_READWRITE);
        if (namesNvs) {
            nvs_erase_all(namesNvs.get());
            nvs_commit(namesNvs.get());
        }
    }

    if (err == ESP_OK) {
        m_config = std::make_shared<ConfigStructure>(makeDefaultConfig());
        ESP_LOGW(TAG, "NVS erased. Configuration reset to defaults.");
    }
    return err;
}

} // namespace daliMQTT