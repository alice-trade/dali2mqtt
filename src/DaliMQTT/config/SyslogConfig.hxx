#ifndef DALIMQTT_SYSLOGCONFIG_HXX
#define DALIMQTT_SYSLOGCONFIG_HXX

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
            void setServer(const std::string& server_addr);

        private:
            SyslogConfig() = default;
            static int syslog_vprintf_func(const char *format, va_list args);
            void udp_log_write(const char *message);

            std::string m_server_addr;
            int m_sock {-1};
            vprintf_like_t m_original_logger {nullptr};
            std::mutex m_mutex;
            bool m_initialized {false};
    };
} // daliMQTT

#endif //DALIMQTT_SYSLOGCONFIG_HXX