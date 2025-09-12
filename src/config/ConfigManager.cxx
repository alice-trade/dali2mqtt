#include "ConfigManager.hxx"
#include "esp_log.h"
#include <cstring>
#include <format>
#include "utils/NvsHandle.hxx"
#include "sdkconfig.h"

namespace daliMQTT
{
    static const char* TAG = "ConfigManager";
    static const char* NVS_NAMESPACE = CONFIG_DALI2MQTT_NVS_NAMESPACE;




    ConfigManager& ConfigManager::getInstance() {
        static ConfigManager instance;
        return instance;
    }

    esp_err_t ConfigManager::init() {
        if (initialized) {
            return ESP_OK;
        }
        esp_err_t ret = nvs_flash_init();
        if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
            ESP_LOGW(TAG, "NVS partition was truncated, erasing and re-initializing...");
            ESP_ERROR_CHECK(nvs_flash_erase());
            ret = nvs_flash_init();
        }
        ESP_ERROR_CHECK(ret);

        ret = initSpiffs();
        ESP_ERROR_CHECK(ret);

        if (ret == ESP_OK) {
            initialized = true;
            ESP_LOGI(TAG, "NVS and SPIFFS initialized successfully.");
        }
        return ret;
    }

    esp_err_t ConfigManager::initSpiffs() {
        ESP_LOGI(TAG, "Initializing SPIFFS");
        esp_vfs_spiffs_conf_t conf = {
          .base_path = "/spiffs",
          .partition_label = CONFIG_DALI2MQTT_WEBUI_SPIFFS_PARTITION_LABEL,
          .max_files = 5,
          .format_if_mount_failed = true
        };

        esp_err_t ret = esp_vfs_spiffs_register(&conf);

        if (ret != ESP_OK) {
            const char* error_msg = esp_err_to_name(ret);
            if (ret == ESP_FAIL) {
                ESP_LOGE(TAG, "Failed to mount or format filesystem");
            } else if (ret == ESP_ERR_NOT_FOUND) {
                ESP_LOGE(TAG, "Failed to find SPIFFS partition");
            } else {
                ESP_LOGE(TAG, "%s", std::format("Failed to initialize SPIFFS ({})", error_msg).c_str());
            }
            return ret;
        }

        return ESP_OK;
    }


    esp_err_t ConfigManager::load() {
        std::lock_guard lock(config_mutex);

        const NvsHandle nvs_handle(NVS_NAMESPACE, NVS_READWRITE);
        if (!nvs_handle) {
            return ESP_FAIL;
        }

        getString(nvs_handle.get(), "wifi_ssid", config_cache.wifi_ssid, "");
        getString(nvs_handle.get(), "wifi_pass", config_cache.wifi_password, "");
        getString(nvs_handle.get(), "mqtt_uri", config_cache.mqtt_uri, "");
        getString(nvs_handle.get(), "mqtt_cid", config_cache.mqtt_client_id, "");
        getString(nvs_handle.get(), "mqtt_base", config_cache.mqtt_base_topic, CONFIG_DALI2MQTT_MQTT_BASE_TOPIC);
        getString(nvs_handle.get(), "http_user", config_cache.http_user, CONFIG_DALI2MQTT_WEBUI_DEFAULT_USER);
        getString(nvs_handle.get(), "http_pass", config_cache.http_pass, CONFIG_DALI2MQTT_WEBUI_DEFAULT_PASS);

        getU32(nvs_handle.get(), "dali_poll", config_cache.dali_poll_interval_ms, CONFIG_DALI2MQTT_DALI_DEFAULT_POLL_INTERVAL_MS);

        uint8_t configured_flag = 0;
        nvs_get_u8(nvs_handle.get(), "configured", &configured_flag);
        config_cache.configured = (configured_flag == 1);

        ESP_LOGI(TAG, "Configuration loaded successfully.");
        return ESP_OK;
    }

    esp_err_t ConfigManager::save() {
        std::lock_guard lock(config_mutex);

        NvsHandle nvs_handle(NVS_NAMESPACE, NVS_READWRITE);
        if (!nvs_handle) {
            return ESP_FAIL;
        }

        esp_err_t err;

        #define SetNVS(func, key, value) \
            err = func(nvs_handle.get(), key, value); \
            if (err != ESP_OK) return err;

        SetNVS(setString, "wifi_ssid", config_cache.wifi_ssid);
        SetNVS(setString, "wifi_pass", config_cache.wifi_password);
        SetNVS(setString, "mqtt_uri", config_cache.mqtt_uri);
        SetNVS(setString, "mqtt_cid", config_cache.mqtt_client_id);
        SetNVS(setString, "mqtt_base", config_cache.mqtt_base_topic);
        SetNVS(setString, "http_user", config_cache.http_user);
        SetNVS(setString, "http_pass", config_cache.http_pass);

        #undef SetNVS

        config_cache.configured = true;
        err = nvs_set_u8(nvs_handle.get(), "configured", 1);
        if (err != ESP_OK) return err;

        err = nvs_commit(nvs_handle.get());
        if (err == ESP_OK) {
            ESP_LOGI(TAG, "Configuration saved successfully.");
        } else {
            ESP_LOGE(TAG, "%s", std::format("Failed to commit NVS changes: {}", esp_err_to_name(err)).c_str());
        }

        return err;
    }

    AppConfig ConfigManager::getConfig() const {
        std::lock_guard lock(config_mutex);
        return config_cache;
    }

    void ConfigManager::setConfig(const AppConfig& new_config) {
        std::lock_guard lock(config_mutex);
        config_cache = new_config;
    }


    bool ConfigManager::isConfigured() const {
        std::lock_guard lock(config_mutex);
        return config_cache.configured;
    }

    esp_err_t ConfigManager::getString(nvs_handle_t handle, const char* key, std::string& out_value, const char* default_value) {
        size_t required_size = 0;
        esp_err_t err = nvs_get_str(handle, key, nullptr, &required_size);

        if (err == ESP_ERR_NVS_NOT_FOUND) {
            if (default_value) {
                out_value = default_value;
                 ESP_LOGW(TAG, "%s", std::format("Key '{}' not found in NVS, using default value: '{}'", key, default_value).c_str());
            } else {
                out_value.clear();
            }
            return ESP_OK;
        }
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "%s", std::format("Error reading key '{}': {}", key, esp_err_to_name(err)).c_str());
            return err;
        }
        if (required_size == 0) {
            out_value.clear();
            return ESP_OK;
        }

        out_value.resize(required_size);
        err = nvs_get_str(handle, key, out_value.data(), &required_size);
        out_value.pop_back();
        return err;
    }

    esp_err_t ConfigManager::setString(nvs_handle_t handle, const char* key, const std::string& value) {
        return nvs_set_str(handle, key, value.c_str());
    }


    esp_err_t ConfigManager::getU32(nvs_handle_t handle, const char* key, uint32_t& out_value, uint32_t default_value) {
        esp_err_t err = nvs_get_u32(handle, key, &out_value);
        if (err == ESP_ERR_NVS_NOT_FOUND) {
            out_value = default_value;
            ESP_LOGW(TAG, "%s", std::format("Key '{}' not found in NVS, using default value: {}", key, default_value).c_str());
            return ESP_OK;
        }
        return err;
    }
}