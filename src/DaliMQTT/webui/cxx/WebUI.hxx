#ifndef DALIMQTT_WEBUI_HXX
#define DALIMQTT_WEBUI_HXX
#include <esp_http_server.h>

namespace daliMQTT
{
    class WebUI {
    public:
        WebUI(const WebUI&) = delete;
        WebUI& operator=(const WebUI&) = delete;

        static WebUI& getInstance() {
            static WebUI instance;
            return instance;
        }

        esp_err_t start();
        [[nodiscard]] esp_err_t stop() const;

    private:
        WebUI() = default;

        // API handlers
        struct api
        {
            static esp_err_t GetConfigHandler(httpd_req_t *req);
            static esp_err_t SetConfigHandler(httpd_req_t *req);
            static esp_err_t GetInfoHandler(httpd_req_t *req);
            static esp_err_t DaliGetDevicesHandler(httpd_req_t *req);
            static esp_err_t DaliScanHandler(httpd_req_t *req);
            static esp_err_t DaliInitializeHandler(httpd_req_t *req);
        };

        // File handler
        static esp_err_t staticFileGetHandler(httpd_req_t *req);

        // Helpers

        static esp_err_t checkAuth(httpd_req_t *req);
        static void set_content_type_from_file(httpd_req_t *req, const char *filepath);

        httpd_handle_t server_handle{nullptr};
    };
} // daliMQTT

#endif //DALIMQTT_WEBUI_HXX