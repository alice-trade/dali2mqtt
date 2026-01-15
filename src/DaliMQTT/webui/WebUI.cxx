#include <sys/stat.h>
#include <esp_chip_info.h>
#include <mbedtls/base64.h>
#include "webui/WebUI.hxx"
#include <utils/StringUtils.hxx>
#include "system/ConfigManager.hxx"
#include "utils/FileHandle.hxx"


namespace daliMQTT
{
    static constexpr char  TAG[] = "WebUIService";
    constexpr std::string_view WEB_MOUNT_POINT = "/spiffs";
    constexpr size_t SCRATCH_BUFSIZE = 4096;


    esp_err_t WebUI::start() {
        httpd_config_t config = HTTPD_DEFAULT_CONFIG();

        config.lru_purge_enable = true;
        config.uri_match_fn = httpd_uri_match_wildcard;
        config.max_uri_handlers = 17;
        config.max_open_sockets = 4;
        config.backlog_conn = 4;
        ESP_LOGI(TAG, "Starting server on port: '%d'", config.server_port);
        if (httpd_start(&server_handle, &config) != ESP_OK) {
            ESP_LOGE(TAG, "Error starting server!");
            return ESP_FAIL;
        }

        // Register handlers
        const std::array<httpd_uri_t, 16> handlers = {{
            { .uri = "/api/config", .method = HTTP_GET,  .handler = api::GetConfigHandler, .user_ctx = this },
            { .uri = "/api/config", .method = HTTP_POST, .handler = api::SetConfigHandler, .user_ctx = this },
            { .uri = "/api/info",   .method = HTTP_GET,  .handler = api::GetInfoHandler,   .user_ctx = this },
            { .uri = "/api/dali/devices",    .method = HTTP_GET,  .handler = api::DaliGetDevicesHandler, .user_ctx = this },
            { .uri = "/api/dali/scan",       .method = HTTP_POST, .handler = api::DaliScanHandler,       .user_ctx = this },
            { .uri = "/api/dali/status",     .method = HTTP_GET,  .handler = api::DaliGetStatusHandler,  .user_ctx = this },
            { .uri = "/api/dali/initialize", .method = HTTP_POST, .handler = api::DaliInitializeHandler, .user_ctx = this },
            { .uri = "/api/dali/names",      .method = HTTP_GET,  .handler = api::DaliGetNamesHandler,   .user_ctx = this },
            { .uri = "/api/dali/names",      .method = HTTP_POST, .handler = api::DaliSetNamesHandler,   .user_ctx = this },
            { .uri = "/api/dali/groups",     .method = HTTP_GET,  .handler = api::DaliGetGroupsHandler,  .user_ctx = nullptr },
            { .uri = "/api/dali/groups",     .method = HTTP_POST, .handler = api::DaliSetGroupsHandler,  .user_ctx = nullptr },
            { .uri = "/api/dali/groups/refresh", .method = HTTP_POST, .handler = api::DaliRefreshGroupsHandler, .user_ctx = nullptr },
            { .uri = "/api/dali/scenes",     .method = HTTP_POST, .handler = api::DaliSetSceneHandler,   .user_ctx = nullptr },
            { .uri = "/api/dali/scenes",     .method = HTTP_GET,  .handler = api::DaliGetSceneHandler,   .user_ctx = nullptr },
            { .uri = "/*",          .method = HTTP_GET,  .handler = staticFileGetHandler, .user_ctx = nullptr },
            { .uri = "/api/ota/pull", .method = HTTP_POST, .handler = api::OtaUpdateHandler, .user_ctx = this },

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

    void WebUI::set_content_type_from_file(httpd_req_t *req, const char *filepath) {
        const std::string_view fp(filepath);
        if (fp.ends_with(".html")) httpd_resp_set_type(req, "text/html");
        else if (fp.ends_with(".js")) httpd_resp_set_type(req, "application/javascript");
        else if (fp.ends_with(".css")) httpd_resp_set_type(req, "text/css");
        else if (fp.ends_with(".png")) httpd_resp_set_type(req, "image/png");
        else if (fp.ends_with(".ico")) httpd_resp_set_type(req, "image/x-icon");
        else if (fp.ends_with(".svg")) httpd_resp_set_type(req, "image/svg+xml");
        else httpd_resp_set_type(req, "text/plain");
    }

    esp_err_t WebUI::staticFileGetHandler(httpd_req_t *req) {
        std::string filepath = utils::stringFormat("%s%s", WEB_MOUNT_POINT.data(), (strcmp(req->uri, "/") == 0 ? "/index.html" : req->uri));

        if (struct stat file_stat{}; stat(filepath.c_str(), &file_stat) == -1) {
            ESP_LOGD(TAG, "File '%s' not found. Assuming SPA route, serving index.html.", filepath.c_str());
            filepath = utils::stringFormat("%s/index.html", WEB_MOUNT_POINT.data());
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
        httpd_resp_set_hdr(req, "Connection", "close");

        std::vector<char> chunk(SCRATCH_BUFSIZE);
        size_t read_bytes;
        do {
            read_bytes = fread(chunk.data(), 1, chunk.size(), f.get());
            if (read_bytes > 0) {
                if (httpd_resp_send_chunk(req, chunk.data(), read_bytes) != ESP_OK) {
                    ESP_LOGE(TAG, "File sending failed!");
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
} // daliMQTT