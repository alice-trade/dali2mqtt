#ifndef APP_CONFIG_H
#define APP_CONFIG_H

#include <stdint.h>

// --- WiFi Event Group Bits ---
#define WIFI_CONNECTED_BIT    BIT0
#define WIFI_FAIL_BIT         BIT1

// --- DALI Constants ---
#define DALI_MAX_DEVICES      64
#define DALI_MAX_GROUPS       16

// --- NVS Keys (только для DALI-специфичных настроек) ---
#define NVS_KEY_POLL_INTERVAL "poll_int"
#define NVS_KEY_POLL_GROUPS   "poll_groups"
#define NVS_KEY_POLL_DEVICES  "poll_devices"

// --- Структура для хранения текущей конфигурации в оперативной памяти ---
typedef struct {
    // WiFi параметры (копируются из sdkconfig)
    char wifi_ssid[64];         // Увеличен размер для большей гибкости
    char wifi_pass[65];

    // MQTT параметры (копируются из sdkconfig)
    char mqtt_uri[128];

    // DALI параметры (читаются из NVS, дефолты из sdkconfig)
    uint32_t poll_interval_ms;
    uint16_t poll_groups_mask;
    uint64_t poll_devices_mask;
} app_config_t;

// Глобальная переменная конфигурации (определена в main.c)
extern app_config_t g_app_config;

#endif // APP_CONFIG_H
