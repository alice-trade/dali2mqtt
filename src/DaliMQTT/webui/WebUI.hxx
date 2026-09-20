// Copyright (c) 2026 Alice-Trade Inc.
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef DALIMQTT_WEBUI_HXX
#define DALIMQTT_WEBUI_HXX

#include "webui/ApiContext.hxx"
#include <esp_err.h>
#include <esp_http_server.h>

namespace daliMQTT {

class WebUI {
public:
    explicit WebUI(ApiContext& apiCtx);
    ~WebUI();

    WebUI(const WebUI&) = delete;
    WebUI& operator=(const WebUI&) = delete;

    esp_err_t start();
    esp_err_t stop();

    [[nodiscard]] inline bool isRunning() const noexcept;

private:
    static esp_err_t staticFileGetHandler(httpd_req_t* req);
    static void setContentTypeByFilename(httpd_req_t* req, const char* filepath);

    ApiContext& m_apiCtx;
    httpd_handle_t m_serverHandle{nullptr};
};

} // namespace daliMQTT

#include "webui/WebUI.icc"

#endif // DALIMQTT_WEBUI_HXX