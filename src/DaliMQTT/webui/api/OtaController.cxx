// Copyright (c) 2026 Alice-Trade Inc.
// SPDX-License-Identifier: GPL-2.0-or-later

#include "system/AppUpdateManager.hxx"
#include "system/ConfigManager.hxx"
#include <cJSON.h>
#include "webui/WebUI.hxx"

namespace daliMQTT {
    static constexpr char TAG[] = "WebUI_OTA";

    esp_err_t WebUI::api::OtaUpdateHandler(httpd_req_t *req) {
        if (checkAuth(req) != ESP_OK) return ESP_FAIL;

        if (AppUpdateManager::Instance().isUpdateInProgress()) {
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Update already in progress");
            return ESP_FAIL;
        }
        std::string target_url;
        int update_type = 0; // 0 = App, 1 = SPIFFS

        if (req->content_len > 0 && req->content_len < 512) {
            std::vector<char> buf(req->content_len + 1);
            const int ret = httpd_req_recv(req, buf.data(), req->content_len);
            if (ret > 0) {
                buf[ret] = '\0';
                cJSON *root = cJSON_Parse(buf.data());
                if (root) {
                    cJSON *url_item = cJSON_GetObjectItem(root, "url");
                    if (cJSON_IsString(url_item) && (url_item->valuestring != nullptr)) {
                        target_url = url_item->valuestring;
                    }
                    const cJSON *type_item = cJSON_GetObjectItem(root, "type");
                    if (cJSON_IsString(type_item) && strcmp(type_item->valuestring, "spiffs") == 0) {
                        update_type = 1;
                    }
                    cJSON_Delete(root);
                }
            }
        }

        if (target_url.empty()) {
            target_url = ConfigManager::Instance().getConfig().app_ota_url;
        }

        if (target_url.empty()) {
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "No OTA URL provided in request or config");
            return ESP_FAIL;
        }

        const bool started = AppUpdateManager::Instance().startUpdateAsync(target_url, update_type);

        if (started) {
            httpd_resp_send(req, "{\"status\": \"ok\", \"message\": \"OTA update started via system\"}", -1);
        } else {
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to start background OTA task");
        }

        return ESP_OK;
    }
}