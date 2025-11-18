#include "SyslogConfig.hxx"
#include "sdkconfig.h"
#include <lwip/sockets.h>
#include <lwip/netdb.h>

namespace daliMQTT {

    static constexpr char TAG[] = "SyslogService";
    static constexpr int SYSLOG_PORT = 514;
    static constexpr int LOG_QUEUE_LENGTH = 32;
    static constexpr int MAX_LOG_MSG_SIZE = 256;

    static SyslogConfig* g_syslog_instance = nullptr;

    void SyslogConfig::init(const std::string& server_addr) {
    #if CONFIG_LOG_DEFAULT_LEVEL > ESP_LOG_INFO
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

        m_log_queue = xQueueCreate(LOG_QUEUE_LENGTH, sizeof(char*));
        if (!m_log_queue) {
            ESP_LOGE(TAG, "Failed to create log queue. Syslog disabled.");
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
            vQueueDelete(m_log_queue);
            m_log_queue = nullptr;
            return;
        }

        m_original_logger = esp_log_set_vprintf(syslog_vprintf_func);
        m_initialized = true;
        ESP_LOGI(TAG, "Syslog logger initialized with a background task.");

        setServer(server_addr);
    }

    void SyslogConfig::setServer(const std::string& server_addr) {
        std::lock_guard<std::recursive_mutex> lock(m_mutex);

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
        if (m_sock >= 0) {
            ESP_LOGI(TAG, "Syslog server set to %s", m_server_addr.c_str());
        }
    }


    int SyslogConfig::syslog_vprintf_func(const char *format, va_list args) {
        va_list args_copy;
        va_copy(args_copy, args);
        int ret = g_syslog_instance->m_original_logger(format, args_copy);
        va_end(args_copy);

        if (!g_syslog_instance || !g_syslog_instance->m_log_queue) {
            return ret;
        }

        char* msg_buffer = static_cast<char*>(malloc(MAX_LOG_MSG_SIZE));
        if (!msg_buffer) {
            return ret;
        }

        vsnprintf(msg_buffer, MAX_LOG_MSG_SIZE, format, args);

        if (char* end = strstr(msg_buffer, "\r\n")) {
            *end = '\0';
        }

        if (xQueueSend(g_syslog_instance->m_log_queue, &msg_buffer, 0) != pdPASS) {
            free(msg_buffer);
        }
        return ret;
    }

    void SyslogConfig::syslog_task_entry(void* arg) {
        SyslogConfig* self = static_cast<SyslogConfig*>(arg);
        self->syslog_task_runner();
    }

    void SyslogConfig::syslog_task_runner() {
        char* message_buffer = nullptr;
        while (true) {
            if (xQueueReceive(m_log_queue, &message_buffer, portMAX_DELAY) == pdPASS) {
                if (message_buffer) {
                    send_log_udp(message_buffer);
                    free(message_buffer);
                }
            }
        }
    }

    void SyslogConfig::send_log_udp(const char* message) {
        std::lock_guard<std::recursive_mutex> lock(m_mutex);

        if (m_sock < 0 || m_server_addr.empty()) {
            return;
        }

        char syslog_buf[MAX_LOG_MSG_SIZE + 32];
        int len = snprintf(syslog_buf, sizeof(syslog_buf), "<14>dalimqtt: %s", message);

        send(m_sock, syslog_buf, len, 0);
    }

} // namespace daliMQTT