// Copyright (c) 2026 Alice-Trade Inc.
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef DALIMQTT_APIHANDLERS_HXX
#define DALIMQTT_APIHANDLERS_HXX

#include <esp_http_server.h>

namespace daliMQTT {
struct HttpChunkStream {
    httpd_req_t* req{};
    char buffer[256]{};
    size_t index{0};

    size_t write(uint8_t c) {
        buffer[index++] = static_cast<char>(c);
        if (index == sizeof(buffer)) {
            flush();
        }
        return 1;
    }

    size_t write(const uint8_t* s, size_t n) {
        for (size_t i = 0; i < n; ++i) {
            write(s[i]);
        }
        return n;
    }

    void flush() {
        if (index > 0) {
            httpd_resp_send_chunk(req, buffer, index);
            index = 0;
        }
    }
};
struct ApiHandlers {
    static esp_err_t getConfig(httpd_req_t* req);
    static esp_err_t setConfig(httpd_req_t* req);
    static esp_err_t getInfo(httpd_req_t* req);

    static esp_err_t getDaliDevices(httpd_req_t* req);
    static esp_err_t controlDaliDevice(httpd_req_t* req);
    static esp_err_t scanDaliBus(httpd_req_t* req);
    static esp_err_t initializeDaliBus(httpd_req_t* req);
    static esp_err_t initializeDaliInputs(httpd_req_t* req);
    static esp_err_t getDaliStatus(httpd_req_t* req);

    static esp_err_t getDaliNames(httpd_req_t* req);
    static esp_err_t setDaliNames(httpd_req_t* req);

    static esp_err_t getDaliGroups(httpd_req_t* req);
    static esp_err_t setDaliGroups(httpd_req_t* req);
    static esp_err_t refreshDaliGroups(httpd_req_t* req);

    static esp_err_t getDaliScenes(httpd_req_t* req);
    static esp_err_t setDaliScenes(httpd_req_t* req);

    static esp_err_t triggerOtaCheck(httpd_req_t* req);
    static esp_err_t triggerOtaInstall(httpd_req_t* req);
    static esp_err_t uploadOtaBin(httpd_req_t* req);

};

} // namespace daliMQTT

#endif // DALIMQTT_APIHANDLERS_HXX