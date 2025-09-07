#include "ConfigManager.hxx"
#include "esp_log.h"
#include <cstring>

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
            if (ret == ESP_FAIL) {
                ESP_LOGE(TAG, "Failed to mount or format filesystem");
            } else if (ret == ESP_ERR_NOT_FOUND) {
                ESP_LOGE(TAG, "Failed to find SPIFFS partition");
            } else {
                ESP_LOGE(TAG, "Failed to initialize SPIFFS (%s)", esp_err_to_name(ret));
            }
            return ret;
        }

        return 0;
    }


    esp_err_t ConfigManager::load() {
        std::lock_guard<std::mutex> lock(config_mutex);

        nvs_handle_t nvs_handle;
        esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Error (%s) opening NVS handle!", esp_err_to_name(err));
            return err;
        }

        getString(nvs_handle, "wifi_ssid", config_cache.wifi_ssid, CONFIG_DALI2MQTT_WIFI_DEFAULT_SSID);
        getString(nvs_handle, "wifi_pass", config_cache.wifi_password, CONFIG_DALI2MQTT_WIFI_DEFAULT_PASS);
        getString(nvs_handle, "mqtt_uri", config_cache.mqtt_uri, CONFIG_DALI2MQTT_MQTT_DEFAULT_URI);
        getString(nvs_handle, "mqtt_cid", config_cache.mqtt_client_id, CONFIG_DALI2MQTT_MQTT_DEFAULT_CLIENT_ID);
        getString(nvs_handle, "mqtt_base", config_cache.mqtt_base_topic, CONFIG_DALI2MQTT_MQTT_BASE_TOPIC);
        getString(nvs_handle, "http_user", config_cache.http_user, CONFIG_DALI2MQTT_WEBUI_DEFAULT_USER);
        getString(nvs_handle, "http_pass", config_cache.http_pass, CONFIG_DALI2MQTT_WEBUI_DEFAULT_PASS);

        getU32(nvs_handle, "dali_poll", config_cache.dali_poll_interval_ms, CONFIG_DALI2MQTT_DALI_DEFAULT_POLL_INTERVAL_MS);

        uint8_t configured_flag = 0;
        nvs_get_u8(nvs_handle, "configured", &configured_flag);
        config_cache.configured = (configured_flag == 1);

        nvs_close(nvs_handle);
        ESP_LOGI(TAG, "Configuration loaded successfully.");
        return ESP_OK;
    }

    esp_err_t ConfigManager::save() {
        std::lock_guard<std::mutex> lock(config_mutex);

        nvs_handle_t nvs_handle;
        esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Error (%s) opening NVS handle!", esp_err_to_name(err));
            return err;
        }

        err = setString(nvs_handle, "wifi_ssid", config_cache.wifi_ssid); if(err != ESP_OK) goto cleanup;
        err = setString(nvs_handle, "wifi_pass", config_cache.wifi_password); if(err != ESP_OK) goto cleanup;
        err = setString(nvs_handle, "mqtt_uri", config_cache.mqtt_uri); if(err != ESP_OK) goto cleanup;
        err = setString(nvs_handle, "mqtt_cid", config_cache.mqtt_client_id); if(err != ESP_OK) goto cleanup;
        err = setString(nvs_handle, "mqtt_base", config_cache.mqtt_base_topic); if(err != ESP_OK) goto cleanup;
        err = setString(nvs_handle, "http_user", config_cache.http_user); if(err != ESP_OK) goto cleanup;
        err = setString(nvs_handle, "http_pass", config_cache.http_pass); if(err != ESP_OK) goto cleanup;


        config_cache.configured = true;
        err = nvs_set_u8(nvs_handle, "configured", 1);
        if(err != ESP_OK) goto cleanup;

        err = nvs_commit(nvs_handle);
        if (err == ESP_OK) {
            ESP_LOGI(TAG, "Configuration saved successfully.");
        } else {
            ESP_LOGE(TAG, "Failed to commit NVS changes: %s", esp_err_to_name(err));
        }

    cleanup:
        nvs_close(nvs_handle);
        return err;
    }


    AppConfig ConfigManager::getConfig() const {
        std::lock_guard<std::mutex> lock(config_mutex);
        return config_cache;
    }

    void ConfigManager::setConfig(const AppConfig& new_config) {
        std::lock_guard<std::mutex> lock(config_mutex);
        config_cache = new_config;
    }


    bool ConfigManager::isConfigured() const {
        std::lock_guard<std::mutex> lock(config_mutex);
        return config_cache.configured;
    }

    esp_err_t ConfigManager::getString(nvs_handle_t handle, const char* key, std::string& out_value, const char* default_value) {
        size_t required_size = 0;
        esp_err_t err = nvs_get_str(handle, key, nullptr, &required_size);
        if (err == ESP_ERR_NVS_NOT_FOUND) {
            out_value = default_value;
            ESP_LOGW(TAG, "Key '%s' not found in NVS, using default value: '%s'", key, default_value);
            return ESP_OK;
        } else if (err != ESP_OK) {
            ESP_LOGE(TAG, "Error reading key '%s': %s", key, esp_err_to_name(err));
            return err;
        }

        out_value.resize(required_size);
        err = nvs_get_str(handle, key, &out_value[0], &required_size);
        if(required_size > 0) out_value.pop_back();
        return err;
    }

    esp_err_t ConfigManager::setString(nvs_handle_t handle, const char* key, const std::string& value) {
        return nvs_set_str(handle, key, value.c_str());
    }


    esp_err_t ConfigManager::getU32(nvs_handle_t handle, const char* key, uint32_t& out_value, uint32_t default_value) {
        esp_err_t err = nvs_get_u32(handle, key, &out_value);
        if (err == ESP_ERR_NVS_NOT_FOUND) {
            out_value = default_value;
            ESP_LOGW(TAG, "Key '%s' not found in NVS, using default value: %lu", key, (unsigned long)default_value);
            return ESP_OK;
        }
        return err;
    }
}