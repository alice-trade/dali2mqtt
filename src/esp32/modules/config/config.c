#include "config.h"
#include "nvs.h"
#include "esp_log.h"
#include "string.h"
#include "mqtt.h"
#include "sdkconfig.h"
#include "app_config.h"

static const char *TAG = "CONFIG";

// --- Внутренние вспомогательные функции для NVS ---
static esp_err_t load_u32_from_nvs(nvs_handle_t handle, const char* key, uint32_t* value, uint32_t default_val) {
    esp_err_t err = nvs_get_u32(handle, key, value);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGW(TAG, "NVS: Key '%s' not found, using default: %lu", key, default_val);
        *value = default_val;
        err = nvs_set_u32(handle, key, default_val);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "NVS: Failed to set default u32 for key '%s': %s", key, esp_err_to_name(err));
        } else {
            ESP_LOGI(TAG, "NVS: Set default for key '%s' to %lu", key, default_val);
            err = ESP_OK; // Успешно установили дефолт
        }
    } else if (err != ESP_OK) {
        ESP_LOGE(TAG, "NVS: Failed to get u32 for key '%s' (%s). Using default %lu.", key, esp_err_to_name(err), default_val);
        *value = default_val;
    }
    return err;
}

// --- Публичные функции ---

esp_err_t config_manager_init(void) {
    ESP_LOGI(TAG, "Initializing configuration...");

    // Копируем параметры WiFi и MQTT из sdkconfig в g_app_config
    strncpy(g_app_config.wifi_ssid, CONFIG_DALI2MQTT_WIFI_DEFAULT_SSID, sizeof(g_app_config.wifi_ssid) -1);
    g_app_config.wifi_ssid[sizeof(g_app_config.wifi_ssid) - 1] = '\0';

    strncpy(g_app_config.wifi_pass, CONFIG_DALI2MQTT_WIFI_DEFAULT_PASS, sizeof(g_app_config.wifi_pass) -1);
    g_app_config.wifi_pass[sizeof(g_app_config.wifi_pass) - 1] = '\0';

    strncpy(g_app_config.mqtt_uri, CONFIG_DALI2MQTT_MQTT_DEFAULT_URI, sizeof(g_app_config.mqtt_uri) -1);
    g_app_config.mqtt_uri[sizeof(g_app_config.mqtt_uri) - 1] = '\0';

    ESP_LOGI(TAG, "Compile-time WiFi/MQTT: SSID='%s', MQTT URI='%s'", g_app_config.wifi_ssid, g_app_config.mqtt_uri);

    // Интервал опроса DALI читаем из NVS, дефолт из sdkconfig
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(CONFIG_DALI2MQTT_NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS namespace '%s': %s. Using poll interval default from sdkconfig.",
                 CONFIG_DALI2MQTT_NVS_NAMESPACE, esp_err_to_name(err));
        g_app_config.poll_interval_ms = CONFIG_DALI2MQTT_DALI_DEFAULT_POLL_INTERVAL_MS;
        return err; // Возвращаем ошибку открытия NVS
    }

    ESP_LOGI(TAG, "Loading DALI poll interval from NVS (namespace: %s)...", CONFIG_DALI2MQTT_NVS_NAMESPACE);
    err = load_u32_from_nvs(nvs_handle, NVS_KEY_POLL_INTERVAL, &g_app_config.poll_interval_ms, CONFIG_DALI2MQTT_DALI_DEFAULT_POLL_INTERVAL_MS);

    ESP_LOGI(TAG, "Runtime DALI Config: Poll Interval=%lu ms", g_app_config.poll_interval_ms);
    ESP_LOGI(TAG, "Compile-time DALI Masks: Groups=0x%04X, Devices=0x%08lX%08lX",
             CONFIG_DALI2MQTT_DALI_DEFAULT_POLL_GROUPS_MASK,
             (uint32_t)(CONFIG_DALI2MQTT_DALI_DEFAULT_POLL_GROUPS_MASK),
             (uint32_t)(CONFIG_DALI2MQTT_DALI_DEFAULT_POLL_DEVICES_MASK_LO));


    esp_err_t commit_err = nvs_commit(nvs_handle);
    if (commit_err != ESP_OK) {
        ESP_LOGE(TAG, "NVS commit failed after poll interval load: %s", esp_err_to_name(commit_err));
        if (err == ESP_OK) err = commit_err;
    }

    nvs_close(nvs_handle);
    return err;
}

esp_err_t config_manager_save(void) {
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(CONFIG_DALI2MQTT_NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS namespace '%s' for saving: %s", CONFIG_DALI2MQTT_NVS_NAMESPACE, esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "Saving DALI poll interval to NVS...");
    err = nvs_set_u32(nvs_handle, NVS_KEY_POLL_INTERVAL, g_app_config.poll_interval_ms);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to save %s: %s", NVS_KEY_POLL_INTERVAL, esp_err_to_name(err));
    }

    esp_err_t commit_err = nvs_commit(nvs_handle);
    if (commit_err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to commit NVS DALI poll interval: %s", esp_err_to_name(commit_err));
        if (err == ESP_OK) err = commit_err;
    }

    nvs_close(nvs_handle);

    if (err == ESP_OK) {
        mqtt_publish_config();
    } else {
        ESP_LOGW(TAG, "DALI poll interval saved with errors.");
    }
    return err;
}

esp_err_t config_manager_set_poll_interval(uint32_t interval_ms) {
    const uint32_t min_interval = 100;
    if (interval_ms < min_interval) {
       ESP_LOGW(TAG, "Poll interval %lu ms is too low, setting to %lu ms", interval_ms, min_interval);
       interval_ms = min_interval;
    }
    if (g_app_config.poll_interval_ms != interval_ms) {
        ESP_LOGI(TAG, "Setting poll interval to %lu ms", interval_ms);
        g_app_config.poll_interval_ms = interval_ms;
        return config_manager_save();
    }
    return ESP_OK;
}