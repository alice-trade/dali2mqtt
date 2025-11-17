#ifndef DALIMQTT_SYSLOGCONFIG_HXX
#define DALIMQTT_SYSLOGCONFIG_HXX

#include <mutex>

namespace daliMQTT {
    class SyslogConfig {
        public:
            SyslogConfig(const SyslogConfig&) = delete;
            SyslogConfig& operator=(const SyslogConfig&) = delete;

            static SyslogConfig& getInstance() {
                static SyslogConfig instance;
                return instance;
            }

            void init(const std::string& server_addr);
        private:
            SyslogConfig() = default;
            static int syslog_vprintf_func(const char *format, va_list args);
            [[noreturn]] static void syslog_sender_task(void *pvParameters);
            void udp_log_write(const char *message);

            void start_service();
            void stop_service();

            std::string m_server_addr;
            int m_sock {-1};
            vprintf_like_t m_original_logger {nullptr};
            std::recursive_mutex m_mutex;
            bool m_initialized {false};

            QueueHandle_t m_log_queue{nullptr};
            TaskHandle_t m_log_task_handle{nullptr};
    };
} // daliMQTT

#endif //DALIMQTT_SYSLOGCONFIG_HXX