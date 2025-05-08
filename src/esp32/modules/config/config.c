//
// Created by danil on 01.05.2025.
//

#include "config.h"
#include "nvs.h"
#include "esp_log.h"
#include "string.h"
#include "mqtt.h" // Для публикации текущей конфигурации

static const char *TAG = "CONFIG_MGR";


// --- Внутренние вспомогательные функции ---
static uint64_t combine_u32_to_u64(uint32_t high, uint32_t low) {
    return ((uint64_t)high << 32) | low;
}

// Загрузка строки из NVS с значением по умолчанию

// Загрузка uint32_t из NVS с значением по умолчанию
static esp_err_t load_u32_from_nvs(nvs_handle_t handle, const char* key, uint32_t* value, uint32_t default_val) {
    esp_err_t err = nvs_get_u32(handle, key, value);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGW(TAG, "NVS: Key '%s' not found, using default: %lu", key, (unsigned long)default_val);
        *value = default_val;
        esp_err_t set_err = nvs_set_u32(handle, key, default_val);
        if (set_err != ESP_OK) {
            ESP_LOGE(TAG, "NVS: Failed to set default u32 for key '%s' (%s)", key, esp_err_to_name(set_err));
            err = set_err;
        } else {
            err = ESP_OK;
        }
    } else if (err != ESP_OK) {
        ESP_LOGE(TAG, "NVS: Failed to get u32 for key '%s' (%s)", key, esp_err_to_name(err));
        *value = default_val;
    }
    return err;
}

// Загрузка uint16_t из NVS с значением по умолчанию
static esp_err_t load_u16_from_nvs(nvs_handle_t handle, const char* key, uint16_t* value, uint16_t default_val) {
    esp_err_t err = nvs_get_u16(handle, key, value);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGW(TAG, "NVS: Key '%s' not found, using default: %u", key, default_val);
        *value = default_val;
        esp_err_t set_err = nvs_set_u16(handle, key, default_val);
        if (set_err != ESP_OK) {
            ESP_LOGE(TAG, "NVS: Failed to set default u16 for key '%s' (%s)", key, esp_err_to_name(set_err));
            err = set_err;
        } else {
            err = ESP_OK;
        }
    } else if (err != ESP_OK) {
        ESP_LOGE(TAG, "NVS: Failed to get u16 for key '%s' (%s)", key, esp_err_to_name(err));
        *value = default_val;
    }
    return err;
}

// Загрузка uint64_t из NVS с значением по умолчанию
static esp_err_t load_u64_from_nvs(nvs_handle_t handle, const char* key, uint64_t* value, uint64_t default_val) {
    esp_err_t err = nvs_get_u64(handle, key, value);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGW(TAG, "NVS: Key '%s' not found, using default: 0x%llX", key, (unsigned long long)default_val);
        *value = default_val;
        esp_err_t set_err = nvs_set_u64(handle, key, default_val);
        if (set_err != ESP_OK) {
            ESP_LOGE(TAG, "NVS: Failed to set default u64 for key '%s' (%s)", key, esp_err_to_name(set_err));
            err = set_err;
        } else {
            err = ESP_OK;
        }
    } else if (err != ESP_OK) {
        ESP_LOGE(TAG, "NVS: Failed to get u64 for key '%s' (%s)", key, esp_err_to_name(err));
        *value = default_val;
    }
    return err;
}

// --- Публичные функции ---

esp_err_t config_manager_init(void) {
        // Устанавливаем значения по умолчанию в g_app_config в случае ошибки
        strncpy(g_app_config.wifi_ssid, CONFIG_DALI2MQTT_WIFI_DEFAULT_SSID, sizeof(g_app_config.wifi_ssid) - 1);
        g_app_config.wifi_ssid[sizeof(g_app_config.wifi_ssid) - 1] = '\0';
        strncpy(g_app_config.wifi_pass, CONFIG_DALI2MQTT_WIFI_DEFAULT_PASS, sizeof(g_app_config.wifi_pass) - 1);
        g_app_config.wifi_pass[sizeof(g_app_config.wifi_pass) - 1] = '\0';
        strncpy(g_app_config.mqtt_uri, CONFIG_DALI2MQTT_MQTT_DEFAULT_URI, sizeof(g_app_config.mqtt_uri) - 1);
        g_app_config.mqtt_uri[sizeof(g_app_config.mqtt_uri) - 1] = '\0';
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS namespace '%s': %s. Using DALI defaults.", NVS_NAMESPACE, esp_err_to_name(err));
        // Устанавливаем значения по умолчанию для DALI параметров
        g_app_config.poll_interval_ms = CONFIG_DALI2MQTT_DALI_DEFAULT_POLL_INTERVAL_MS; // или CONFIG_MYPROJ_DALI_DEFAULT_POLL_INTERVAL_MS
        g_app_config.poll_groups_mask = 0x0001;
        g_app_config.poll_devices_mask = 0;
        return err; // Возвращаем ошибку открытия NVS
    }

    ESP_LOGI(TAG, "Loading configuration from NVS...");
    esp_err_t load_err; // Отслеживаем ошибки загрузки
    err = ESP_OK;       // Общий результат инициализаци

    load_err = load_u32_from_nvs(nvs_handle, NVS_KEY_POLL_INTERVAL, &g_app_config.poll_interval_ms, CONFIG_DALI2MQTT_DALI_DEFAULT_POLL_INTERVAL_MS);
    if (load_err != ESP_OK && err == ESP_OK) err = load_err;

    load_err = load_u16_from_nvs(nvs_handle, NVS_KEY_POLL_GROUPS, &g_app_config.poll_groups_mask, CONFIG_DALI2MQTT_DALI_DEFAULT_POLL_GROUPS_MASK);
    if (load_err != ESP_OK && err == ESP_OK) err = load_err;

    uint64_t default_devices_mask = combine_u32_to_u64(CONFIG_DALI2MQTT_DALI_DEFAULT_POLL_DEVICES_MASK, CONFIG_DALI2MQTT_DALI_DEFAULT_POLL_DEVICES_MASK_LO);
    load_err = load_u64_from_nvs(nvs_handle, NVS_KEY_POLL_DEVICES, &g_app_config.poll_devices_mask, default_devices_mask);
    if (load_err != ESP_OK && err == ESP_OK) err = load_err;

    ESP_LOGI(TAG, "Loaded Config: SSID='%s', MQTT URI='%s', Poll Interval=%lums, Poll Groups=0x%04X, Poll Devices=0x%016llX",
             g_app_config.wifi_ssid, g_app_config.mqtt_uri, (unsigned long)g_app_config.poll_interval_ms, g_app_config.poll_groups_mask, (unsigned long long)g_app_config.poll_devices_mask);

    // Сохраняем изменения (если были установлены значения по умолчанию)
    esp_err_t commit_err = nvs_commit(nvs_handle);
    if (commit_err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to commit NVS changes after loading: %s", esp_err_to_name(commit_err));
        if (err == ESP_OK) err = commit_err; // Запоминаем ошибку commit
    }

    nvs_close(nvs_handle);
    return err;
}

esp_err_t config_manager_save(void) {
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS namespace '%s' for saving: %s", NVS_NAMESPACE, esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "Saving configuration to NVS...");
    esp_err_t save_err; // Локальная переменная для ошибок сохранения
    err = ESP_OK;       // Инициализируем общую ошибку как OK

    save_err = nvs_set_u32(nvs_handle, NVS_KEY_POLL_INTERVAL, g_app_config.poll_interval_ms);
    if (save_err != ESP_OK) { ESP_LOGE(TAG, "Failed to save %s: %s", NVS_KEY_POLL_INTERVAL, esp_err_to_name(save_err)); if (err == ESP_OK) err = save_err; }

    save_err = nvs_set_u16(nvs_handle, NVS_KEY_POLL_GROUPS, g_app_config.poll_groups_mask);
    if (save_err != ESP_OK) { ESP_LOGE(TAG, "Failed to save %s: %s", NVS_KEY_POLL_GROUPS, esp_err_to_name(save_err)); if (err == ESP_OK) err = save_err; }

    save_err = nvs_set_u64(nvs_handle, NVS_KEY_POLL_DEVICES, g_app_config.poll_devices_mask);
    if (save_err != ESP_OK) { ESP_LOGE(TAG, "Failed to save %s: %s", NVS_KEY_POLL_DEVICES, esp_err_to_name(save_err)); if (err == ESP_OK) err = save_err; }

    esp_err_t commit_err = nvs_commit(nvs_handle);
    if (commit_err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to commit NVS changes: %s", esp_err_to_name(commit_err));
        if (err == ESP_OK) err = commit_err;
    }

    nvs_close(nvs_handle);

    // После успешного сохранения публикуем текущую конфигурацию
    if (err == ESP_OK) {
        mqtt_publish_config(); // Публикуем даже если были ошибки записи, но commit прошел
    } else {
        ESP_LOGW(TAG, "Configuration saved with errors, MQTT config update might be incomplete.");
        // Все равно публикуем то, что в памяти, т.к. оно могло измениться
         mqtt_publish_config();
    }

    return err; // Возвращаем первую ошибку записи или commit
}

esp_err_t config_manager_set_poll_interval(uint32_t interval_ms) {
    if (interval_ms < 100) { // Минимальный разумный интервал
       ESP_LOGW(TAG, "Poll interval %lums is too low, setting to 100ms", (unsigned long)interval_ms);
       interval_ms = 100;
    }
    if (g_app_config.poll_interval_ms != interval_ms) {
        ESP_LOGI(TAG, "Setting poll interval to %lums", (unsigned long)interval_ms);
        g_app_config.poll_interval_ms = interval_ms;
        // Перезапускаем таймер с новым интервалом (если он уже создан и запущен)
        // Используем g_dali_poll_timer из project_defs.h
        return config_manager_save();
    }
    return ESP_OK; // Интервал не изменился
}

esp_err_t config_manager_set_poll_groups_mask(uint16_t groups_mask) {
    if (g_app_config.poll_groups_mask != groups_mask) {
         ESP_LOGI(TAG, "Setting poll groups mask to 0x%04X", groups_mask);
         g_app_config.poll_groups_mask = groups_mask;
         return config_manager_save();
    }
     return ESP_OK;
}

esp_err_t config_manager_set_poll_devices_mask(uint64_t devices_mask) {
     if (g_app_config.poll_devices_mask != devices_mask) {
         ESP_LOGI(TAG, "Setting poll devices mask to 0x%016llX", (unsigned long long)devices_mask);
         g_app_config.poll_devices_mask = devices_mask;
         return config_manager_save();
    }
     return ESP_OK;
}