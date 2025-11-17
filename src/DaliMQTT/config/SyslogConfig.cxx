#include "SyslogConfig.hxx"
#include "sdkconfig.h"
#include <lwip/sockets.h>
#include <lwip/netdb.h>
#include <cstdarg>
#include <cstdio>
#include <cstring>

namespace daliMQTT {

    static constexpr char TAG[] = "SyslogService";
    static constexpr int SYSLOG_PORT = 514; // TO BE REWRITTEN TO DYNAMIC PORT

    static constexpr int LOG_QUEUE_LENGTH = 30;
    static constexpr int MAX_LOG_MESSAGE_SIZE = 256;

    static SyslogConfig* g_syslog_instance = nullptr;

    void SyslogConfig::init(const std::string& server_addr) {
        if (m_initialized || server_addr.empty()) {
            if (server_addr.empty()) {
                stop_service();
            }
            return;
        }


        m_log_queue = xQueueCreate(LOG_QUEUE_LENGTH, sizeof(char*));
        if (!m_log_queue) {
            ESP_LOGE(TAG, "Failed to create log queue.");
            return;
        }

        xTaskCreate(
            syslog_sender_task,
            "syslog_sender",
            3072,
            this,
            5,
            &m_log_task_handle
        );
        if (!m_log_task_handle) {
            ESP_LOGE(TAG, "Failed to create syslog sender task.");
            vQueueDelete(m_log_queue);
            m_log_queue = nullptr;
            return;
        }

        m_server_addr = server_addr;
        struct addrinfo hints = {};
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_DGRAM;
        struct addrinfo *res;

        int err = getaddrinfo(m_server_addr.c_str(), std::to_string(SYSLOG_PORT).c_str(), &hints, &res);
        if (err != 0 || res == nullptr) {
            vTaskDelete(m_log_task_handle);
            m_log_task_handle = nullptr;
            vQueueDelete(m_log_queue);
            m_log_queue = nullptr;
            return;
        }

        m_sock = socket(res->ai_family, res->ai_socktype, 0);
        if (m_sock >= 0) {
            connect(m_sock, res->ai_addr, res->ai_addrlen);
        }
        freeaddrinfo(res);

        g_syslog_instance = this;
        m_initialized = true;
        m_original_logger = esp_log_set_vprintf(syslog_vprintf_func);

        ESP_LOGI(TAG, "Syslog service initialized for server %s", m_server_addr.c_str());
    }

    void SyslogConfig::start_service() {
        if (m_initialized) return;

        m_log_queue = xQueueCreate(LOG_QUEUE_LENGTH, sizeof(char*));
        if (!m_log_queue) {
            ESP_LOGE(TAG, "Failed to create log queue.");
            return;
        }

        g_syslog_instance = this;
        m_original_logger = esp_log_set_vprintf(syslog_vprintf_func);

        xTaskCreate(
            syslog_sender_task,
            "syslog_sender",
            3072,
            this,
            5,
            &m_log_task_handle
        );

        m_initialized = true;
        ESP_LOGI(TAG, "Syslog logger task and queue created.");
    }

    void SyslogConfig::stop_service() {
        if (!m_initialized) return;

        std::lock_guard<std::recursive_mutex> lock(m_mutex);

        if (m_original_logger) {
            esp_log_set_vprintf(m_original_logger);
            m_original_logger = nullptr;
        }

        if (m_log_task_handle) {
            vTaskDelete(m_log_task_handle);
            m_log_task_handle = nullptr;
        }

        if (m_log_queue) {
            char* msg_ptr;
            while(xQueueReceive(m_log_queue, &msg_ptr, 0) == pdTRUE) {
                free(msg_ptr);
            }
            vQueueDelete(m_log_queue);
            m_log_queue = nullptr;
        }

        // Закрываем сокет
        if (m_sock >= 0) {
            close(m_sock);
            m_sock = -1;
        }

        m_initialized = false;
        ESP_LOGI(TAG, "Syslog service stopped.\n");
    }


    void SyslogConfig::udp_log_write(const char *message) {
        std::lock_guard<std::recursive_mutex> lock(m_mutex);
        if (m_sock < 0 || m_server_addr.empty()) {
            return;
        }

        char syslog_buf[MAX_LOG_MESSAGE_SIZE + 50];
        const int len = snprintf(syslog_buf, sizeof(syslog_buf), "<14>dalimqtt: %s", message);

        send(m_sock, syslog_buf, len, 0);
    }

    int SyslogConfig::syslog_vprintf_func(const char *format, va_list args) {
        const int ret = g_syslog_instance->m_original_logger(format, args);

        va_list args_for_syslog;
        va_copy(args_for_syslog, args);

        char buffer[MAX_LOG_MESSAGE_SIZE];
        vsnprintf(buffer, sizeof(buffer), format, args_for_syslog);
        va_end(args_for_syslog);

        char* end = strstr(buffer, "\r\n");
        if (end) {
            *end = '\0';
        }

        char* message_copy = strdup(buffer);
        if (!message_copy) {
            return ret;
        }


        if (xQueueSend(g_syslog_instance->m_log_queue, &message_copy, 0) != pdTRUE) {
            free(message_copy);
        }

        return ret;
    }

    void SyslogConfig::syslog_sender_task(void *pvParameters) {
        SyslogConfig *self = static_cast<SyslogConfig*>(pvParameters);
        char *log_message = nullptr;

        while (true) {
            if (xQueueReceive(self->m_log_queue, &log_message, portMAX_DELAY) == pdTRUE) {
                if (log_message) {
                    self->udp_log_write(log_message);
                    free(log_message);
                }
            }
        }
    }

} // namespace daliMQTT

