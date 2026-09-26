// Copyright (c) 2026 Alice-Trade Inc.
// SPDX-License-Identifier: GPL-3.0-or-later

#include "system/ConfigJson.hxx"
#include <cstring>

namespace daliMQTT {
namespace {

template <size_t N>
bool assignStringSafe(etl::string<N>& dest, const char* src, const char* fieldKey, daliMQTT::ConfigApplyResult& res,
                      const bool triggersReboot = true) {
    if (!src)
        return true;

    const size_t len = strlen(src);
    if (len > dest.max_size()) {
        res.success = false;
        res.errorMessage = fieldKey;
        ESP_LOGE("ConfigJson", "Payload field '%s' length (%zu) exceeds capacity (%zu)", fieldKey, len,
                 dest.max_size());
        return false;
    }

    if (dest != src) {
        dest = src;
        if (triggersReboot) {
            res.requiresReboot = true;
        }
    }
    return true;
}

} // anonymous namespace

void ConfigJson::serialize(const ConfigStructure& cfg, JsonDocument& doc, const bool maskSecrets) {
    doc["wifi_ssid"] = cfg.wifiSsid.c_str();
    doc["wifi_password"] = maskSecrets ? "***" : cfg.wifiPass.c_str();

    doc["mqtt_uri"] = cfg.mqttUri.c_str();
    doc["mqtt_user"] = cfg.mqttUser.c_str();
    doc["mqtt_pass"] = maskSecrets ? "***" : cfg.mqttPass.c_str();
    doc["mqtt_base_topic"] = cfg.mqttBaseTopic.c_str();
    doc["mqtt_ca_cert"] = cfg.mqttCaCert.empty() ? "" : (maskSecrets ? "***" : cfg.mqttCaCert.c_str());
    doc["client_id"] = cfg.clientId.c_str();

    doc["http_domain"] = cfg.httpDomain.c_str();
    doc["http_user"] = cfg.httpUser.c_str();
    doc["http_pass"] = maskSecrets ? "***" : cfg.httpPass.c_str();

    doc["dali_poll_interval_ms"] = cfg.daliPollIntervalMs;
    doc["telemetry_interval_sec"] = cfg.telemetryIntervalSec;
    doc["ota_check_interval_days"] = cfg.otaCheckIntervalDays;
    doc["ota_url"] = cfg.otaBaseUrl.c_str();

    doc["hass_discovery_enabled"] = cfg.hassDiscoveryEnabled;
    doc["hass_discovery_prefix"] = cfg.hassDiscoveryPrefix.c_str();
    doc["syslog_server"] = cfg.syslogServer.c_str();
    doc["syslog_enabled"] = cfg.syslogEnabled;

    JsonArray busesArr = doc["buses"].to<JsonArray>();
    for (const auto& b : cfg.buses) {
        JsonObject bObj = busesArr.add<JsonObject>();
        bObj["enabled"] = b.enabled;
        bObj["rx_pin"] = b.rxPin;
        bObj["tx_pin"] = b.txPin;
    }
}

ConfigApplyResult ConfigJson::apply(const JsonDocument& doc, ConfigStructure& target) {
    ConfigApplyResult res{};

    auto getString = [&](const char* key) -> const char* {
        return doc[key].is<const char*>() ? doc[key].as<const char*>() : nullptr;
    };

    if (!assignStringSafe(target.wifiSsid, getString("wifi_ssid"), "wifi_ssid", res))
        return res;

    if (const char* pass = getString("wifi_password")) {
        if (strcmp(pass, "***") != 0) {
            if (!assignStringSafe(target.wifiPass, pass, "wifi_password", res))
                return res;
        }
    }

    if (!assignStringSafe(target.mqttUri, getString("mqtt_uri"), "mqtt_uri", res))
        return res;
    if (!assignStringSafe(target.mqttUser, getString("mqtt_user"), "mqtt_user", res))
        return res;
    if (const char* mPass = getString("mqtt_pass")) {
        if (strcmp(mPass, "***") != 0) {
            if (!assignStringSafe(target.mqttPass, mPass, "mqtt_pass", res))
                return res;
        }
    }
    if (!assignStringSafe(target.mqttBaseTopic, getString("mqtt_base_topic"), "mqtt_base_topic", res))
        return res;
    if (!assignStringSafe(target.clientId, getString("client_id"), "client_id", res))
        return res;

    if (const char* cert = getString("mqtt_ca_cert")) {
        if (strcmp(cert, "***") != 0) {
            if (!assignStringSafe(target.mqttCaCert, cert, "mqtt_ca_cert", res))
                return res;
        }
    }

    if (const char* domain = getString("http_domain")) {
        const size_t len = strlen(domain);
        if (len <= target.httpDomain.max_size()) {
            target.httpDomain = domain;
        } else {
            res.success = false;
            res.errorMessage = "http_domain too long";
            return res;
        }
    }
    if (!assignStringSafe(target.httpUser, getString("http_user"), "http_user", res, false)) return res;

    if (const char* hPass = getString("http_pass")) {
        if (strcmp(hPass, "***") != 0) {
            if (!assignStringSafe(target.httpPass, hPass, "http_pass", res, false)) return res;
        }
    }

    if (!assignStringSafe(target.otaBaseUrl, getString("ota_url"), "ota_url", res, false)) return res;

    if (!assignStringSafe(target.syslogServer, getString("syslog_server"), "syslog_server", res, false)) return res;

    if (!assignStringSafe(target.hassDiscoveryPrefix, getString("hass_discovery_prefix"), "hass_discovery_prefix", res, false)) return res;


    if (doc["dali_poll_interval_ms"].is<uint32_t>()) {
        target.daliPollIntervalMs = doc["dali_poll_interval_ms"].as<uint32_t>();
    }
    if (doc["telemetry_interval_sec"].is<uint32_t>()) {
        target.telemetryIntervalSec = doc["telemetry_interval_sec"].as<uint32_t>();
    }
    if (doc["ota_check_interval_days"].is<uint8_t>()) {
        target.otaCheckIntervalDays = doc["ota_check_interval_days"].as<uint8_t>();
    }
    if (const char* ota = getString("ota_url")) {
        target.otaBaseUrl = ota;
    }
    if (const char* sysSrv = getString("syslog_server")) {
        target.syslogServer = sysSrv;
    }
    if (doc["syslog_enabled"].is<bool>()) {
        target.syslogEnabled = doc["syslog_enabled"].as<bool>();
    }
    if (const char* haPre = getString("hass_discovery_prefix")) {
        target.hassDiscoveryPrefix = haPre;
    }
    if (doc["hass_discovery_enabled"].is<bool>()) {
        const bool newHa = doc["hass_discovery_enabled"].as<bool>();
        if (target.hassDiscoveryEnabled != newHa) {
            target.hassDiscoveryEnabled = newHa;
            res.hassDiscoveryToggled = true;
        }
    }

    if (doc["buses"].is<JsonArrayConst>()) {
        size_t idx = 0;
        for (JsonObjectConst bObj : doc["buses"].as<JsonArrayConst>()) {
            if (idx >= target.buses.size())
                break;
            if (bObj["rx_pin"].is<int8_t>() && target.buses[idx].rxPin != bObj["rx_pin"].as<int8_t>()) {
                target.buses[idx].rxPin = bObj["rx_pin"].as<int8_t>();
                res.requiresReboot = true;
            }
            if (bObj["tx_pin"].is<int8_t>() && target.buses[idx].txPin != bObj["tx_pin"].as<int8_t>()) {
                target.buses[idx].txPin = bObj["tx_pin"].as<int8_t>();
                res.requiresReboot = true;
            }
            if (bObj["enabled"].is<bool>()) {
                target.buses[idx].enabled = bObj["enabled"].as<bool>();
            }
            idx++;
        }
    }

    return res;
}

} // namespace daliMQTT