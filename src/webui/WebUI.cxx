#include "WebUI.hxx"
#include "esp_log.h"
#include "cJSON.h"
#include "ConfigManager.hxx"
namespace daliMQTT
{
    static const char* TAG = "WebServer";

    WebUI& WebUI::getInstance() {
        static WebUI instance;
        return instance;
    }

    esp_err_t WebUI::start() {
        httpd_config_t config = HTTPD_DEFAULT_CONFIG();
        config.lru_purge_enable = true; // Для статических файлов

        ESP_LOGI(TAG, "Starting server on port: '%d'", config.server_port);
        if (httpd_start(&server_handle, &config) != ESP_OK) {
            ESP_LOGE(TAG, "Error starting server!");
            return ESP_FAIL;
        }

        // --- Регистрация URI обработчиков ---
        httpd_uri_t root_uri = {
            .uri       = "/",
            .method    = HTTP_GET,
            .handler   = rootGetHandler,
            .user_ctx  = nullptr
        };
        httpd_register_uri_handler(server_handle, &root_uri);

        httpd_uri_t api_get_config_uri = {
            .uri       = "/api/config",
            .method    = HTTP_GET,
            .handler   = apiGetConfigHandler,
            .user_ctx  = nullptr
        };
        httpd_register_uri_handler(server_handle, &api_get_config_uri);

        httpd_uri_t api_set_config_uri = {
            .uri       = "/api/config",
            .method    = HTTP_POST,
            .handler   = apiSetConfigHandler,
            .user_ctx  = nullptr
        };
        httpd_register_uri_handler(server_handle, &api_set_config_uri);

        return ESP_OK;
    }

    esp_err_t WebUI::stop() const
    {
        if (server_handle) {
            return httpd_stop(server_handle);
        }
        return ESP_OK;
    }


    // TODO: здесь будет код для отдачи Vue SPA из SPIFFS.
    esp_err_t WebUI::rootGetHandler(httpd_req_t *req) {
        const char* resp_str = R"html(
            <!DOCTYPE html>
            <html>
            <head><title>DALI-MQTT Bridge</title></head>
            <body>
                <h1>DALI-MQTT Bridge is in Provisioning Mode</h1>
                <p>Connect to the 'DALI-Bridge-Setup' WiFi network and configure your device.</p>
            </body>
            </html>
        )html";
        httpd_resp_send(req, resp_str, HTTPD_RESP_USE_STRLEN);
        return ESP_OK;
    }


    esp_err_t WebUI::apiGetConfigHandler(httpd_req_t *req) {
        AppConfig cfg = ConfigManager::getInstance().getConfig();

        cJSON *root = cJSON_CreateObject();
        cJSON_AddStringToObject(root, "wifi_ssid", cfg.wifi_ssid.c_str());
        cJSON_AddStringToObject(root, "mqtt_uri", cfg.mqtt_uri.c_str());

        const char *json_string = cJSON_Print(root);
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, json_string, strlen(json_string));

        cJSON_Delete(root);
        free((void*)json_string);
        return ESP_OK;
    }

    esp_err_t WebUI::apiSetConfigHandler(httpd_req_t *req) {
        char buf[256];
        const int remaining = req->content_len;

        if (remaining >= sizeof(buf)) {
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Request too long");
            return ESP_FAIL;
        }

        const int ret = httpd_req_recv(req, buf, remaining);
        if (ret <= 0) { // Ошибка или конец
            return ESP_FAIL;
        }
        buf[ret] = '\0'; // Null-terminate

        cJSON *root = cJSON_Parse(buf);
        if (root == nullptr) {
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
            return ESP_FAIL;
        }

        AppConfig current_cfg = ConfigManager::getInstance().getConfig();
        cJSON *ssid = cJSON_GetObjectItem(root, "wifi_ssid");
        if (cJSON_IsString(ssid) && (ssid->valuestring != nullptr)) {
            current_cfg.wifi_ssid = ssid->valuestring;
        }
        //... (добавить парсинг других полей)

        ConfigManager::getInstance().setConfig(current_cfg);
        ConfigManager::getInstance().save();

        cJSON_Delete(root);
        httpd_resp_send(req, "{\"status\":\"ok\"}", -1);

        // TODO: esp_restart();
        return ESP_OK;
    }
} // daliMQTT