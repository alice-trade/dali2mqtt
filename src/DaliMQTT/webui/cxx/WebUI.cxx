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
#include <DaliGroupManagement.hxx>
#include <DaliSceneManagement.hxx>
#include "ConfigManager.hxx"
#include "DaliDeviceController.hxx"
#include "utils/FileHandle.hxx"
#include "Wifi.hxx"
#include "MQTTClient.hxx"
#include "DaliAPI.hxx"

namespace daliMQTT
{
    static constexpr char  TAG[] = "WebUI Service";
    constexpr std::string_view WEB_MOUNT_POINT = "/spiffs";
    constexpr size_t SCRATCH_BUFSIZE = 8192;

    // --- Helpers for API Info ---
    static const char* get_chip_model_name(esp_chip_model_t model) {
        switch (model) {
            case CHIP_ESP32:   return "ESP32";
            case CHIP_ESP32S2: return "ESP32-S2";
            case CHIP_ESP32S3: return "ESP32-S3";
            case CHIP_ESP32C2: return "ESP32-C2";
            case CHIP_ESP32C3: return "ESP32-C3";
            case CHIP_ESP32C5: return "ESP32-C5";
            case CHIP_ESP32C6: return "ESP32-C6";
            case CHIP_ESP32H2: return "ESP32-H2";
            case CHIP_ESP32P4: return "ESP32-P4";
            default:           return "Unknown";
        }
    }

    static const char* get_mqtt_status_string(MqttStatus status) {
        switch (status) {
            case MqttStatus::CONNECTED:    return "Connected";
            case MqttStatus::CONNECTING:   return "Connecting";
            case MqttStatus::DISCONNECTED: return "Disconnected";
            default:                       return "Unknown";
        }
    }

    static const char* get_wifi_status_string(daliMQTT::Wifi::Status status) {
        switch (status) {
            case daliMQTT::Wifi::Status::CONNECTED:    return "Connected (STA)";
            case daliMQTT::Wifi::Status::CONNECTING:   return "Connecting (STA)";
            case daliMQTT::Wifi::Status::DISCONNECTED: return "Disconnected";
            case daliMQTT::Wifi::Status::AP_MODE:      return "Access Point";
            default:                                   return "Unknown";
        }
    }


    esp_err_t WebUI::start() {
        httpd_config_t config = HTTPD_DEFAULT_CONFIG();
        config.lru_purge_enable = true;
        config.uri_match_fn = httpd_uri_match_wildcard;
        config.max_uri_handlers = 13;
        ESP_LOGI(TAG, "Starting server on port: '%d'", config.server_port);
        if (httpd_start(&server_handle, &config) != ESP_OK) {
            ESP_LOGE(TAG, "Error starting server!");
            return ESP_FAIL;
        }

        // Register handlers
        const std::array<httpd_uri_t, 12> handlers = {{
            { .uri = "/api/config", .method = HTTP_GET,  .handler = api::GetConfigHandler, .user_ctx = this },
            { .uri = "/api/config", .method = HTTP_POST, .handler = api::SetConfigHandler, .user_ctx = this },
            { .uri = "/api/info",   .method = HTTP_GET,  .handler = api::GetInfoHandler,   .user_ctx = this },
            { .uri = "/api/dali/devices",    .method = HTTP_GET,  .handler = api::DaliGetDevicesHandler, .user_ctx = this },
            { .uri = "/api/dali/scan",       .method = HTTP_POST, .handler = api::DaliScanHandler,       .user_ctx = this },
            { .uri = "/api/dali/initialize", .method = HTTP_POST, .handler = api::DaliInitializeHandler, .user_ctx = this },
            { .uri = "/api/dali/names",      .method = HTTP_GET,  .handler = api::DaliGetNamesHandler,   .user_ctx = this },
            { .uri = "/api/dali/names",      .method = HTTP_POST, .handler = api::DaliSetNamesHandler,   .user_ctx = this },
            { .uri = "/api/dali/groups",     .method = HTTP_GET,  .handler = api::DaliGetGroupsHandler,  .user_ctx = nullptr },
            { .uri = "/api/dali/groups",     .method = HTTP_POST, .handler = api::DaliSetGroupsHandler,  .user_ctx = nullptr },
            { .uri = "/api/dali/scenes",     .method = HTTP_POST, .handler = api::DaliSetSceneHandler,   .user_ctx = nullptr },
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
            ESP_LOGD(TAG, "File '%s' not found. Assuming SPA route, serving index.html.", filepath.c_str());
            filepath = std::format("{}/index.html", WEB_MOUNT_POINT);
            if (stat(filepath.c_str(), &file_stat) == -1) {
                ESP_LOGE(TAG, "FATAL: Fallback file index.html not found!");
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
            read_bytes = fread(chunk.data(), 1, chunk.size(), f.get());
            if (read_bytes > 0) {
                if (httpd_resp_send_chunk(req, chunk.data(), read_bytes) != ESP_OK) {
                    ESP_LOGE(TAG, "File sending failed!");
                    // Важно не возвращать ошибку сразу, а закрыть chunk-ответ
                    httpd_resp_send_chunk(req, nullptr, 0);
                    return ESP_FAIL;
                }
            }
        } while (read_bytes > 0);

        httpd_resp_send_chunk(req, nullptr, 0);
        ESP_LOGD(TAG, "File sent successfully: %s", filepath.c_str());
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
            return ESP_OK;
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

        ConfigManager::getInstance().setConfig(current_cfg);

        if (esp_err_t save_err = ConfigManager::getInstance().save(); save_err != ESP_OK) {
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

    esp_err_t WebUI::api::GetInfoHandler(httpd_req_t *req) {
        if (checkAuth(req) != ESP_OK) return ESP_FAIL;

        esp_chip_info_t chip_info;
        esp_chip_info(&chip_info);

        // Get DALI status
        const auto& dali_api = DaliAPI::getInstance();
        std::string dali_status;
        if (dali_api.isInitialized()) {
            const auto discovered_devices = DaliDeviceController::getInstance().getDiscoveredDevices().count();
            dali_status = std::format("Active, {} devices found", discovered_devices);
        } else {
            dali_status = "Inactive (Provisioning Mode)";
        }

        // Get MQTT status
        const auto mqtt_status = MQTTClient::getInstance().getStatus();
        const char* mqtt_status_str = get_mqtt_status_string(mqtt_status);

        // Get WiFi status
        const auto& wifi = Wifi::getInstance();
        const std::string wifi_status_str = std::format("{} ({})", get_wifi_status_string(wifi.getStatus()), wifi.getIpAddress());

        cJSON *root = cJSON_CreateObject();
        cJSON_AddStringToObject(root, "version", "dev_0.8.0");
        cJSON_AddStringToObject(root, "chip_model", get_chip_model_name(chip_info.model));
        cJSON_AddNumberToObject(root, "chip_cores", chip_info.cores);
        cJSON_AddNumberToObject(root, "free_heap", esp_get_free_heap_size());
        cJSON_AddStringToObject(root, "dali_status", dali_status.c_str());
        cJSON_AddStringToObject(root, "mqtt_status", mqtt_status_str);
        cJSON_AddStringToObject(root, "wifi_status", wifi_status_str.c_str());

        char *json_string = cJSON_Print(root);
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, json_string, strlen(json_string));
        cJSON_Delete(root);
        free( json_string);
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

        char *json_string = cJSON_PrintUnformatted(root);
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, json_string, strlen(json_string));
        cJSON_Delete(root);
        free( json_string);

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

    esp_err_t WebUI::api::DaliGetNamesHandler(httpd_req_t *req) {
        if (checkAuth(req) != ESP_OK) return ESP_FAIL;
        const auto cfg = ConfigManager::getInstance().getConfig();
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, cfg.dali_device_identificators.c_str(), HTTPD_RESP_USE_STRLEN);
        return ESP_OK;
    }

    esp_err_t WebUI::api::DaliSetNamesHandler(httpd_req_t *req) {
        if (checkAuth(req) != ESP_OK) return ESP_FAIL;

        if (req->content_len >= 1024) {
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Request too long");
            return ESP_FAIL;
        }
        std::vector<char> buf(req->content_len + 1);
        const int ret = httpd_req_recv(req, buf.data(), req->content_len);
        if (ret <= 0) return ESP_FAIL;
        buf[ret] = '\0';

        cJSON *root = cJSON_Parse(buf.data());
        if (!cJSON_IsObject(root)) {
            cJSON_Delete(root);
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON: root must be an object");
            return ESP_FAIL;
        }
        cJSON_Delete(root);

        auto& configManager = ConfigManager::getInstance();
        auto current_cfg = configManager.getConfig();
        current_cfg.dali_device_identificators = std::string(buf.data(), ret);
        configManager.setConfig(current_cfg);
        configManager.save();

        httpd_resp_send(req, R"({"status":"ok", "message":"Device names saved."})", -1);
        return ESP_OK;
    }

    esp_err_t WebUI::api::DaliGetGroupsHandler(httpd_req_t *req) {
        if (checkAuth(req) != ESP_OK) return ESP_FAIL;

        const auto assignments = DaliGroupManagement::getInstance().getAllAssignments();
        cJSON *root = cJSON_CreateObject();

        for (const auto& [addr, groups] : assignments) {
            cJSON* group_array = cJSON_CreateArray();
            for (int i = 0; i < 16; ++i) {
                if (groups.test(i)) {
                    cJSON_AddItemToArray(group_array, cJSON_CreateNumber(i));
                }
            }
            cJSON_AddItemToObject(root, std::to_string(addr).c_str(), group_array);
        }

        char *json_string = cJSON_PrintUnformatted(root);
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, json_string, strlen(json_string));
        cJSON_Delete(root);
        free( json_string);
        return ESP_OK;
    }

    esp_err_t WebUI::api::DaliSetGroupsHandler(httpd_req_t *req) {
        if (checkAuth(req) != ESP_OK) return ESP_FAIL;

        std::vector<char> buf(req->content_len + 1, 0);
        if (httpd_req_recv(req, buf.data(), req->content_len) <= 0) return ESP_FAIL;

        cJSON *root = cJSON_Parse(buf.data());
        if (!cJSON_IsObject(root)) {
             httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON: root must be an object");
             cJSON_Delete(root);
             return ESP_FAIL;
        }

        GroupAssignments new_assignments;
        cJSON* device_item = nullptr;
        cJSON_ArrayForEach(device_item, root) {
            uint8_t addr = std::stoi(device_item->string);
            std::bitset<16> groups;
            if (cJSON_IsArray(device_item)) {
                cJSON* group_item = nullptr;
                cJSON_ArrayForEach(group_item, device_item) {
                    if (cJSON_IsNumber(group_item) && group_item->valueint < 16) {
                        groups.set(group_item->valueint);
                    }
                }
            }
            new_assignments[addr] = groups;
        }
        cJSON_Delete(root);

        DaliGroupManagement::getInstance().setAllAssignments(new_assignments);

        httpd_resp_send(req, R"({"status":"ok", "message":"Group assignments saved."})", -1);
        return ESP_OK;
    }

    esp_err_t WebUI::api::DaliSetSceneHandler(httpd_req_t *req) {
        if (checkAuth(req) != ESP_OK) return ESP_FAIL;

        std::vector<char> buf(req->content_len + 1, 0);
        if (httpd_req_recv(req, buf.data(), req->content_len) <= 0) return ESP_FAIL;

        cJSON *root = cJSON_Parse(buf.data());
        if (!cJSON_IsObject(root)) {
             httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON: root must be an object");
             cJSON_Delete(root);
             return ESP_FAIL;
        }

        cJSON* scene_id_item = cJSON_GetObjectItem(root, "scene_id");
        cJSON* levels_item = cJSON_GetObjectItem(root, "levels");

        if (!cJSON_IsNumber(scene_id_item) || !cJSON_IsObject(levels_item)) {
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON: 'scene_id' or 'levels' are missing/invalid");
            cJSON_Delete(root);
            return ESP_FAIL;
        }

        uint8_t scene_id = scene_id_item->valueint;
        SceneDeviceLevels levels;
        cJSON* level_item = nullptr;
        cJSON_ArrayForEach(level_item, levels_item) {
            uint8_t addr = std::stoi(level_item->string);
            const uint8_t level = level_item->valueint;
            levels[addr] = level;
        }

        cJSON_Delete(root);

        DaliSceneManagement::getInstance().saveScene(scene_id, levels);

        httpd_resp_send(req, R"({"status":"ok", "message":"Scene configuration saved to devices."})", -1);
        return ESP_OK;
    }
} // daliMQTT