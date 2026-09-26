// Copyright (c) 2026 Alice-Trade Inc.
// SPDX-License-Identifier: GPL-3.0-or-later

#include "webui/WebUI.hxx"
#include "system/ConfigStore.hxx"
#include "utils/FileHandle.hxx"
#include "webui/ApiHandlers.hxx"
#include <cstring>
#include <esp_log.h>
#include <mbedtls/base64.h>
#include <string_view>
#include <sys/stat.h>

namespace daliMQTT {

static constexpr char TAG[] = "WebServer";
static constexpr size_t FILE_CHUNK_SIZE = 2048;

WebUI::WebUI(ApiContext& apiCtx) : m_apiCtx(apiCtx) {}

WebUI::~WebUI() {
    stop();
}

esp_err_t WebUI::start() {
    if (m_serverHandle)
        return ESP_OK;

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.uri_match_fn = httpd_uri_match_wildcard;
    config.max_uri_handlers = 23;
    config.stack_size = 10240;
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
        {"/*", HTTP_GET, staticFileGetHandler, &m_apiCtx}};

    for (const auto& route : apiRoutes) {
        httpd_register_uri_handler(m_serverHandle, &route);
    }

    ESP_LOGI(TAG, "HTTP Web Server started on port 80");
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
    char filepath[544];
    snprintf(filepath, sizeof(filepath), "/littlefs%s", (strcmp(req->uri, "/") == 0) ? "/index.html" : req->uri);
    if (std::string_view(req->uri).find("..") != std::string_view::npos) {
        httpd_resp_send_err(req, HTTPD_403_FORBIDDEN, "Access Denied");
        return ESP_FAIL;
    }
    struct stat st{};
    if (stat(filepath, &st) != 0) {
        snprintf(filepath, sizeof(filepath), "/littlefs/index.html");
        if (stat(filepath, &st) != 0) {
            httpd_resp_send_404(req);
            return ESP_FAIL;
        }
    }

    FileHandle f(filepath, "r");
    if (!f) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    setContentTypeByFilename(req, filepath);

    char chunk[FILE_CHUNK_SIZE];
    size_t readBytes = 0;
    while ((readBytes = fread(chunk, 1, sizeof(chunk), f.get())) > 0) {
        if (httpd_resp_send_chunk(req, chunk, readBytes) != ESP_OK) {
            httpd_resp_send_chunk(req, nullptr, 0);
            return ESP_FAIL;
        }
    }

    httpd_resp_send_chunk(req, nullptr, 0);
    return ESP_OK;
}

void WebUI::setContentTypeByFilename(httpd_req_t* req, const char* filepath) {
    const std::string_view path(filepath);
    if (path.ends_with(".html"))
        httpd_resp_set_type(req, "text/html");
    else if (path.ends_with(".js"))
        httpd_resp_set_type(req, "application/javascript");
    else if (path.ends_with(".css"))
        httpd_resp_set_type(req, "text/css");
    else if (path.ends_with(".svg"))
        httpd_resp_set_type(req, "image/svg+xml");
    else if (path.ends_with(".ico"))
        httpd_resp_set_type(req, "image/x-icon");
    else if (path.ends_with(".json"))
        httpd_resp_set_type(req, "application/json");
    else
        httpd_resp_set_type(req, "text/plain");
}

} // namespace daliMQTT