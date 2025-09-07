#include "WebUI.hxx"
#include "esp_log.h"

#include "cJSON.h"
#include "ConfigManager.hxx"
#include <string_view>
#include <vector>
#include <sys/stat.h>
#include "esp_system.h"
#include "esp_chip_info.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mbedtls/base64.h"

namespace daliMQTT
{
    static const char* TAG = "WebServer";
    constexpr std::string_view WEB_MOUNT_POINT = "/spiffs";
    constexpr size_t MAX_FILE_SIZE = 20 * 1024; // 20KB
    constexpr size_t SCRATCH_BUFSIZE = 8192;

    WebUI& WebUI::getInstance() {
        static WebUI instance;
        return instance;
    }

    esp_err_t WebUI::start() {
        httpd_config_t config = HTTPD_DEFAULT_CONFIG();
        config.lru_purge_enable = true;
        config.uri_match_fn = httpd_uri_match_wildcard;

        ESP_LOGI(TAG, "Starting server on port: '%d'", config.server_port);
        if (httpd_start(&server_handle, &config) != ESP_OK) {
            ESP_LOGE(TAG, "Error starting server!");
            return ESP_FAIL;
        }

        httpd_uri_t api_get_config_uri = {
            .uri       = "/api/config", .method    = HTTP_GET,
            .handler   = apiGetConfigHandler, .user_ctx  = nullptr
        };
        httpd_register_uri_handler(server_handle, &api_get_config_uri);

        httpd_uri_t api_set_config_uri = {
            .uri       = "/api/config", .method    = HTTP_POST,
            .handler   = apiSetConfigHandler, .user_ctx  = nullptr
        };
        httpd_register_uri_handler(server_handle, &api_set_config_uri);

        httpd_uri_t api_get_info_uri = {
            .uri       = "/api/info", .method    = HTTP_GET,
            .handler   = apiGetInfoHandler, .user_ctx  = nullptr
        };
        httpd_register_uri_handler(server_handle, &api_get_info_uri);

        httpd_uri_t static_files_uri = {
            .uri = "/*", .method = HTTP_GET,
            .handler = staticFileGetHandler, .user_ctx = nullptr
        };
        httpd_register_uri_handler(server_handle, &static_files_uri);

        return ESP_OK;
    }

    esp_err_t WebUI::stop() const {
        if (server_handle) {
            return httpd_stop(server_handle);
        }
        return ESP_OK;
    }

    void WebUI::set_content_type_from_file(httpd_req_t *req, const char *filepath) {
        std::string_view fp(filepath);
        if (fp.ends_with(".html")) httpd_resp_set_type(req, "text/html");
        else if (fp.ends_with(".js")) httpd_resp_set_type(req, "application/javascript");
        else if (fp.ends_with(".css")) httpd_resp_set_type(req, "text/css");
        else if (fp.ends_with(".png")) httpd_resp_set_type(req, "image/png");
        else if (fp.ends_with(".ico")) httpd_resp_set_type(req, "image/x-icon");
        else if (fp.ends_with(".svg")) httpd_resp_set_type(req, "image/svg+xml");
        else httpd_resp_set_type(req, "text/plain");
    }

    esp_err_t WebUI::staticFileGetHandler(httpd_req_t *req) {
        std::string filepath = std::string(WEB_MOUNT_POINT.data()) + (strcmp(req->uri, "/") == 0 ? "/index.html" : req->uri);

        if (struct stat file_stat{}; stat(filepath.c_str(), &file_stat) == -1) {
            ESP_LOGE(TAG, "File not found: %s", filepath.c_str());
            filepath = std::string(WEB_MOUNT_POINT.data()) + "/index.html";
            if(stat(filepath.c_str(), &file_stat) == -1) {
                httpd_resp_send_404(req);
                return ESP_FAIL;
            }
        }

        FILE *f = fopen(filepath.c_str(), "r");
        if (!f) {
            ESP_LOGE(TAG, "Failed to open file for reading: %s", filepath.c_str());
            httpd_resp_send_500(req);
            return ESP_FAIL;
        }

        set_content_type_from_file(req, filepath.c_str());

        char *chunk = new char[SCRATCH_BUFSIZE];
        size_t read_bytes;
        do {
            read_bytes = fread(chunk, 1, SCRATCH_BUFSIZE, f);
            if (read_bytes > 0) {
                if (httpd_resp_send_chunk(req, chunk, read_bytes) != ESP_OK) {
                    fclose(f);
                    delete[] chunk;
                    ESP_LOGE(TAG, "File sending failed!");
                    return ESP_FAIL;
                }
            }
        } while (read_bytes > 0);

        fclose(f);
        delete[] chunk;
        httpd_resp_send_chunk(req, nullptr, 0); // End response
        return ESP_OK;
    }

    esp_err_t WebUI::checkAuth(httpd_req_t *req) {
        char *buf = nullptr;
        size_t buf_len = httpd_req_get_hdr_value_len(req, "Authorization") + 1;
        if (buf_len <= 1) {
            goto unauthorized;
        }

        buf = new char[buf_len];
        if (httpd_req_get_hdr_value_str(req, "Authorization", buf, buf_len) == ESP_OK) {
            std::string_view auth_header(buf);
            if (auth_header.starts_with("Basic ")) {
                auth_header.remove_prefix(6); // "Basic "
                unsigned char decoded[128];
                size_t decoded_len = 0;

                if (mbedtls_base64_decode(decoded, sizeof(decoded), &decoded_len, reinterpret_cast<const unsigned char*>(auth_header.data()), auth_header.length()) == 0) {
                    decoded[decoded_len] = '\0';
                    std::string_view decoded_sv(reinterpret_cast<char*>(decoded));
                    auto colon_pos = decoded_sv.find(':');
                    if (colon_pos != std::string_view::npos) {
                        auto cfg = ConfigManager::getInstance().getConfig();
                        if (decoded_sv.substr(0, colon_pos) == cfg.http_user &&
                            decoded_sv.substr(colon_pos + 1) == cfg.http_pass) {
                            delete[] buf;
                            return ESP_OK;
                        }
                    }
                }
            }
        }
        delete[] buf;

    unauthorized:
        httpd_resp_set_hdr(req, "WWW-Authenticate", "Basic realm=\"DALI Bridge\"");
        httpd_resp_send_err(req, HTTPD_401_UNAUTHORIZED, "Authentication failed");
        return ESP_FAIL;
    }

    esp_err_t WebUI::apiGetConfigHandler(httpd_req_t *req) {
        if (checkAuth(req) != ESP_OK) return ESP_FAIL;

        const AppConfig cfg = ConfigManager::getInstance().getConfig();
        cJSON *root = cJSON_CreateObject();
        cJSON_AddStringToObject(root, "wifi_ssid", cfg.wifi_ssid.c_str());
        cJSON_AddStringToObject(root, "mqtt_uri", cfg.mqtt_uri.c_str());
        cJSON_AddStringToObject(root, "mqtt_client_id", cfg.mqtt_client_id.c_str());
        cJSON_AddStringToObject(root, "mqtt_base_topic", cfg.mqtt_base_topic.c_str());
        cJSON_AddStringToObject(root, "http_user", cfg.http_user.c_str());

        const char *json_string = cJSON_Print(root);
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, json_string, strlen(json_string));
        cJSON_Delete(root);
        free((void*)json_string);
        return ESP_OK;
    }

    esp_err_t WebUI::apiSetConfigHandler(httpd_req_t *req) {
        if (checkAuth(req) != ESP_OK) return ESP_FAIL;

        char buf[512];
        const int remaining = req->content_len;
        if (remaining >= sizeof(buf)) {
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Request too long");
            return ESP_FAIL;
        }

        const int ret = httpd_req_recv(req, buf, remaining);
        if (ret <= 0) return ESP_FAIL;
        buf[ret] = '\0';

        cJSON *root = cJSON_Parse(buf);
        if (root == nullptr) {
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
            return ESP_FAIL;
        }

        AppConfig current_cfg = ConfigManager::getInstance().getConfig();
        #define JSON_STR_TO_CFG(NAME) if (cJSON* item = cJSON_GetObjectItem(root, #NAME); cJSON_IsString(item) && (item->valuestring != nullptr)) { current_cfg.NAME = item->valuestring; }

        JSON_STR_TO_CFG(wifi_ssid);
        JSON_STR_TO_CFG(wifi_password); // пароль может быть пустым, если не менялся
        JSON_STR_TO_CFG(mqtt_uri);
        JSON_STR_TO_CFG(mqtt_client_id);
        JSON_STR_TO_CFG(mqtt_base_topic);
        JSON_STR_TO_CFG(http_user);
        JSON_STR_TO_CFG(http_pass);
        #undef JSON_STR_TO_CFG

        if(current_cfg.wifi_ssid.empty() || current_cfg.mqtt_uri.empty()) {
            cJSON_Delete(root);
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "SSID and MQTT URI cannot be empty");
            return ESP_FAIL;
        }

        ConfigManager::getInstance().setConfig(current_cfg);
        ConfigManager::getInstance().save();
        cJSON_Delete(root);

        httpd_resp_send(req, R"({"status":"ok", "message":"Settings saved. Restarting..."})", -1);

        ESP_LOGI(TAG, "Configuration saved via WebUI. Restarting in 3 seconds...");
        vTaskDelay(pdMS_TO_TICKS(3000));
        esp_restart();
    }

    esp_err_t WebUI::apiGetInfoHandler(httpd_req_t *req) {
        if (checkAuth(req) != ESP_OK) return ESP_FAIL;

        esp_chip_info_t chip_info;
        esp_chip_info(&chip_info);

        cJSON *root = cJSON_CreateObject();
        cJSON_AddStringToObject(root, "version", "0.2.0");
        cJSON_AddStringToObject(root, "idf_version", esp_get_idf_version());
        cJSON_AddNumberToObject(root, "cores", chip_info.cores);
        cJSON_AddNumberToObject(root, "free_heap", esp_get_free_heap_size());

        const char *json_string = cJSON_Print(root);
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, json_string, strlen(json_string));
        cJSON_Delete(root);
        free((void*)json_string);
        return ESP_OK;
    }

} // daliMQTT