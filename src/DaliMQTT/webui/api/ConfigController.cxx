#include <webui/WebUI.hxx>
#include "system/ConfigManager.hxx"
#include "dali/DaliAdapter.hxx"
#include "system/AppController.hxx"

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

        ConfigUpdateResult config_update_result = ConfigManager::Instance().updateConfigFromJson(buf.data());


        switch (config_update_result) {
        case ConfigUpdateResult::MQTTUpdate:
            ESP_LOGI(TAG, "MQTT Settings changed. Triggering hot reload via AppController.");
            httpd_resp_send(req, R"({"status":"ok", "message":"MQTT configuration updated. Reconnecting services..."})", HTTPD_RESP_USE_STRLEN);
            xTaskCreate([](void*){
                vTaskDelay(pdMS_TO_TICKS(200));
                AppController::Instance().onConfigReloadRequest();
                vTaskDelete(nullptr);
            }, "mqtt_reload_task", 4096, nullptr, 5, nullptr);
            break;

        case ConfigUpdateResult::WIFIUpdate:
        case ConfigUpdateResult::SystemUpdate:
            ESP_LOGW(TAG, "System/WiFi settings changed. Reboot required.");
            httpd_resp_send(req, R"({"status":"ok", "message":"System settings saved. Restarting device..."})", HTTPD_RESP_USE_STRLEN);

            vTaskDelay(pdMS_TO_TICKS(1000));
            esp_restart();
            break;

        case ConfigUpdateResult::NoUpdate:
        default:
            ESP_LOGI(TAG, "Configuration updated but no active services require restart.");
            httpd_resp_send(req, R"({"status":"ok", "message":"Settings saved."})", HTTPD_RESP_USE_STRLEN);
            break;
        }

        return ESP_OK;
    }

    esp_err_t WebUI::api::GetConfigHandler(httpd_req_t *req) {
        if (checkAuth(req) != ESP_OK) return ESP_FAIL;

        cJSON *root = ConfigManager::Instance().getSerializedConfig(true);

        char *json_string = cJSON_Print(root);
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, json_string, strlen(json_string));

        cJSON_Delete(root);
        free(json_string);
        return ESP_OK;
    }
}
