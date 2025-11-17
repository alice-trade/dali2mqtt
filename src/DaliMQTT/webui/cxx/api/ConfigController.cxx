
#include <WebUI.hxx>
#include "ConfigManager.hxx"
#include "DaliAPI.hxx"
#include <SyslogConfig.hxx>

namespace daliMQTT {
    static constexpr char  TAG[] = "WebUIConfig";
    esp_err_t WebUI::api::SetConfigHandler(httpd_req_t *req) {
            if (checkAuth(req) != ESP_OK) return ESP_FAIL;

            if (req->content_len >= 512) {
                httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Request too long");
                return ESP_FAIL;
            }
            std::vector<char> buf(req->content_len + 1);
            const int ret = httpd_req_recv(req, buf.data(), req->content_len);
            if (ret <= 0) return ESP_FAIL;
            buf[ret] = '\0';

            cJSON *root = cJSON_Parse(buf.data());
            if (root == nullptr) {
                httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
                return ESP_FAIL;
            }

            AppConfig current_cfg = ConfigManager::getInstance().getConfig();
            #define JsonSetStrConfig(NAME) if (cJSON* item = cJSON_GetObjectItem(root, #NAME); cJSON_IsString(item) && (item->valuestring != nullptr)) { current_cfg.NAME = item->valuestring; }

            JsonSetStrConfig(wifi_ssid);
            JsonSetStrConfig(wifi_password);
            JsonSetStrConfig(mqtt_uri);
            JsonSetStrConfig(mqtt_user);
            JsonSetStrConfig(mqtt_pass);
            JsonSetStrConfig(mqtt_client_id);
            JsonSetStrConfig(mqtt_base_topic);
            JsonSetStrConfig(http_user);
            JsonSetStrConfig(http_pass);
            #undef JsonSetStrConfig

            cJSON_Delete(root);
            if(current_cfg.wifi_ssid.empty() || current_cfg.mqtt_uri.empty()) {
                httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "SSID and MQTT URI cannot be empty");
                return ESP_FAIL;
            }

            if (esp_err_t save_err = ConfigManager::getInstance().saveMainConfig(current_cfg); save_err != ESP_OK) {
                ESP_LOGE(TAG, "Failed to save configuration to NVS! Error: %s (%d)", esp_err_to_name(save_err), save_err);
                httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to save settings to flash memory.");
                return ESP_FAIL;
            }
            httpd_resp_send(req, R"({"status":"ok", "message":"Settings saved. Restarting..."})", HTTPD_RESP_USE_STRLEN);

            ESP_LOGI(TAG, "Configuration saved via WebUI. Restarting in 3 seconds...");
            vTaskDelay(pdMS_TO_TICKS(3000));
            esp_restart();

            return ESP_OK;
    }

    esp_err_t WebUI::api::GetConfigHandler(httpd_req_t *req) {
         if (checkAuth(req) != ESP_OK) return ESP_FAIL;

         const AppConfig cfg = ConfigManager::getInstance().getConfig();
         cJSON *root = cJSON_CreateObject();
         cJSON_AddStringToObject(root, "wifi_ssid", cfg.wifi_ssid.c_str());
         cJSON_AddStringToObject(root, "mqtt_uri", cfg.mqtt_uri.c_str());
         cJSON_AddStringToObject(root, "mqtt_user", cfg.mqtt_user.c_str());
         cJSON_AddStringToObject(root, "mqtt_client_id", cfg.mqtt_client_id.c_str());
         cJSON_AddStringToObject(root, "mqtt_base_topic", cfg.mqtt_base_topic.c_str());
         cJSON_AddStringToObject(root, "http_user", cfg.http_user.c_str());

         char *json_string = cJSON_Print(root);
         httpd_resp_set_type(req, "application/json");
         httpd_resp_send(req, json_string, strlen(json_string));
         cJSON_Delete(root);
         free( json_string);
         return ESP_OK;
     }
}