// Copyright (c) 2026 Alice-Trade Inc.
// SPDX-License-Identifier: GPL-3.0-or-later

#include "webui/ApiHandlers.hxx"
#include "network/NetworkPlatform.hxx"
#include "dali/DaliDeviceRegistry.hxx"
#include "mqtt/MqttClient.hxx"
#include "system/ConfigStore.hxx"
#include "system/OtaService.hxx"
#include "utils/DaliLongAddrConversions.hxx"
#include "utils/NvsHandle.hxx"
#include "webui/ApiContext.hxx"
#include <ArduinoJson.h>
#include <atomic>
#include <esp_chip_info.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <mbedtls/base64.h>

namespace daliMQTT {

static constexpr char TAG[] = "WebApi";

namespace {
enum class DaliOperationStatus : uint8_t { Idle, Scanning, Initializing, RefreshingGroups };
}

static std::atomic<DaliOperationStatus> g_daliStatus{DaliOperationStatus::Idle};

static esp_err_t checkAuth(httpd_req_t* req, const ApiContext* ctx) {
    char authHdr[128];
    if (httpd_req_get_hdr_value_str(req, "Authorization", authHdr, sizeof(authHdr)) != ESP_OK) {
        httpd_resp_set_status(req, "401 Unauthorized");
        httpd_resp_set_hdr(req, "WWW-Authenticate", "Basic realm=\"DALI Bridge\"");
        httpd_resp_send(req, "Auth required", HTTPD_RESP_USE_STRLEN);
        return ESP_FAIL;
    }

    std::string_view authSv(authHdr);
    if (!authSv.starts_with("Basic "))
        return ESP_FAIL;
    authSv.remove_prefix(6);

    unsigned char decoded[128]{0};
    size_t decodedLen = 0;
    if (mbedtls_base64_decode(decoded, sizeof(decoded) - 1, &decodedLen,
                              reinterpret_cast<const unsigned char*>(authSv.data()), authSv.length()) != 0) {
        return ESP_FAIL;
    }

    decoded[decodedLen] = '\0';
    std::string_view creds(reinterpret_cast<char*>(decoded));
    const size_t colonPos = creds.find(':');
    if (colonPos == std::string_view::npos)
        return ESP_FAIL;

    const auto cfg = ctx->config.get();
    if (creds.substr(0, colonPos) == cfg->httpUser.c_str() && creds.substr(colonPos + 1) == cfg->httpPass.c_str()) {
        return ESP_OK;
    }

    httpd_resp_set_status(req, "401 Unauthorized");
    httpd_resp_send(req, "Invalid credentials", HTTPD_RESP_USE_STRLEN);
    return ESP_FAIL;
}

esp_err_t ApiHandlers::getConfig(httpd_req_t* req) {
    const auto* ctx = static_cast<ApiContext*>(req->user_ctx);
    if (checkAuth(req, ctx) != ESP_OK)
        return ESP_FAIL;

    const auto cfg = ctx->config.get();

    JsonDocument doc;
    doc["wifi_ssid"] = cfg->wifiSsid.c_str();
    doc["mqtt_uri"] = cfg->mqttUri.c_str();
    doc["mqtt_user"] = cfg->mqttUser.c_str();
    doc["mqtt_base_topic"] = cfg->mqttBaseTopic.c_str();
    doc["mqtt_ca_cert"] = cfg->mqttCaCert.empty() ? "" : "***";
    doc["client_id"] = cfg->clientId.c_str();
    doc["http_domain"] = cfg->httpDomain.c_str();
    doc["http_user"] = cfg->httpUser.c_str();
    doc["dali_poll_interval_ms"] = cfg->daliPollIntervalMs;
    doc["telemetry_interval_sec"] = cfg->telemetryIntervalSec;
    doc["ota_check_interval_days"] = cfg->otaCheckIntervalDays;
    doc["ota_url"] = cfg->otaBaseUrl.c_str();
    doc["hass_discovery_enabled"] = cfg->hassDiscoveryEnabled;
    doc["hass_discovery_prefix"]  = cfg->hassDiscoveryPrefix.c_str();
    doc["syslog_server"] = cfg->syslogServer.c_str();
    doc["syslog_enabled"] = cfg->syslogEnabled;

    const auto busesArr = doc["buses"].to<JsonArray>();
    for (const auto& b : cfg->buses) {
        auto bObj = busesArr.add<JsonObject>();
        bObj["enabled"] = b.enabled;
        bObj["rx_pin"] = b.rxPin;
        bObj["tx_pin"] = b.txPin;
    }

    doc["wifi_password"] = "***";
    doc["mqtt_pass"] = "***";
    doc["http_pass"] = "***";

    char buf[768];
    serializeJson(doc, buf, sizeof(buf));
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, buf, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

esp_err_t ApiHandlers::setConfig(httpd_req_t* req) {
    const auto* ctx = static_cast<ApiContext*>(req->user_ctx);
    if (checkAuth(req, ctx) != ESP_OK)
        return ESP_FAIL;

    if (req->content_len == 0 || req->content_len > 4096) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Payload too large or empty");
        return ESP_FAIL;
    }

    auto buf = std::make_unique<char[]>(req->content_len + 1);

    size_t received = 0;
    int timeouts = 0;

    while (received < req->content_len) {
        const int ret = httpd_req_recv(req, buf.get() + received, req->content_len - received);
        if (ret <= 0) {
            if (ret == HTTPD_SOCK_ERR_TIMEOUT && ++timeouts < 3) {
                continue;
            }
            httpd_resp_send_408(req);
            return ESP_FAIL;
        }
        received += ret;
    }
    buf[received] = '\0';

    JsonDocument doc;
    if (deserializeJson(doc, buf.get()) != DeserializationError::Ok) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_FAIL;
    }


    auto newCfg = *ctx->config.get();

    if (doc["wifi_ssid"].is<const char*>())
        newCfg.wifiSsid = doc["wifi_ssid"].as<const char*>();
    if (doc["wifi_password"].is<const char*>() && strcmp(doc["wifi_password"].as<const char*>(), "***") != 0) {
        newCfg.wifiPass = doc["wifi_password"].as<const char*>();
    }
    if (doc["mqtt_uri"].is<const char*>())
        newCfg.mqttUri = doc["mqtt_uri"].as<const char*>();
    if (doc["mqtt_user"].is<const char*>())
        newCfg.mqttUser = doc["mqtt_user"].as<const char*>();
    if (doc["mqtt_pass"].is<const char*>() && strcmp(doc["mqtt_pass"].as<const char*>(), "***") != 0) {
        newCfg.mqttPass = doc["mqtt_pass"].as<const char*>();
    }
    if (doc["mqtt_base_topic"].is<const char*>())
        newCfg.mqttBaseTopic = doc["mqtt_base_topic"].as<const char*>();
    if (doc["mqtt_ca_cert"].is<const char*>()) {
        const std::string_view cert = doc["mqtt_ca_cert"].as<const char*>();
        if (cert != "***") {
            if (cert.length() <= newCfg.mqttCaCert.max_size()) {
                newCfg.mqttCaCert = cert.data();
            } else {
                httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Certificate exceeds maximum size (2048)");
                return ESP_FAIL;
            }
        }
    }
    if (doc["http_domain"].is<const char*>())
        newCfg.httpDomain = doc["http_domain"].as<const char*>();
    if (doc["http_user"].is<const char*>())
        newCfg.httpUser = doc["http_user"].as<const char*>();
    if (doc["http_pass"].is<const char*>() && strcmp(doc["http_pass"].as<const char*>(), "***") != 0) {
        newCfg.httpPass = doc["http_pass"].as<const char*>();
    }
    if (doc["telemetry_interval_sec"].is<uint32_t>()) {
        newCfg.telemetryIntervalSec = doc["telemetry_interval_sec"].as<uint32_t>();
    }
    if (doc["ota_check_interval_days"].is<uint8_t>()) {
        newCfg.otaCheckIntervalDays = doc["ota_check_interval_days"].as<uint8_t>();
    }
    if (doc["ota_url"].is<const char*>())
        newCfg.otaBaseUrl = doc["ota_url"].as<const char*>();
    if (doc["syslog_server"].is<const char*>())
        newCfg.syslogServer = doc["syslog_server"].as<const char*>();
    if (doc["syslog_enabled"].is<bool>())
        newCfg.syslogEnabled = doc["syslog_enabled"].as<bool>();
    if (doc["hass_discovery_enabled"].is<bool>())
        newCfg.hassDiscoveryEnabled = doc["hass_discovery_enabled"].as<bool>();
    if (doc["hass_discovery_prefix"].is<const char*>()) {
        newCfg.hassDiscoveryPrefix = doc["hass_discovery_prefix"].as<const char*>();
    }
    if (doc["dali_poll_interval_ms"].is<uint32_t>())
        newCfg.daliPollIntervalMs = doc["dali_poll_interval_ms"].as<uint32_t>();

    ctx->config.save(newCfg);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, R"({"status":"ok","message":"Settings saved. Restarting device..."})", HTTPD_RESP_USE_STRLEN);

    vTaskDelay(pdMS_TO_TICKS(1000));
    esp_restart();
    return ESP_OK;
}

esp_err_t ApiHandlers::getInfo(httpd_req_t* req) {
    const auto* ctx = static_cast<ApiContext*>(req->user_ctx);
    if (checkAuth(req, ctx) != ESP_OK)
        return ESP_FAIL;

    esp_chip_info_t chipInfo{};
    esp_chip_info(&chipInfo);

    JsonDocument doc;
    doc["version"] = DALIMQTT_VERSION;
    doc["chip_model"] = (chipInfo.model == CHIP_ESP32C6) ? "ESP32-C6" : "ESP32-S3";
    doc["free_heap"] = esp_get_free_heap_size();
    doc["uptime_seconds"] = static_cast<double>(esp_timer_get_time() / 1'000'000);
    doc["ip"] = ctx->network.getIpAddress().c_str();
    doc["network_type"] = NetworkPlatform::getInterfaceName();
    doc["mqtt_status"] = ctx->mqttClient.isConnected() ? "Connected" : "Disconnected";
    doc["configured"] = ctx->config.isConfigured();

    const auto ota = ctx->ota.getVersionInfo();
    const auto otaObj = doc["ota"].to<JsonObject>();
    otaObj["installed_version"] = DALIMQTT_VERSION;
    otaObj["latest_version"] = ota.latestVersion.c_str();
    otaObj["update_available"] = ota.updateAvailable;
    otaObj["release_url"] = ota.releaseUrl.c_str();
    otaObj["is_updating"] = ctx->ota.isUpdating();

    char buf[512];
    serializeJson(doc, buf, sizeof(buf));
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, buf, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

esp_err_t ApiHandlers::getDaliDevices(httpd_req_t* req) {
    const auto* ctx = static_cast<ApiContext*>(req->user_ctx);
    if (checkAuth(req, ctx) != ESP_OK)
        return ESP_FAIL;

    const auto devices = ctx->daliRegistry.getDevicesSnapshot();

    JsonDocument doc;
    const auto arr = doc.to<JsonArray>();

    for (const auto& dev : devices) {
        auto obj = arr.add<JsonObject>();
        const auto& id = getIdentity(dev);
        const auto addrStr = utils::longAddressToString(id.longAddress);

        obj["long_address"] = addrStr.data();
        obj["driverId"] = id.internalAddress.bus();
        obj["short_address"] = id.internalAddress.shortAddr();
        obj["available"] = id.available;

        if (const auto* gear = etl::get_if<ControlGear>(&dev)) {
            obj["type"] = "gear";
            obj["level"] = gear->currentLevel;
            obj["dt"] = gear->deviceType.has_value() ? gear->deviceType.value() : 0;
            obj["lamp_failure"] = (gear->statusByte & 0x02) != 0;
            obj["min"] = gear->minLevel;
            obj["max"] = gear->maxLevel;
            obj["on_level"] = gear->powerOnLevel;
            obj["fail_level"] = gear->systemFailureLevel;
        } else {
            obj["type"] = "input";
        }
    }

    httpd_resp_set_type(req, "application/json");

    HttpChunkStream stream{.req = req};
    serializeJson(doc, stream);
    stream.flush();

    httpd_resp_send_chunk(req, nullptr, 0);
    return ESP_OK;
}

esp_err_t ApiHandlers::scanDaliBus(httpd_req_t* req) {
    auto* ctx = static_cast<ApiContext*>(req->user_ctx);
    if (checkAuth(req, ctx) != ESP_OK)
        return ESP_FAIL;

    if (g_daliStatus.load() != DaliOperationStatus::Idle) {
        httpd_resp_set_status(req, "409 Conflict");
        httpd_resp_send(req, "Operation in progress", HTTPD_RESP_USE_STRLEN);
        return ESP_OK;
    }

    g_daliStatus.store(DaliOperationStatus::Scanning);
    xTaskCreate(
        [](void* arg) {
            auto* c = static_cast<ApiContext*>(arg);
            c->daliRegistry.scanBus();
            c->daliRegistry.refreshGroupAssignmentsFromBus();
            g_daliStatus.store(DaliOperationStatus::Idle);
            vTaskDelete(nullptr);
        },
        "web_scan_task", 4096, ctx, 4, nullptr);

    httpd_resp_set_status(req, "202 Accepted");
    httpd_resp_send(req, R"({"status":"ok","message":"Scan initiated"})", HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

esp_err_t ApiHandlers::initializeDaliBus(httpd_req_t* req) {
    auto* ctx = static_cast<ApiContext*>(req->user_ctx);
    if (checkAuth(req, ctx) != ESP_OK)
        return ESP_FAIL;

    if (g_daliStatus.load() != DaliOperationStatus::Idle) {
        httpd_resp_set_status(req, "409 Conflict");
        httpd_resp_send(req, "Operation in progress", HTTPD_RESP_USE_STRLEN);
        return ESP_OK;
    }

    g_daliStatus.store(DaliOperationStatus::Initializing);
    xTaskCreate(
        [](void* arg) {
            const auto* c = static_cast<ApiContext*>(arg);
            c->daliRegistry.commissionNewDevices();
            c->daliRegistry.refreshGroupAssignmentsFromBus();
            g_daliStatus.store(DaliOperationStatus::Idle);
            vTaskDelete(nullptr);
        },
        "web_init_task", 4096, ctx, 4, nullptr);

    httpd_resp_set_status(req, "202 Accepted");
    httpd_resp_send(req, R"({"status":"ok","message":"Commissioning initiated"})", HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

esp_err_t ApiHandlers::getDaliStatus(httpd_req_t* req) {
    auto statusStr = "idle";
    switch (g_daliStatus.load()) {
    case DaliOperationStatus::Scanning:
        statusStr = "scanning";
        break;
    case DaliOperationStatus::Initializing:
        statusStr = "initializing";
        break;
    case DaliOperationStatus::RefreshingGroups:
        statusStr = "refreshing_groups";
        break;
    default:
        statusStr = "idle";
        break;
    }

    char buf[64];
    snprintf(buf, sizeof(buf), R"({"status":"%s"})", statusStr);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, buf, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

esp_err_t ApiHandlers::getDaliNames(httpd_req_t* req) {
    const auto* ctx = static_cast<ApiContext*>(req->user_ctx);
    if (checkAuth(req, ctx) != ESP_OK)
        return ESP_FAIL;

    NvsHandle nvs("dali_names", NVS_READONLY);
    char buf[1024] = "{}";
    if (nvs) {
        size_t len = sizeof(buf);
        nvs_get_str(nvs.get(), "names_json", buf, &len);
    }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, buf, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

esp_err_t ApiHandlers::setDaliNames(httpd_req_t* req) {
    const auto* ctx = static_cast<ApiContext*>(req->user_ctx);
    if (checkAuth(req, ctx) != ESP_OK)
        return ESP_FAIL;

    char buf[1024];
    const int ret = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (ret <= 0)
        return ESP_FAIL;
    buf[ret] = '\0';

    NvsHandle nvs("dali_names", NVS_READWRITE);
    if (nvs) {
        nvs_set_str(nvs.get(), "names_json", buf);
        nvs_commit(nvs.get());
    }

    httpd_resp_send(req, R"({"status":"ok"})", HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

esp_err_t ApiHandlers::getDaliGroups(httpd_req_t* req) {
    auto* ctx = static_cast<ApiContext*>(req->user_ctx);
    if (checkAuth(req, ctx) != ESP_OK)
        return ESP_FAIL;

    const auto assignments = ctx->daliRegistry.getGroupAssignments();

    JsonDocument doc;
    for (const auto& [longAddr, groups] : assignments) {
        const auto laStr = utils::longAddressToString(longAddr);
        JsonArray grpArr = doc[laStr.data()].to<JsonArray>();
        for (int i = 0; i < 16; ++i) {
            if (groups.test(i))
                grpArr.add(i);
        }
    }

    char buf[768];
    serializeJson(doc, buf, sizeof(buf));
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, buf, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

esp_err_t ApiHandlers::setDaliGroups(httpd_req_t* req) {
    auto* ctx = static_cast<ApiContext*>(req->user_ctx);
    if (checkAuth(req, ctx) != ESP_OK)
        return ESP_FAIL;

    char buf[1024];
    const int ret = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (ret <= 0)
        return ESP_FAIL;
    buf[ret] = '\0';

    JsonDocument doc;
    if (deserializeJson(doc, buf) == DeserializationError::Ok && doc.is<JsonObject>()) {
        for (JsonPair kv : doc.as<JsonObject>()) {
            auto laOpt = utils::stringToLongAddress(kv.key().c_str());
            if (!laOpt || !kv.value().is<JsonArray>())
                continue;

            for (uint8_t i = 0; i < 16; ++i) {
                ctx->daliRegistry.setDeviceGroupMembership(*laOpt, i, false);
            }

            for (JsonVariant g : kv.value().as<JsonArray>()) {
                if (g.is<uint8_t>()) {
                    ctx->daliRegistry.setDeviceGroupMembership(*laOpt, g.as<uint8_t>(), true);
                }
            }
        }
    }
    httpd_resp_send(req, R"({"status":"ok"})", HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

esp_err_t ApiHandlers::refreshDaliGroups(httpd_req_t* req) {
    auto* ctx = static_cast<ApiContext*>(req->user_ctx);
    if (checkAuth(req, ctx) != ESP_OK)
        return ESP_FAIL;

    g_daliStatus.store(DaliOperationStatus::RefreshingGroups);
    xTaskCreate(
        [](void* arg) {
            auto* c = static_cast<ApiContext*>(arg);
            c->daliRegistry.refreshGroupAssignmentsFromBus();
            g_daliStatus.store(DaliOperationStatus::Idle);
            vTaskDelete(nullptr);
        },
        "web_grp_task", 4096, ctx, 4, nullptr);

    httpd_resp_send(req, R"({"status":"ok"})", HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

esp_err_t ApiHandlers::getDaliScenes(httpd_req_t* req) {
    auto* ctx = static_cast<ApiContext*>(req->user_ctx);
    if (checkAuth(req, ctx) != ESP_OK)
        return ESP_FAIL;

    char queryBuf[32];
    uint8_t sceneId = 0;
    if (httpd_req_get_url_query_str(req, queryBuf, sizeof(queryBuf)) == ESP_OK) {
        char param[8];
        if (httpd_query_key_value(queryBuf, "id", param, sizeof(param)) == ESP_OK) {
            sceneId = static_cast<uint8_t>(atoi(param));
        }
    }

    const auto levels = ctx->daliRegistry.querySceneLevels(0, sceneId);

    JsonDocument doc;
    doc["scene_id"] = sceneId;
    auto lObj = doc["levels"].to<JsonObject>();
    for (uint8_t sa = 0; sa < 64; ++sa) {
        if (levels[sa] != 255) {
            auto laOpt = ctx->daliRegistry.getLongAddress(DaliInternalAddr(0, sa));
            if (laOpt)
                lObj[utils::longAddressToString(*laOpt).data()] = levels[sa];
        }
    }

    char buf[512];
    serializeJson(doc, buf, sizeof(buf));
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, buf, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

esp_err_t ApiHandlers::setDaliScenes(httpd_req_t* req) {
    auto* ctx = static_cast<ApiContext*>(req->user_ctx);
    if (checkAuth(req, ctx) != ESP_OK)
        return ESP_FAIL;

    char buf[1024];
    const int ret = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (ret <= 0)
        return ESP_FAIL;
    buf[ret] = '\0';

    JsonDocument doc;
    if (deserializeJson(doc, buf) == DeserializationError::Ok) {
        if (doc["scene_id"].is<uint8_t>() && doc["levels"].is<JsonObject>()) {
            const uint8_t sceneId = doc["scene_id"].as<uint8_t>();
            SceneLevels levels{};
            levels.fill(255);

            for (JsonPair kv : doc["levels"].as<JsonObject>()) {
                auto laOpt = utils::stringToLongAddress(kv.key().c_str());
                if (laOpt) {
                    auto intAddr = ctx->daliRegistry.getInternalAddress(*laOpt);
                    if (intAddr)
                        levels[intAddr->shortAddr()] = kv.value().as<uint8_t>();
                }
            }
            ctx->daliRegistry.saveSceneLevels(0, sceneId, levels);
        }
    }
    httpd_resp_send(req, R"({"status":"ok"})", HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

esp_err_t ApiHandlers::triggerOtaCheck(httpd_req_t* req) {
    auto* ctx = static_cast<ApiContext*>(req->user_ctx);
    if (checkAuth(req, ctx) != ESP_OK)
        return ESP_FAIL;
    const auto cfg = ctx->config.get();

    if (cfg->otaBaseUrl.empty()) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "OTA URL is not configured");
        return ESP_FAIL;
    }

    const esp_err_t err = ctx->ota.checkForUpdateAsync(cfg->otaBaseUrl.c_str());
    if (err == ESP_OK) {
        httpd_resp_send(req, R"({"status":"ok","message":"Update check initiated"})", HTTPD_RESP_USE_STRLEN);
    } else {
        httpd_resp_set_status(req, "409 Conflict");
        httpd_resp_send(req, "OTA check already running", HTTPD_RESP_USE_STRLEN);
    }
    return ESP_OK;
}

esp_err_t ApiHandlers::triggerOtaInstall(httpd_req_t* req) {
    auto* ctx = static_cast<ApiContext*>(req->user_ctx);
    if (checkAuth(req, ctx) != ESP_OK)
        return ESP_FAIL;

    char body[256];
    char customUrl[160] = "";
    const int ret = httpd_req_recv(req, body, sizeof(body) - 1);
    if (ret > 0) {
        body[ret] = '\0';
        JsonDocument doc;
        if (deserializeJson(doc, body) == DeserializationError::Ok && doc["url"].is<const char*>()) {
            strncpy(customUrl, doc["url"].as<const char*>(), sizeof(customUrl) - 1);
        }
    }

    const esp_err_t err = ctx->ota.startUpdate(strlen(customUrl) > 0 ? customUrl : nullptr, true);
    if (err == ESP_OK) {
        httpd_resp_send(req, R"({"status":"ok","message":"Installation started"})", HTTPD_RESP_USE_STRLEN);
    } else {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to start installation");
    }
    return ESP_OK;
}

} // namespace daliMQTT