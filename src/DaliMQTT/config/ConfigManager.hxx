#ifndef DALIMQTT_CONFIGMANAGER_HXX
#define DALIMQTT_CONFIGMANAGER_HXX

namespace daliMQTT
{
    struct AppConfig {
        // WiFi
        std::string wifi_ssid;
        std::string wifi_password;

        // MQTT
        std::string mqtt_uri;
        std::string mqtt_user;
        std::string mqtt_pass;
        std::string mqtt_client_id;
        std::string mqtt_base_topic;

        // WebUI
        std::string http_domain;
        std::string http_user;
        std::string http_pass;

        // DALI
        uint32_t dali_poll_interval_ms;
        std::string dali_device_identificators;
        std::string dali_group_assignments;

        // Syslog
        std::string syslog_server;
        bool syslog_enabled{false};

        bool configured{false};
    };

    class ConfigManager {
        public:
            ConfigManager(const ConfigManager&) = delete;
            ConfigManager& operator=(const ConfigManager&) = delete;

            // Получение единственного экземпляра класса
            [[nodiscard]] static ConfigManager& getInstance() {
                static ConfigManager instance;
                return instance;
            }

            // Инициализация NVS и SPIFFS
            esp_err_t init();

            // Загрузка конфигурации из NVS
            esp_err_t load();

            // Гранулярные методы сохранения
            esp_err_t saveMainConfig(const AppConfig& new_config);
            esp_err_t saveDaliDeviceIdentificators(const std::string& identificators);
            esp_err_t saveDaliGroupAssignments(const std::string& assignments);
            // Сохранение конфигурации в NVS
            esp_err_t save();

            // Получение текущей конфигурации
            [[nodiscard]] AppConfig getConfig() const;

            // Установка новой конфигурации
            void setConfig(const AppConfig& new_config);

            // Проверка, была ли система сконфигурирована
            [[nodiscard]] bool isConfigured() const;

        private:
            ConfigManager() = default;
            esp_err_t initSpiffs();
            esp_err_t ensureConfiguredAndCommit(nvs_handle_t handle);

            static esp_err_t getString(nvs_handle_t handle, const char* key, std::string& out_value, const char* default_value);
            static esp_err_t getU32(nvs_handle_t handle, const char* key, uint32_t& out_value, uint32_t default_value);
            static esp_err_t getU64(nvs_handle_t handle, const char* key, uint64_t& out_value, uint64_t default_value);
            static esp_err_t setString(nvs_handle_t handle, const char* key, const std::string& value);

            AppConfig config_cache;
            mutable std::mutex config_mutex;
            bool initialized{false};
    };
}


#endif //DALIMQTT_CONFIGMANAGER_HXX