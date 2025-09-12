#ifndef DALIMQTT_WEBUI_HXX
#define DALIMQTT_WEBUI_HXX
#include "esp_http_server.h"

namespace daliMQTT
{
    class WebUI {
    public:
        WebUI(const WebUI&) = delete;
        WebUI& operator=(const WebUI&) = delete;

        static WebUI& getInstance();

        esp_err_t start();
        [[nodiscard]] esp_err_t stop() const;

    private:
        WebUI() = default;

        static esp_err_t apiGetConfigHandler(httpd_req_t *req);
        static esp_err_t apiSetConfigHandler(httpd_req_t *req);
        static esp_err_t apiGetInfoHandler(httpd_req_t *req);
        static esp_err_t staticFileGetHandler(httpd_req_t *req);

        static esp_err_t checkAuth(httpd_req_t *req);
        static void set_content_type_from_file(httpd_req_t *req, const char *filepath);

        httpd_handle_t server_handle{nullptr};
    };
} // daliMQTT

#endif //DALIMQTT_WEBUI_HXX