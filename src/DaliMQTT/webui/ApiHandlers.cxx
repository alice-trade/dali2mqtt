// Copyright (c) 2026 Alice-Trade Inc.
// SPDX-License-Identifier: GPL-3.0-or-later

#include "webui/ApiHandlers.hxx"
#include "dali/DaliDeviceRegistry.hxx"
#include "mqtt/MqttClient.hxx"
#include "network/NetworkPlatform.hxx"
#include "system/ConfigJson.hxx"
#include "system/ConfigStore.hxx"
#include "system/OtaService.hxx"
#include "utils/DaliLongAddrConversions.hxx"
#include "utils/JsonArenaAllocator.hxx"
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

static char* readRequestBodyToArena(httpd_req_t* req, ApiContext* ctx, const size_t maxLen, size_t* outReceived = nullptr) {
    if (req->content_len == 0 || req->content_len > maxLen) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Payload too large or empty");
        return nullptr;
    }

    auto* buf = static_cast<char*>(ctx->arena.allocate(req->content_len + 1));
    if (!buf) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Arena allocation failed");
        return nullptr;
    }

    size_t received = 0;
    while (received < req->content_len) {
        const int ret = httpd_req_recv(req, buf + received, req->content_len - received);
        if (ret <= 0) {
            if (ret == HTTPD_SOCK_ERR_TIMEOUT) {
                httpd_resp_send_408(req);
            }
            return nullptr;
        }
        received += ret;
    }
    buf[received] = '\0';

    if (outReceived) {
        *outReceived = received;
    }
    return buf;
}

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

    std::lock_guard<std::mutex> lock(ctx->arenaMutex);
    ctx->arena.reset();

    memory::JsonArenaAllocator allocator(ctx->arena);
    JsonDocument doc(&allocator);
    ConfigJson::serialize(*ctx->config.get(), doc, true);

    const size_t neededSize = measureJson(doc) + 1;
    auto* buf = static_cast<char*>(ctx->arena.allocate(neededSize));
    if (!buf) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Arena out of memory");
        return ESP_FAIL;
    }

    serializeJson(doc, buf, neededSize);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, buf, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

esp_err_t ApiHandlers::setConfig(httpd_req_t* req) {
    auto* ctx = static_cast<ApiContext*>(req->user_ctx);
    if (checkAuth(req, ctx) != ESP_OK)
        return ESP_FAIL;

    std::lock_guard<std::mutex> lock(ctx->arenaMutex);
    ctx->arena.reset();

    size_t received = 0;
    auto* buf = readRequestBodyToArena(req, ctx, 4096, &received);
    if (!buf) {
        return ESP_FAIL;
    }

    memory::JsonArenaAllocator allocator(ctx->arena);
    JsonDocument doc(&allocator);
    if (deserializeJson(doc, buf, received) != DeserializationError::Ok) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_FAIL;
    }

    auto newCfg = *ctx->config.get();
    auto result = ConfigJson::apply(doc, newCfg);

    if (!result.success) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, result.errorMessage);
        return ESP_FAIL;
    }

    ctx->config.save(newCfg);

    httpd_resp_set_type(req, "application/json");
    if (result.requiresReboot) {
        httpd_resp_send(req, R"({"status":"ok","message":"Settings saved. Restarting device..."})",
                        HTTPD_RESP_USE_STRLEN);
        xTaskCreate(
            [](void*) {
                vTaskDelay(pdMS_TO_TICKS(1000));
                esp_restart();
            },
            "web_reboot", 2048, nullptr, 5, nullptr);
    } else {
        httpd_resp_send(req, R"({"status":"ok","message":"Settings saved successfully."})", HTTPD_RESP_USE_STRLEN);
    }
    return ESP_OK;
}

esp_err_t ApiHandlers::getInfo(httpd_req_t* req) {
    const auto* ctx = static_cast<ApiContext*>(req->user_ctx);
    if (checkAuth(req, ctx) != ESP_OK)
        return ESP_FAIL;

    esp_chip_info_t chipInfo{};
    esp_chip_info(&chipInfo);

    std::lock_guard<std::mutex> lock(ctx->arenaMutex);
    ctx->arena.reset();

    memory::JsonArenaAllocator allocator(ctx->arena);
    JsonDocument doc(&allocator);

    doc["version"] = DALIMQTT_VERSION;
    doc["chip_model"] = (chipInfo.model == CHIP_ESP32C6)   ? "ESP32-C6"
                        : (chipInfo.model == CHIP_ESP32S3) ? "ESP32-S3"
                        : (chipInfo.model == CHIP_ESP32C3) ? "ESP32-C3"
                        : (chipInfo.model == CHIP_ESP32S2) ? "ESP32-S2"
                        : (chipInfo.model == CHIP_ESP32P4) ? "ESP32-P4"
                        : (chipInfo.model == CHIP_ESP32C5) ? "ESP32-C5"
                        : (chipInfo.model == CHIP_ESP32)   ? "ESP32"
                                                           : "?";
    doc["chip_cores"] = chipInfo.cores;
    doc["firmware_verbosity_level"] = CONFIG_LOG_DEFAULT_LEVEL;
    doc["free_heap"] = esp_get_free_heap_size();
    doc["uptime_seconds"] = static_cast<double>(esp_timer_get_time() / 1'000'000);
    doc["ip"] = ctx->network.getIpAddress().c_str();
    doc["network_type"] = NetworkPlatform::getInterfaceName();
    doc["mqtt_status"] = ctx->mqttClient.isConnected() ? "Connected" : "Disconnected";
    doc["net_status"] = ctx->network.isConnected() ? "Connected" : "Disconnected";
    doc["dali_status"] = (g_daliStatus.load() == DaliOperationStatus::Idle)           ? "Idle"
                         : (g_daliStatus.load() == DaliOperationStatus::Scanning)     ? "Scanning"
                         : (g_daliStatus.load() == DaliOperationStatus::Initializing) ? "Initializing"
                                                                                      : "Busy";
    doc["configured"] = ctx->config.isConfigured();

    const auto ota = ctx->ota.getVersionInfo();
    const auto otaObj = doc["ota"].to<JsonObject>();
    otaObj["installed_version"] = DALIMQTT_VERSION;
    otaObj["latest_version"] = ota.latestVersion.c_str();
    otaObj["update_available"] = ota.updateAvailable;
    otaObj["release_url"] = ota.releaseUrl.c_str();
    otaObj["is_updating"] = ctx->ota.isUpdating();

    const size_t neededSize = measureJson(doc) + 1;
    auto* buf = static_cast<char*>(ctx->arena.allocate(neededSize));
    if (!buf) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Arena out of memory");
        return ESP_FAIL;
    }

    serializeJson(doc, buf, neededSize);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, buf, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

esp_err_t ApiHandlers::getDaliDevices(httpd_req_t* req) {
    const auto* ctx = static_cast<const ApiContext*>(req->user_ctx);
    if (checkAuth(req, ctx) != ESP_OK)
        return ESP_FAIL;

    const auto devices = ctx->daliRegistry.getDevicesSnapshot();

    std::lock_guard<std::mutex> lock(ctx->arenaMutex);
    ctx->arena.reset();

    httpd_resp_set_type(req, "application/json");
    HttpChunkStream stream{.req = req};

    stream.write(reinterpret_cast<const uint8_t*>("["), 1);
    bool first = true;

    for (const auto& dev : devices) {
        ctx->arena.reset();
        memory::JsonArenaAllocator allocator(ctx->arena);
        JsonDocument itemDoc(&allocator);

        const auto& id = getIdentity(dev);
        const auto addrStr = utils::longAddressToString(id.longAddress);

        itemDoc["long_address"] = addrStr.data();
        itemDoc["driverId"] = id.internalAddress.bus();
        itemDoc["short_address"] = id.internalAddress.shortAddr();
        itemDoc["available"] = id.available;
        if (!id.gtin.empty())
            itemDoc["gtin"] = id.gtin.c_str();

        if (const auto* gear = etl::get_if<ControlGear>(&dev)) {
            itemDoc["type"] = "gear";
            itemDoc["level"] = gear->currentLevel;
            itemDoc["dt"] = gear->deviceType.value_or(0);
            itemDoc["lamp_failure"] = (gear->statusByte & 0x02) != 0;
            itemDoc["min"] = gear->minLevel;
            itemDoc["max"] = gear->maxLevel;
            itemDoc["on_level"] = gear->powerOnLevel;
            itemDoc["fail_level"] = gear->systemFailureLevel;
            if (gear->color.has_value()) {
                itemDoc["supports_tc"] = gear->color->supportsTc;
                itemDoc["supports_rgb"] = gear->color->supportsRgb;
            }
        } else {
            itemDoc["type"] = "input";
        }

        if (!first) {
            stream.write(reinterpret_cast<const uint8_t*>(","), 1);
        }
        first = false;

        serializeJson(itemDoc, stream);
    }

    stream.write(reinterpret_cast<const uint8_t*>("]"), 1);
    stream.flush();

    httpd_resp_send_chunk(req, nullptr, 0);
    return ESP_OK;
}

esp_err_t ApiHandlers::controlDaliDevice(httpd_req_t* req) {
    const auto* ctx = static_cast<ApiContext*>(req->user_ctx);
    if (checkAuth(req, ctx) != ESP_OK)
        return ESP_FAIL;

    char body[128];
    const int ret = httpd_req_recv(req, body, sizeof(body) - 1);
    if (ret <= 0)
        return ESP_FAIL;
    body[ret] = '\0';

    JsonDocument doc;
    if (deserializeJson(doc, body) != DeserializationError::Ok || !doc["address"].is<const char*>()) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid arguments");
        return ESP_FAIL;
    }

    auto laOpt = utils::stringToLongAddress(doc["address"].as<const char*>());
    if (!laOpt) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid address");
        return ESP_FAIL;
    }

    esp_err_t err = ESP_OK;
    if (doc["level"].is<int>()) {
        err = ctx->daliRegistry.setBrightness(*laOpt, static_cast<uint8_t>(std::clamp(doc["level"].as<int>(), 0, 254)));
    } else if (doc["state"].is<const char*>()) {
        const bool on = (strcmp(doc["state"].as<const char*>(), "ON") == 0);
        err = ctx->daliRegistry.setPower(*laOpt, on);
    }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, (err == ESP_OK) ? R"({"status":"ok"})" : R"({"status":"error"})", HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

esp_err_t ApiHandlers::scanDaliBus(httpd_req_t* req) {
    auto* ctx = static_cast<ApiContext*>(req->user_ctx);
    if (checkAuth(req, ctx) != ESP_OK)
        return ESP_FAIL;

    auto expected = DaliOperationStatus::Idle;
    if (!g_daliStatus.compare_exchange_strong(expected, DaliOperationStatus::Scanning, std::memory_order_acq_rel)) {
        httpd_resp_set_status(req, "409 Conflict");
        httpd_resp_send(req, "Operation in progress", HTTPD_RESP_USE_STRLEN);
        return ESP_OK;
    }

    const BaseType_t ret = xTaskCreate(
        [](void* arg) {
            auto* c = static_cast<ApiContext*>(arg);
            c->daliRegistry.scanBus();
            c->daliRegistry.refreshGroupAssignmentsFromBus();
            g_daliStatus.store(DaliOperationStatus::Idle, std::memory_order_release);
            vTaskDelete(nullptr);
        },
        "web_scan", 4096, ctx, 4, nullptr);

    if (ret != pdPASS) {
        g_daliStatus.store(DaliOperationStatus::Idle, std::memory_order_release);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Task create failed");
        return ESP_FAIL;
    }

    httpd_resp_set_status(req, "202 Accepted");
    httpd_resp_send(req, R"({"status":"ok","message":"Scan initiated"})", HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

esp_err_t ApiHandlers::initializeDaliBus(httpd_req_t* req) {
    auto* ctx = static_cast<ApiContext*>(req->user_ctx);
    if (checkAuth(req, ctx) != ESP_OK)
        return ESP_FAIL;

    auto expected = DaliOperationStatus::Idle;
    if (!g_daliStatus.compare_exchange_strong(expected, DaliOperationStatus::Initializing, std::memory_order_acq_rel)) {
        httpd_resp_set_status(req, "409 Conflict");
        httpd_resp_send(req, "Operation in progress", HTTPD_RESP_USE_STRLEN);
        return ESP_OK;
    }

    const BaseType_t ret = xTaskCreate(
        [](void* arg) {
            const auto* c = static_cast<ApiContext*>(arg);
            c->daliRegistry.commissionNewDevices();
            c->daliRegistry.refreshGroupAssignmentsFromBus();
            g_daliStatus.store(DaliOperationStatus::Idle, std::memory_order_release);
            vTaskDelete(nullptr);
        },
        "web_init", 4096, ctx, 4, nullptr);

    if (ret != pdPASS) {
        g_daliStatus.store(DaliOperationStatus::Idle, std::memory_order_release);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Task creation failed");
        return ESP_FAIL;
    }

    httpd_resp_set_status(req, "202 Accepted");
    httpd_resp_send(req, R"({"status":"ok","message":"Commissioning initiated"})", HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

esp_err_t ApiHandlers::initializeDaliInputs(httpd_req_t* req) {
    auto* ctx = static_cast<ApiContext*>(req->user_ctx);
    if (checkAuth(req, ctx) != ESP_OK)
        return ESP_FAIL;

    auto expected = DaliOperationStatus::Idle;
    if (!g_daliStatus.compare_exchange_strong(expected, DaliOperationStatus::Initializing, std::memory_order_acq_rel)) {
        httpd_resp_set_status(req, "409 Conflict");
        httpd_resp_send(req, "Operation in progress", HTTPD_RESP_USE_STRLEN);
        return ESP_OK;
    }

    const BaseType_t ret = xTaskCreate(
        [](void* arg) {
            auto* c = static_cast<ApiContext*>(arg);
            c->daliRegistry.commission24BitDevices();
            c->daliRegistry.refreshGroupAssignmentsFromBus();
            g_daliStatus.store(DaliOperationStatus::Idle, std::memory_order_release);
            vTaskDelete(nullptr);
        },
        "web_inp_init", 4096, ctx, 4, nullptr);

    if (ret != pdPASS) {
        g_daliStatus.store(DaliOperationStatus::Idle, std::memory_order_release);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Task creation failed");
        return ESP_FAIL;
    }

    httpd_resp_set_status(req, "202 Accepted");
    httpd_resp_send(req, R"({"status":"ok","message":"Input device commissioning initiated"})", HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

esp_err_t ApiHandlers::getDaliStatus(httpd_req_t* req) {
    const auto* ctx = static_cast<ApiContext*>(req->user_ctx);
    if (checkAuth(req, ctx) != ESP_OK)
        return ESP_FAIL;

    auto s = "idle";
    switch (g_daliStatus.load()) {
    case DaliOperationStatus::Scanning:
        s = "scanning";
        break;
    case DaliOperationStatus::Initializing:
        s = "initializing";
        break;
    case DaliOperationStatus::RefreshingGroups:
        s = "refreshing_groups";
        break;
    default:
        break;
    }

    char buf[64];
    snprintf(buf, sizeof(buf), R"({"status":"%s"})", s);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, buf, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

esp_err_t ApiHandlers::getDaliNames(httpd_req_t* req) {
    const auto* ctx = static_cast<ApiContext*>(req->user_ctx);
    if (checkAuth(req, ctx) != ESP_OK)
        return ESP_FAIL;

    std::lock_guard<std::mutex> lock(ctx->arenaMutex);
    ctx->arena.reset();

    constexpr size_t MAX_NAMES_SIZE = 3072;
    auto* buf = static_cast<char*>(ctx->arena.allocate(MAX_NAMES_SIZE));
    if (!buf) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Arena out of memory");
        return ESP_FAIL;
    }

    strncpy(buf, "{}", MAX_NAMES_SIZE);
    const NvsHandle nvs("dali_names", NVS_READONLY);
    if (nvs) {
        size_t len = MAX_NAMES_SIZE;
        nvs_get_str(nvs.get(), "names_json", buf, &len);
    }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, buf, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

esp_err_t ApiHandlers::setDaliNames(httpd_req_t* req) {
    auto* ctx = static_cast<ApiContext*>(req->user_ctx);
    if (checkAuth(req, ctx) != ESP_OK) {
        return ESP_FAIL;
    }

    std::lock_guard<std::mutex> lock(ctx->arenaMutex);
    ctx->arena.reset();

    size_t received = 0;
    auto* buf = readRequestBodyToArena(req, ctx, 3072, &received);
    if (!buf) {
        return ESP_FAIL;
    }

    memory::JsonArenaAllocator allocator(ctx->arena);
    JsonDocument doc(&allocator);

    const DeserializationError jsonErr = deserializeJson(doc, buf, received);
    if (jsonErr != DeserializationError::Ok || !doc.is<JsonObject>()) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON object");
        return ESP_FAIL;
    }

    const NvsHandle nvs("dali_names", NVS_READWRITE);
    if (!nvs) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "NVS open failed");
        return ESP_FAIL;
    }

    esp_err_t err = nvs_set_str(nvs.get(), "names_json", buf);
    if (err == ESP_OK) {
        err = nvs_commit(nvs.get());
    }

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to commit names to NVS: %s", esp_err_to_name(err));
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "NVS commit failed");
        return ESP_FAIL;
    }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, R"({"status":"ok","message":"Names saved successfully"})", HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

esp_err_t ApiHandlers::getDaliGroups(httpd_req_t* req) {
    auto* ctx = static_cast<ApiContext*>(req->user_ctx);
    if (checkAuth(req, ctx) != ESP_OK)
        return ESP_FAIL;

    const auto assignments = ctx->daliRegistry.getGroupAssignments();

    std::lock_guard<std::mutex> lock(ctx->arenaMutex);
    ctx->arena.reset();

    memory::JsonArenaAllocator allocator(ctx->arena);
    JsonDocument doc(&allocator);

    for (const auto& [longAddr, groups] : assignments) {
        const auto laStr = utils::longAddressToString(longAddr);
        auto grpArr = doc[laStr.data()].to<JsonArray>();
        for (int i = 0; i < 16; ++i) {
            if (groups.test(i))
                grpArr.add(i);
        }
    }

    const size_t neededSize = measureJson(doc) + 1;
    auto* buf = static_cast<char*>(ctx->arena.allocate(neededSize));
    if (!buf) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Arena out of memory");
        return ESP_FAIL;
    }

    serializeJson(doc, buf, neededSize);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, buf, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

esp_err_t ApiHandlers::setDaliGroups(httpd_req_t* req) {
    auto* ctx = static_cast<ApiContext*>(req->user_ctx);
    if (checkAuth(req, ctx) != ESP_OK)
        return ESP_FAIL;

    std::lock_guard<std::mutex> lock(ctx->arenaMutex);
    ctx->arena.reset();

    size_t received = 0;
    auto* buf = readRequestBodyToArena(req, ctx, 2048, &received);
    if (!buf) {
        return ESP_FAIL;
    }

    memory::JsonArenaAllocator allocator(ctx->arena);
    JsonDocument doc(&allocator);

    if (deserializeJson(doc, buf, received) == DeserializationError::Ok && doc.is<JsonObject>()) {
        const auto currentAssignments = ctx->daliRegistry.getGroupAssignments();
        for (JsonPair kv : doc.as<JsonObject>()) {
            auto laOpt = utils::stringToLongAddress(kv.key().c_str());
            if (!laOpt || !kv.value().is<JsonArray>())
                continue;

            GroupMask newMask;
            for (JsonVariant g : kv.value().as<JsonArray>()) {
                if (g.is<uint8_t>() && g.as<uint8_t>() < 16)
                    newMask.set(g.as<uint8_t>());
            }

            GroupMask curMask;
            auto it = currentAssignments.find(*laOpt);
            if (it != currentAssignments.end())
                curMask = it->second;

            for (uint8_t i = 0; i < 16; ++i) {
                if (newMask.test(i) != curMask.test(i)) {
                    ctx->daliRegistry.setDeviceGroupMembership(*laOpt, i, newMask.test(i));
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
        "web_grp", 4096, ctx, 4, nullptr);

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

    std::lock_guard<std::mutex> lock(ctx->arenaMutex);
    ctx->arena.reset();

    memory::JsonArenaAllocator allocator(ctx->arena);
    JsonDocument doc(&allocator);
    doc["scene_id"] = sceneId;
    auto lObj = doc["levels"].to<JsonObject>();

    for (uint8_t sa = 0; sa < 64; ++sa) {
        if (levels[sa] != 255) {
            auto laOpt = ctx->daliRegistry.getLongAddress(DaliInternalAddr(0, sa));
            if (laOpt)
                lObj[utils::longAddressToString(*laOpt).data()] = levels[sa];
        }
    }

    const size_t neededSize = measureJson(doc) + 1;
    auto* buf = static_cast<char*>(ctx->arena.allocate(neededSize));
    if (!buf) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Arena out of memory");
        return ESP_FAIL;
    }

    serializeJson(doc, buf, neededSize);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, buf, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

esp_err_t ApiHandlers::setDaliScenes(httpd_req_t* req) {
    auto* ctx = static_cast<ApiContext*>(req->user_ctx);
    if (checkAuth(req, ctx) != ESP_OK)
        return ESP_FAIL;

    std::lock_guard<std::mutex> lock(ctx->arenaMutex);
    ctx->arena.reset();

    size_t received = 0;
    auto* buf = readRequestBodyToArena(req, ctx, 2048, &received);
    if (!buf) {
        return ESP_FAIL;
    }

    memory::JsonArenaAllocator allocator(ctx->arena);
    JsonDocument doc(&allocator);

    if (deserializeJson(doc, buf, received) == DeserializationError::Ok) {
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

    const esp_err_t err = ctx->ota.startUpdate(strlen(customUrl) > 0 ? customUrl : nullptr);
    if (err == ESP_OK) {
        httpd_resp_send(req, R"({"status":"ok","message":"Update initiated"})", HTTPD_RESP_USE_STRLEN);
    } else {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Update start failed");
    }
    return ESP_OK;
}
} // namespace daliMQTT