#include "SyslogConfig.hxx"
#include "sdkconfig.h"
#include <lwip/sockets.h>
#include <lwip/netdb.h>
#include <cstdarg>
#include <cstdio>
#include <cstring>

namespace daliMQTT {

static constexpr char TAG[] = "Syslog";
static constexpr int SYSLOG_PORT = 514;

static SyslogConfig* g_syslog_instance = nullptr;

void SyslogConfig::init(const std::string& server_addr) {
    if (m_initialized) {
        setServer(server_addr);
        return;
    }

    if (!g_syslog_instance) {
        g_syslog_instance = this;
    }

    setServer(server_addr);

    m_original_logger = esp_log_set_vprintf(syslog_vprintf_func);
    m_initialized = true;
    ESP_LOGI(TAG, "Syslog logger initialized.");
}

void SyslogConfig::setServer(const std::string& server_addr) {
    std::lock_guard<std::mutex> lock(m_mutex);

    if (m_sock >= 0) {
        close(m_sock);
        m_sock = -1;
    }
    m_server_addr = server_addr;

    if (m_server_addr.empty()) {
        ESP_LOGI(TAG, "Syslog server address is empty, disabling remote logging.");
        if (m_initialized && m_original_logger) {
             esp_log_set_vprintf(m_original_logger);
             m_original_logger = nullptr;
             m_initialized = false;
             ESP_LOGI(TAG, "Restored default logger.");
        }
        return;
    }

    if (!m_initialized) {
        init(server_addr);
        return;
    }

    struct addrinfo hints = {};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    struct addrinfo *res;

    int err = getaddrinfo(m_server_addr.c_str(), std::to_string(SYSLOG_PORT).c_str(), &hints, &res);
    if (err != 0 || res == nullptr) {
        ESP_LOGE(TAG, "DNS lookup failed for syslog server '%s': err=%d", m_server_addr.c_str(), err);
        return;
    }

    m_sock = socket(res->ai_family, res->ai_socktype, 0);
    if (m_sock < 0) {
        ESP_LOGE(TAG, "Failed to create socket for syslog.");
        freeaddrinfo(res);
        return;
    }

    if (connect(m_sock, res->ai_addr, res->ai_addrlen) != 0) {
        ESP_LOGE(TAG, "Failed to connect to syslog server.");
        close(m_sock);
        m_sock = -1;
    }

    freeaddrinfo(res);
    ESP_LOGI(TAG, "Syslog server set to %s", m_server_addr.c_str());
}

void SyslogConfig::udp_log_write(const char *message) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_sock < 0 || m_server_addr.empty()) {
        return;
    }

    // Формат RFC 3164: <PRI>HOSTNAME TAG: MESSAGE
    // PRI 14 = facility 1 (user-level), severity 6 (informational)
    char syslog_buf[512];
    int len = snprintf(syslog_buf, sizeof(syslog_buf), "<14>dalimqtt: %s", message);

    if (send(m_sock, syslog_buf, len, 0) < 0) {
    }
}

int SyslogConfig::syslog_vprintf_func(const char *format, va_list args) {
    if (!g_syslog_instance || !g_syslog_instance->m_original_logger) {
        return vprintf(format, args);
    }

    va_list args_for_serial;
    va_list args_for_syslog;
    va_copy(args_for_serial, args);
    va_copy(args_for_syslog, args);

    int ret = g_syslog_instance->m_original_logger(format, args_for_serial);
    va_end(args_for_serial);

    char buffer[256];
    vsnprintf(buffer, sizeof(buffer), format, args_for_syslog);
    va_end(args_for_syslog);

    char* end = strstr(buffer, "\r\n");
    if (end) {
        *end = '\0';
    }

    g_syslog_instance->udp_log_write(buffer);
    return ret;
}

} // namespace daliMQTT