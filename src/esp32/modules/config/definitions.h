

#ifndef DEFINITIONS_H
#define DEFINITIONS_H


#include "sdkconfig.h"

// --- WiFi Event Group Bits ---
#define WIFI_CONNECTED_BIT    BIT0
#define WIFI_FAIL_BIT         BIT1

// --- DALI ---
#define DALI_MAX_DEVICES      64
#define DALI_MAX_GROUPS       16

// --- NVS (Non-Volatile Storage) ---
#define NVS_NAMESPACE         CONFIG_DALI2MQTT_NVS_NAMESPACE
#define NVS_KEY_WIFI_SSID     "wifi_ssid"
#define NVS_KEY_WIFI_PASS     "wifi_pass"
#define NVS_KEY_MQTT_URI      "mqtt_uri"
#define NVS_KEY_POLL_INTERVAL "poll_int"
#define NVS_KEY_POLL_GROUPS   "poll_groups"
#define NVS_KEY_POLL_DEVICES  "poll_devices"

// --- Структура конфигурации ---
typedef struct {
    char wifi_ssid[64];
    char wifi_pass[65];
    char mqtt_uri[128];
    uint32_t poll_interval_ms;
    uint16_t poll_groups_mask;
    uint64_t poll_devices_mask;
} app_config_t;

// --- Глобальные переменные ---
extern app_config_t g_app_config;


#endif //DEFINITIONS_H
