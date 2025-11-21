#include "SyslogConfig.hxx"
#include "sdkconfig.h"
#include <lwip/sockets.h>
#include <lwip/netdb.h>

namespace daliMQTT {

    static constexpr char TAG[] = "SyslogService";
    static constexpr int SYSLOG_PORT = 514;
    static constexpr size_t MESSAGE_BUFFER_SIZE = 4096;
    static constexpr size_t MAX_LOG_MSG_SIZE = 256;

    static SyslogConfig* g_syslog_instance = nullptr;

    void SyslogConfig::init(const std::string& server_addr) {
        #if CONFIG_LOG_DEFAULT_LEVEL > 3
            ESP_LOGW(TAG, "IDF log level is set to DEBUG or VERBOSE. Syslog is disabled to prevent instability.");
            return;
        #endif

        if (m_initialized) {
            setServer(server_addr);
            return;
        }

        if (!g_syslog_instance) {
            g_syslog_instance = this;
        }

        m_log_buffer = xMessageBufferCreate(MESSAGE_BUFFER_SIZE);
        if (!m_log_buffer) {
            ESP_LOGE(TAG, "Failed to create message buffer. Syslog disabled.");
            return;
        }

        BaseType_t task_created = xTaskCreate(
            syslog_task_entry,
            "syslog_task",
            4096,
            this,
            5,
            &m_task_handle
        );

        if (task_created != pdPASS) {
            ESP_LOGE(TAG, "Failed to create syslog task. Syslog disabled.");
            vMessageBufferDelete(m_log_buffer);
            m_log_buffer = nullptr;
            return;
        }

        m_original_logger = esp_log_set_vprintf(syslog_vprintf_func);
        m_initialized = true;

        setServer(server_addr);
        ESP_LOGI(TAG, "Syslog logger initialized with a background task.");
    }

    void SyslogConfig::setServer(const std::string& server_addr) {
        std::lock_guard<std::recursive_mutex> lock(m_sock_mutex);

        if (m_sock >= 0) {
            close(m_sock);
            m_sock = -1;
        }
        m_server_addr = server_addr;

        if (m_server_addr.empty()) {
            ESP_LOGI(TAG, "Syslog server address is empty, remote logging is paused.");
            return;
        }

        struct addrinfo hints = {};
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_DGRAM;
        struct addrinfo *res;

        int err = getaddrinfo(m_server_addr.c_str(), std::to_string(SYSLOG_PORT).c_str(), &hints, &res);
        if (err != 0 || res == nullptr) {
            ESP_LOGE(TAG, "DNS lookup failed for '%s': err=%d", m_server_addr.c_str(), err);
            return;
        }

        m_sock = socket(res->ai_family, res->ai_socktype, 0);
        if (m_sock < 0) {
            ESP_LOGE(TAG, "Failed to create socket.");
        } else if (connect(m_sock, res->ai_addr, res->ai_addrlen) != 0) {
            ESP_LOGE(TAG, "Failed to connect socket.");
            close(m_sock);
            m_sock = -1;
        }

        freeaddrinfo(res);
    }


    int SyslogConfig::syslog_vprintf_func(const char *format, va_list args) {
        int ret = 0;
        if (g_syslog_instance && g_syslog_instance->m_original_logger) {
            va_list args_copy;
            va_copy(args_copy, args);
            ret = g_syslog_instance->m_original_logger(format, args_copy);
            va_end(args_copy);
        }

        if (!g_syslog_instance || !g_syslog_instance->m_log_buffer) {
            return ret;
        }
        if (xPortInIsrContext()) {
            return ret;
        }
        if (xTaskGetCurrentTaskHandle() == g_syslog_instance->m_task_handle) {
            return ret;
        }

        char msg_buffer[192];

        const int len = vsnprintf(msg_buffer, sizeof(msg_buffer), format, args);

        if (len > 0) {
            const size_t actual_len = (len < sizeof(msg_buffer)) ? len : (sizeof(msg_buffer) - 1);
            xMessageBufferSend(g_syslog_instance->m_log_buffer, msg_buffer, actual_len, 0);
        }

        return ret;
    }

    void SyslogConfig::syslog_task_entry(void* arg) {
        SyslogConfig* self = static_cast<SyslogConfig*>(arg);
        self->syslog_task_runner();
    }

    void SyslogConfig::syslog_task_runner() {
        std::array<char, MAX_LOG_MSG_SIZE + 1> recv_buffer;

        while (true) {
            size_t received_bytes = xMessageBufferReceive(
                m_log_buffer,
                recv_buffer.data(),
                recv_buffer.size() - 1,
                portMAX_DELAY
            );

            if (received_bytes > 0) {
                recv_buffer[received_bytes] = '\0';
                send_log_udp(recv_buffer.data(), received_bytes);
            }
        }
    }

    void SyslogConfig::send_log_udp(const char* message, size_t msg_len) {
        std::lock_guard<std::recursive_mutex> lock(m_sock_mutex);

        if (m_sock < 0 || m_server_addr.empty()) {
            return;
        }

        while (msg_len > 0 && (message[msg_len - 1] == '\n' || message[msg_len - 1] == '\r')) {
            msg_len--;
        }
        if (msg_len == 0) return;

        char packet_buf[MAX_LOG_MSG_SIZE + 32];

        const int header_len = snprintf(packet_buf, sizeof(packet_buf), "<14>dalimqtt: ");
        if (header_len < 0) return;

        const size_t max_msg_payload = sizeof(packet_buf) - header_len - 1;
        size_t copy_len = (msg_len < max_msg_payload) ? msg_len : max_msg_payload;

        memcpy(packet_buf + header_len, message, copy_len);
        packet_buf[header_len + copy_len] = '\0';

        send(m_sock, packet_buf, header_len + copy_len, 0);
    }

} // namespace daliMQTT