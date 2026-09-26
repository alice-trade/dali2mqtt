// Copyright (c) 2026 Alice-Trade Inc.
// SPDX-License-Identifier: GPL-3.0-or-later

#include "webui/WebUI.hxx"
#include "system/ConfigStore.hxx"
#include "webui/ApiHandlers.hxx"
#include <cstring>
#include <esp_log.h>
#include <string_view>

namespace daliMQTT {

static constexpr char TAG[] = "WebServer";

extern "C" {
extern const uint8_t index_html_gz_start[] asm("_binary_index_html_gz_start");
extern const uint8_t index_html_gz_end[]   asm("_binary_index_html_gz_end");
extern const uint8_t app_js_gz_start[]     asm("_binary_app_js_gz_start");
extern const uint8_t app_js_gz_end[]       asm("_binary_app_js_gz_end");
extern const uint8_t app_css_gz_start[]    asm("_binary_app_css_gz_start");
extern const uint8_t app_css_gz_end[]      asm("_binary_app_css_gz_end");

extern __attribute__((weak)) const uint8_t _binary_index_html_gz_start[1] = {};
extern __attribute__((weak)) const uint8_t _binary_index_html_gz_end[1]   = {};
extern __attribute__((weak)) const uint8_t _binary_app_js_gz_start[1]     = {};
extern __attribute__((weak)) const uint8_t _binary_app_js_gz_end[1]       = {};
extern __attribute__((weak)) const uint8_t _binary_app_css_gz_start[1]    = {};
extern __attribute__((weak)) const uint8_t _binary_app_css_gz_end[1]      = {};
}

WebUI::WebUI(ApiContext& apiCtx) : m_apiCtx(apiCtx) {}

WebUI::~WebUI() {
    stop();
}

esp_err_t WebUI::start() {
    if (m_serverHandle)
        return ESP_OK;

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.uri_match_fn = httpd_uri_match_wildcard;
    config.max_uri_handlers = 24;
    config.stack_size = 8192;
    config.lru_purge_enable = true;

    ESP_RETURN_ON_ERROR(httpd_start(&m_serverHandle, &config), TAG, "HTTP Server start failed");

    const httpd_uri_t apiRoutes[] = {
        {"/api/config", HTTP_GET, ApiHandlers::getConfig, &m_apiCtx},
        {"/api/config", HTTP_POST, ApiHandlers::setConfig, &m_apiCtx},
        {"/api/info", HTTP_GET, ApiHandlers::getInfo, &m_apiCtx},
        {"/api/dali/devices", HTTP_GET, ApiHandlers::getDaliDevices, &m_apiCtx},
        {"/api/dali/device/control", HTTP_POST, ApiHandlers::controlDaliDevice, &m_apiCtx},
        {"/api/dali/scan", HTTP_POST, ApiHandlers::scanDaliBus, &m_apiCtx},
        {"/api/dali/status", HTTP_GET, ApiHandlers::getDaliStatus, &m_apiCtx},
        {"/api/dali/initialize", HTTP_POST, ApiHandlers::initializeDaliBus, &m_apiCtx},
        {"/api/dali/initialize-cd", HTTP_POST, ApiHandlers::initializeDaliInputs, &m_apiCtx},
        {"/api/dali/names", HTTP_GET, ApiHandlers::getDaliNames, &m_apiCtx},
        {"/api/dali/names", HTTP_POST, ApiHandlers::setDaliNames, &m_apiCtx},
        {"/api/dali/groups", HTTP_GET, ApiHandlers::getDaliGroups, &m_apiCtx},
        {"/api/dali/groups", HTTP_POST, ApiHandlers::setDaliGroups, &m_apiCtx},
        {"/api/dali/groups/refresh", HTTP_POST, ApiHandlers::refreshDaliGroups, &m_apiCtx},
        {"/api/dali/scenes", HTTP_GET, ApiHandlers::getDaliScenes, &m_apiCtx},
        {"/api/dali/scenes", HTTP_POST, ApiHandlers::setDaliScenes, &m_apiCtx},
        {"/api/ota/check", HTTP_POST, ApiHandlers::triggerOtaCheck, &m_apiCtx},
        {"/api/ota/install", HTTP_POST, ApiHandlers::triggerOtaInstall, &m_apiCtx},
        {"/api/ota/upload", HTTP_POST, ApiHandlers::uploadOtaBin, &m_apiCtx},
        {"/*", HTTP_GET, staticFileGetHandler, &m_apiCtx}
    };

    for (const auto& route : apiRoutes) {
        httpd_register_uri_handler(m_serverHandle, &route);
    }

    ESP_LOGI(TAG, "HTTP Web Server started (Embedded Static Gzip)");
    return ESP_OK;
}

esp_err_t WebUI::stop() {
    if (m_serverHandle) {
        httpd_stop(m_serverHandle);
        m_serverHandle = nullptr;
    }
    return ESP_OK;
}

esp_err_t WebUI::staticFileGetHandler(httpd_req_t* req) {
    const std::string_view uri(req->uri);

    if (uri.starts_with("/api/")) {
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "API endpoint not found");
        return ESP_FAIL;
    }

    auto calcSize = [](const uint8_t* start, const uint8_t* end) noexcept -> size_t {
        return static_cast<size_t>(reinterpret_cast<uintptr_t>(end) - reinterpret_cast<uintptr_t>(start));
    };

    const uint8_t* data = nullptr;
    size_t size = 0;
    const char* contentType = nullptr;
    bool cacheable = false;

    if (uri == "/app.js" || uri.ends_with(".js")) {
        data = app_js_gz_start;
        size = calcSize(app_js_gz_start, app_js_gz_end);
        contentType = "application/javascript";
        cacheable = true;
    } else if (uri == "/app.css" || uri.ends_with(".css")) {
        data = app_css_gz_start;
        size = calcSize(app_css_gz_start, app_css_gz_end);
        contentType = "text/css";
        cacheable = true;
    } else {
        data = index_html_gz_start;
        size = calcSize(index_html_gz_start, index_html_gz_end);
        contentType = "text/html";
        cacheable = false;
    }

    httpd_resp_set_type(req, contentType);
    httpd_resp_set_hdr(req, "Content-Encoding", "gzip");

    if (cacheable) {
        httpd_resp_set_hdr(req, "Cache-Control", "no-cache");
        httpd_resp_set_hdr(req, "ETag", DALIMQTT_VERSION);
    } else {
        httpd_resp_set_hdr(req, "Cache-Control", "no-store, no-cache, must-revalidate");
    }

    return httpd_resp_send(req, reinterpret_cast<const char*>(data), size);
}

} // namespace daliMQTT