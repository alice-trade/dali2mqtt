#include <webui/WebUI.hxx>
#include "system/ConfigManager.hxx"
#include "dali/DaliAPI.hxx"

namespace daliMQTT {
    static constexpr char TAG[] = "WebUIConfig";

    esp_err_t WebUI::api::SetConfigHandler(httpd_req_t *req) {
        if (checkAuth(req) != ESP_OK) return ESP_FAIL;

        if (req->content_len >= 1024) {
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Request too long");
            return ESP_FAIL;
        }
        std::vector<char> buf(req->content_len + 1);
        const int ret = httpd_req_recv(req, buf.data(), req->content_len);
        if (ret <= 0) return ESP_FAIL;
        buf[ret] = '\0';

        bool reboot_needed = false;
        esp_err_t err = ConfigManager::getInstance().updateConfigFromJson(buf.data(), reboot_needed);

        if (err == ESP_ERR_INVALID_ARG) {
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST,
                                "Invalid Configuration (Missing SSID/URI or Malformed JSON)");
            return ESP_FAIL;
        }
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to save configuration: %s", esp_err_to_name(err));
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to save settings to flash.");
            return ESP_FAIL;
        }

        if (reboot_needed) {
            httpd_resp_send(req, R"({"status":"ok", "message":"Settings saved. Restarting..."})",
                            HTTPD_RESP_USE_STRLEN);
            ESP_LOGI(TAG, "Configuration saved via WebUI. Restarting in 3 seconds...");
            vTaskDelay(pdMS_TO_TICKS(3000));
            esp_restart();
        } else {
            httpd_resp_send(req, R"({"status":"ok", "message":"Settings saved (no changes or non-critical)."})",
                            HTTPD_RESP_USE_STRLEN);
        }

        return ESP_OK;
    }

    esp_err_t WebUI::api::GetConfigHandler(httpd_req_t *req) {
        if (checkAuth(req) != ESP_OK) return ESP_FAIL;

        cJSON *root = ConfigManager::getInstance().getSerializedConfig(true);

        char *json_string = cJSON_Print(root);
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, json_string, strlen(json_string));

        cJSON_Delete(root);
        free(json_string);
        return ESP_OK;
    }
}
