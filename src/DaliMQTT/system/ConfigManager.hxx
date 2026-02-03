// Copyright (c) 2026 Alice-Trade Inc.
// SPDX-License-Identifier: GPL-2.0-or-later

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
        std::string mqtt_base_topic;
        std::string mqtt_ca_cert;

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

        // OTA
        std::string app_ota_url;

        // General
        std::string client_id;

         // Features
        bool hass_discovery_enabled{true};

        bool configured{false};
    };
    enum class ConfigUpdateResult {
        NoUpdate,
        MQTTUpdate,
        WIFIUpdate,
        SystemUpdate
    };
    class ConfigManager {
        public:
            ConfigManager(const ConfigManager&) = delete;
            ConfigManager& operator=(const ConfigManager&) = delete;

            [[nodiscard]] static ConfigManager& Instance() {
                static ConfigManager instance;
                return instance;
            }

            esp_err_t init();

            esp_err_t load();

            esp_err_t saveMainConfig(const AppConfig& new_config);
            esp_err_t saveDaliDeviceIdentificators(const std::string& identificators);
            esp_err_t saveDaliGroupAssignments(const std::string& assignments);
            esp_err_t save();

            esp_err_t resetConfiguredFlag();

            [[nodiscard]] AppConfig getConfig() const;

            [[nodiscard]] std::string getMqttBaseTopic() const;

            void setConfig(const AppConfig& new_config);

            [[nodiscard]] bool isConfigured() const;

            struct cJSON* getSerializedConfig(bool mask_passwords = true) const;

            ConfigUpdateResult updateConfigFromJson(const char* json_str);

        private:
            ConfigManager() = default;
            esp_err_t initSpiffs();
            esp_err_t ensureConfiguredAndCommit(nvs_handle_t handle);
            esp_err_t processConfigUpdate(const std::function<esp_err_t(nvs_handle_t)>& write_action);
            esp_err_t writeBasicSettings(nvs_handle_t handle, const AppConfig& cfg);
            static esp_err_t getString(nvs_handle_t handle, const char* key, std::string& out_value, const char* default_value);
            static esp_err_t getU32(nvs_handle_t handle, const char* key, uint32_t& out_value, uint32_t default_value);
            static esp_err_t setString(nvs_handle_t handle, const char* key, const std::string& value);

            AppConfig config_cache{};
            mutable std::mutex config_mutex{};
            bool initialized{false};
    };
}


#endif //DALIMQTT_CONFIGMANAGER_HXX