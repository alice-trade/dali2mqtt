// Copyright (c) 2026 Alice-Trade Inc.
// SPDX-License-Identifier: GPL-3.0-or-later

#include "system/ConfigJson.hxx"
#include <cstring>

namespace daliMQTT {

void ConfigJson::serialize(const ConfigStructure& cfg, JsonDocument& doc, const bool maskSecrets) {
    doc["wifi_ssid"] = cfg.wifiSsid.c_str();
    doc["wifi_password"] = maskSecrets ? "***" : cfg.wifiPass.c_str();
    doc["wifi_pass"] = doc["wifi_password"];

    doc["mqtt_uri"] = cfg.mqttUri.c_str();
    doc["mqtt_user"] = cfg.mqttUser.c_str();
    doc["mqtt_pass"] = maskSecrets ? "***" : cfg.mqttPass.c_str();
    doc["mqtt_base_topic"] = cfg.mqttBaseTopic.c_str();
    doc["mqtt_base"] = cfg.mqttBaseTopic.c_str();
    doc["mqtt_ca_cert"] = cfg.mqttCaCert.empty() ? "" : (maskSecrets ? "***" : cfg.mqttCaCert.c_str());
    doc["client_id"] = cfg.clientId.c_str();

    doc["http_domain"] = cfg.httpDomain.c_str();
    doc["http_user"] = cfg.httpUser.c_str();
    doc["http_pass"] = maskSecrets ? "***" : cfg.httpPass.c_str();

    doc["dali_poll_interval_ms"] = cfg.daliPollIntervalMs;
    doc["dali_poll"] = cfg.daliPollIntervalMs;
    doc["telemetry_interval_sec"] = cfg.telemetryIntervalSec;
    doc["telemetry_sec"] = cfg.telemetryIntervalSec;

    doc["ota_check_interval_days"] = cfg.otaCheckIntervalDays;
    doc["ota_days"] = cfg.otaCheckIntervalDays;
    doc["ota_url"] = cfg.otaBaseUrl.c_str();
    doc["hass_discovery_enabled"] = cfg.hassDiscoveryEnabled;
    doc["hass_disc"] = cfg.hassDiscoveryEnabled;
    doc["hass_discovery_prefix"] = cfg.hassDiscoveryPrefix.c_str();
    doc["ha_prefix"] = cfg.hassDiscoveryPrefix.c_str();
    doc["syslog_server"] = cfg.syslogServer.c_str();
    doc["syslog_srv"] = cfg.syslogServer.c_str();
    doc["syslog_enabled"] = cfg.syslogEnabled;
    doc["syslog_en"] = cfg.syslogEnabled;

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

    auto getString = [&](const char* key1, const char* key2 = nullptr) -> const char* {
        if (doc[key1].is<const char*>()) return doc[key1].as<const char*>();
        if (key2 && doc[key2].is<const char*>()) return doc[key2].as<const char*>();
        return nullptr;
    };

    if (const char* ssid = getString("wifi_ssid")) {
        if (target.wifiSsid != ssid) {
            target.wifiSsid = ssid;
            res.requiresReboot = true;
        }
    }
    if (const char* pass = getString("wifi_password", "wifi_pass")) {
        if (strcmp(pass, "***") != 0) {
            target.wifiPass = pass;
            res.requiresReboot = true;
        }
    }

    if (const char* uri = getString("mqtt_uri")) {
        if (target.mqttUri != uri) {
            target.mqttUri = uri;
            res.requiresReboot = true;
        }
    }
    if (const char* user = getString("mqtt_user")) {
        if (target.mqttUser != user) {
            target.mqttUser = user;
            res.requiresReboot = true;
        }
    }
    if (const char* pass = getString("mqtt_pass")) {
        if (strcmp(pass, "***") != 0) {
            target.mqttPass = pass;
            res.requiresReboot = true;
        }
    }
    if (const char* base = getString("mqtt_base_topic", "mqtt_base")) {
        if (target.mqttBaseTopic != base) {
            target.mqttBaseTopic = base;
            res.requiresReboot = true;
        }
    }
    if (const char* cid = getString("client_id")) {
        if (target.clientId != cid) {
            target.clientId = cid;
            res.requiresReboot = true;
        }
    }
    if (const char* cert = getString("mqtt_ca_cert")) {
        if (strcmp(cert, "***") != 0) {
            if (strlen(cert) <= target.mqttCaCert.max_size()) {
                target.mqttCaCert = cert;
                res.requiresReboot = true;
            } else {
                res.success = false;
                res.errorMessage = "Certificate exceeds maximum size (2048)";
                return res;
            }
        }
    }

    if (const char* domain = getString("http_domain")) {
        target.httpDomain = domain;
    }
    if (const char* hUser = getString("http_user")) {
        target.httpUser = hUser;
    }
    if (const char* hPass = getString("http_pass")) {
        if (strcmp(hPass, "***") != 0) {
            target.httpPass = hPass;
        }
    }

    if (doc["dali_poll_interval_ms"].is<uint32_t>()) {
        target.daliPollIntervalMs = doc["dali_poll_interval_ms"].as<uint32_t>();
    } else if (doc["dali_poll"].is<uint32_t>()) {
        target.daliPollIntervalMs = doc["dali_poll"].as<uint32_t>();
    }

    if (doc["telemetry_interval_sec"].is<uint32_t>()) {
        target.telemetryIntervalSec = doc["telemetry_interval_sec"].as<uint32_t>();
    } else if (doc["telemetry_sec"].is<uint32_t>()) {
        target.telemetryIntervalSec = doc["telemetry_sec"].as<uint32_t>();
    }

    if (doc["ota_check_interval_days"].is<uint8_t>()) {
        target.otaCheckIntervalDays = doc["ota_check_interval_days"].as<uint8_t>();
    } else if (doc["ota_days"].is<uint8_t>()) {
        target.otaCheckIntervalDays = doc["ota_days"].as<uint8_t>();
    }

    if (const char* ota = getString("ota_url")) {
        target.otaBaseUrl = ota;
    }
    if (const char* sysSrv = getString("syslog_server", "syslog_srv")) {
        target.syslogServer = sysSrv;
    }
    if (doc["syslog_enabled"].is<bool>()) {
        target.syslogEnabled = doc["syslog_enabled"].as<bool>();
    } else if (doc["syslog_en"].is<bool>()) {
        target.syslogEnabled = doc["syslog_en"].as<bool>();
    }

    if (const char* haPre = getString("hass_discovery_prefix", "ha_prefix")) {
        target.hassDiscoveryPrefix = haPre;
    }

    bool newHaDisc = target.hassDiscoveryEnabled;
    if (doc["hass_discovery_enabled"].is<bool>()) {
        newHaDisc = doc["hass_discovery_enabled"].as<bool>();
    } else if (doc["hass_disc"].is<bool>()) {
        newHaDisc = doc["hass_disc"].as<bool>();
    }
    if (target.hassDiscoveryEnabled != newHaDisc) {
        target.hassDiscoveryEnabled = newHaDisc;
        res.hassDiscoveryToggled = true;
    }

    if (doc["buses"].is<JsonArrayConst>()) {
        size_t idx = 0;
        for (JsonObjectConst bObj : doc["buses"].as<JsonArrayConst>()) {
            if (idx >= target.buses.size()) break;
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