#ifndef DALIMQTT_CONFIGMANAGER_HXX
#define DALIMQTT_CONFIGMANAGER_HXX


#include <string>
#include <cstdint>
#include <optional>
#include <mutex>

#include "nvs_flash.h"
#include "nvs.h"
namespace daliMQTT
{
    struct AppConfig {
        // WiFi
        std::string wifi_ssid;
        std::string wifi_password;

        // MQTT
        std::string mqtt_uri;
        std::string mqtt_client_id;
        std::string mqtt_base_topic;

        // DALI
        uint32_t dali_poll_interval_ms;

        bool configured{false};
    };

    class ConfigManager {
        public:
            ConfigManager(const ConfigManager&) = delete;
            ConfigManager& operator=(const ConfigManager&) = delete;

            // Получение единственного экземпляра класса
            [[nodiscard]] static ConfigManager& getInstance();

            // Инициализация NVS
            esp_err_t init();

            // Загрузка конфигурации из NVS
            esp_err_t load();

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
            static esp_err_t getString(nvs_handle_t handle, const char* key, std::string& out_value, const char* default_value);
            static esp_err_t getU32(nvs_handle_t handle, const char* key, uint32_t& out_value, uint32_t default_value);
            static esp_err_t setString(nvs_handle_t handle, const char* key, const std::string& value);

            AppConfig config_cache;
            mutable std::mutex config_mutex;
            bool initialized{false};
        };
}


#endif //DALIMQTT_CONFIGMANAGER_HXX