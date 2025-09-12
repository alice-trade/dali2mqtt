#include <esp_log.h>
#include <cJSON.h>
#include <string_view>
#include <vector>
#include <sys/stat.h>
#include <format>
#include <array>
#include <esp_system.h>
#include <esp_chip_info.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <mbedtls/base64.h>
#include "WebUI.hxx"
#include "ConfigManager.hxx"
#include "DaliDeviceController.hxx"
#include "utils/FileHandle.hxx"

namespace daliMQTT
{
    static constexpr char  TAG[] = "WebUI Service";
    constexpr std::string_view WEB_MOUNT_POINT = "/spiffs";
    constexpr size_t SCRATCH_BUFSIZE = 8192;

    esp_err_t WebUI::start() {
        httpd_config_t config = HTTPD_DEFAULT_CONFIG();
        config.lru_purge_enable = true;
        config.uri_match_fn = httpd_uri_match_wildcard;

        ESP_LOGI(TAG, "Starting server on port: '%d'", config.server_port);
        if (httpd_start(&server_handle, &config) != ESP_OK) {
            ESP_LOGE(TAG, "Error starting server!");
            return ESP_FAIL;
        }

        // Register handlers
        const std::array<httpd_uri_t, 7> handlers = {{
            { .uri = "/api/config", .method = HTTP_GET,  .handler = api::GetConfigHandler, .user_ctx = this },
            { .uri = "/api/config", .method = HTTP_POST, .handler = api::SetConfigHandler, .user_ctx = this },
            { .uri = "/api/info",   .method = HTTP_GET,  .handler = api::GetInfoHandler,   .user_ctx = this },
            { .uri = "/api/dali/devices",    .method = HTTP_GET,  .handler = api::DaliGetDevicesHandler, .user_ctx = this },
            { .uri = "/api/dali/scan",       .method = HTTP_POST, .handler = api::DaliScanHandler,       .user_ctx = this },
            { .uri = "/api/dali/initialize", .method = HTTP_POST, .handler = api::DaliInitializeHandler, .user_ctx = this },
            { .uri = "/*",          .method = HTTP_GET,  .handler = staticFileGetHandler, .user_ctx = nullptr }
        }};

        for(auto const& handler : handlers) {
            httpd_register_uri_handler(server_handle, &handler);
        }

        return ESP_OK;
    }

    esp_err_t WebUI::stop() const {
        if (server_handle) {
            return httpd_stop(server_handle);
        }
        return ESP_OK;
    }

    // --- Static File and Auth ---
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
        std::string filepath = std::format("{}{}", WEB_MOUNT_POINT, (strcmp(req->uri, "/") == 0 ? "/index.html" : req->uri));

        if (struct stat file_stat{}; stat(filepath.c_str(), &file_stat) == -1) {
            ESP_LOGE(TAG, "File not found: %s, falling back to index.html", filepath.c_str());
            filepath = std::format("{}/index.html", WEB_MOUNT_POINT);
            if(stat(filepath.c_str(), &file_stat) == -1) {
                ESP_LOGE(TAG, "index.html not found either.");
                httpd_resp_send_404(req);
                return ESP_FAIL;
            }
        }

        FileHandle f(filepath.c_str(), "r");
        if (!f) {
            ESP_LOGE(TAG, "Failed to open file for reading: %s", filepath.c_str());
            httpd_resp_send_500(req);
            return ESP_FAIL;
        }

        set_content_type_from_file(req, filepath.c_str());

        std::vector<char> chunk(SCRATCH_BUFSIZE);
        size_t read_bytes;
        do {
            read_bytes = fread(chunk.data(), 1, SCRATCH_BUFSIZE, f.get());
            if (read_bytes > 0) {
                if (httpd_resp_send_chunk(req, chunk.data(), read_bytes) != ESP_OK) {
                    ESP_LOGE(TAG, "File sending failed!");
                    return ESP_FAIL;
                }
            }
        } while (read_bytes > 0);

        httpd_resp_send_chunk(req, nullptr, 0); // End response
        return ESP_OK;
    }

    esp_err_t WebUI::checkAuth(httpd_req_t *req) {
        auto send_unauthorized = [req]() {
            httpd_resp_set_hdr(req, "WWW-Authenticate", "Basic realm=\"DALI Bridge\"");
            httpd_resp_send_err(req, HTTPD_401_UNAUTHORIZED, "Authentication failed");
            return ESP_FAIL;
        };

        size_t buf_len = httpd_req_get_hdr_value_len(req, "Authorization") + 1;
        if (buf_len <= 1) {
            return send_unauthorized();
        }

        std::vector<char> buf(buf_len);
        if (httpd_req_get_hdr_value_str(req, "Authorization", buf.data(), buf_len) != ESP_OK) {
            return send_unauthorized();
        }

        std::string_view auth_header(buf.data());
        if (!auth_header.starts_with("Basic ")) {
            return send_unauthorized();
        }

        auth_header.remove_prefix(6); // "Basic "
        std::vector<unsigned char> decoded(128);
        size_t decoded_len = 0;

        if (mbedtls_base64_decode(decoded.data(), decoded.size(), &decoded_len, reinterpret_cast<const unsigned char*>(auth_header.data()), auth_header.length()) != 0) {
            return send_unauthorized();
        }

        decoded[decoded_len] = '\0';
        std::string_view decoded_sv(reinterpret_cast<char*>(decoded.data()));

        const auto colon_pos = decoded_sv.find(':');
        if (colon_pos == std::string_view::npos) {
            return send_unauthorized();
        }

        const auto cfg = ConfigManager::getInstance().getConfig();
        if (decoded_sv.substr(0, colon_pos) == cfg.http_user && decoded_sv.substr(colon_pos + 1) == cfg.http_pass) {
            return ESP_OK; // Авторизация успешна
        }

        return send_unauthorized();
    }

    // --- API Handlers ---

    esp_err_t WebUI::api::GetConfigHandler(httpd_req_t *req) {
        if (checkAuth(req) != ESP_OK) return ESP_FAIL;

        const AppConfig cfg = ConfigManager::getInstance().getConfig();
        cJSON *root = cJSON_CreateObject();
        cJSON_AddStringToObject(root, "wifi_ssid", cfg.wifi_ssid.c_str());
        cJSON_AddStringToObject(root, "mqtt_uri", cfg.mqtt_uri.c_str());
        cJSON_AddStringToObject(root, "mqtt_client_id", cfg.mqtt_client_id.c_str());
        cJSON_AddStringToObject(root, "mqtt_base_topic", cfg.mqtt_base_topic.c_str());
        cJSON_AddStringToObject(root, "http_user", cfg.http_user.c_str());

        char *json_string = cJSON_Print(root);
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, json_string, strlen(json_string));
        cJSON_Delete(root);
        delete json_string;
        return ESP_OK;
    }

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
        #define JSON_STR_TO_CFG(NAME) if (cJSON* item = cJSON_GetObjectItem(root, #NAME); cJSON_IsString(item) && (item->valuestring != nullptr)) { current_cfg.NAME = item->valuestring; }

        JSON_STR_TO_CFG(wifi_ssid);
        JSON_STR_TO_CFG(wifi_password);
        JSON_STR_TO_CFG(mqtt_uri);
        JSON_STR_TO_CFG(mqtt_client_id);
        JSON_STR_TO_CFG(mqtt_base_topic);
        JSON_STR_TO_CFG(http_user);
        JSON_STR_TO_CFG(http_pass);
        #undef JSON_STR_TO_CFG

        cJSON_Delete(root);

        if(current_cfg.wifi_ssid.empty() || current_cfg.mqtt_uri.empty()) {
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "SSID and MQTT URI cannot be empty");
            return ESP_FAIL;
        }

        ConfigManager::getInstance().setConfig(current_cfg);
        ConfigManager::getInstance().save();

        httpd_resp_send(req, R"({"status":"ok", "message":"Settings saved. Restarting..."})", -1);

        ESP_LOGI(TAG, "Configuration saved via WebUI. Restarting in 3 seconds...");
        vTaskDelay(pdMS_TO_TICKS(3000));
        esp_restart();

        return ESP_OK;
    }

    esp_err_t WebUI::api::GetInfoHandler(httpd_req_t *req) {
        if (checkAuth(req) != ESP_OK) return ESP_FAIL;

        esp_chip_info_t chip_info;
        esp_chip_info(&chip_info);

        cJSON *root = cJSON_CreateObject();
        cJSON_AddStringToObject(root, "version", "0.3.0");
        cJSON_AddStringToObject(root, "idf_version", esp_get_idf_version());
        cJSON_AddNumberToObject(root, "cores", chip_info.cores);
        cJSON_AddNumberToObject(root, "free_heap", esp_get_free_heap_size());

        char *json_string = cJSON_Print(root);
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, json_string, strlen(json_string));
        cJSON_Delete(root);
        delete json_string;
        return ESP_OK;
    }

    esp_err_t WebUI::api::DaliGetDevicesHandler(httpd_req_t *req) {
        if (checkAuth(req) != ESP_OK) return ESP_FAIL;

        auto devices = DaliDeviceController::getInstance().getDiscoveredDevices();
        cJSON *root = cJSON_CreateArray();

        for (int i = 0; i < 64; ++i) {
            if (devices.test(i)) {
                cJSON_AddItemToArray(root, cJSON_CreateNumber(i));
            }
        }

        const char *json_string = cJSON_Print(root);
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, json_string, strlen(json_string));
        cJSON_Delete(root);
        delete json_string;

        return ESP_OK;
    }

    esp_err_t WebUI::api::DaliScanHandler(httpd_req_t *req) {
        if (checkAuth(req) != ESP_OK) return ESP_FAIL;

        ESP_LOGI(TAG, "Triggering DALI scan from WebUI.");
        DaliDeviceController::getInstance().performScan();

        httpd_resp_send(req, R"({"status":"ok", "message":"Scan completed."})", -1);
        return ESP_OK;
    }

    esp_err_t WebUI::api::DaliInitializeHandler(httpd_req_t *req) {
        if (checkAuth(req) != ESP_OK) return ESP_FAIL;

        ESP_LOGI(TAG, "Triggering DALI initialization from WebUI.");
        DaliDeviceController::getInstance().performFullInitialization();

        httpd_resp_send(req, R"({"status":"ok", "message":"Initialization completed."})", -1);
        return ESP_OK;
    }

} // daliMQTT