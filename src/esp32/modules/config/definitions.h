

#ifndef DEFINITIONS_H
#define DEFINITIONS_H


#include "freertos/FreeRTOS.h"
#include "freertos/timers.h"
#include "freertos/task.h" // Для TaskHandle_t

// --- Конфигурация GPIO ---
#define DALI_RX_PIN           GPIO_NUM_16 // !!! ЗАМЕНИТЕ НА ВАШ RX ПИН !!!
#define DALI_TX_PIN           GPIO_NUM_17 // !!! ЗАМЕНИТЕ НА ВАШ TX ПИН !!!

// --- WiFi ---
#define WIFI_DEFAULT_SSID     "YourSSID" // !!! ЗАМЕНИТЕ НА ВАШ SSID !!!
#define WIFI_DEFAULT_PASS     "YourPassword" // !!! ЗАМЕНИТЕ НА ВАШ ПАРОЛЬ !!!
#define WIFI_MAX_RETRY        5
#define WIFI_CONNECTED_BIT    BIT0
#define WIFI_FAIL_BIT         BIT1

// --- MQTT ---
#define MQTT_DEFAULT_URI      "mqtt://your_mqtt_broker_ip" // !!! ЗАМЕНИТЕ НА ВАШ MQTT URI !!! (например, "mqtt://user:pass@host:port")
#define MQTT_DEFAULT_CLIENT_ID "esp32_dali_bridge"
#define MQTT_BASE_TOPIC       "dali_bridge" // Базовый топик
#define MQTT_AVAILABILITY_TOPIC MQTT_BASE_TOPIC "/status"
#define MQTT_COMMAND_TOPIC_PREFIX MQTT_BASE_TOPIC "/light/" // Префикс для команд light/group/+/set
#define MQTT_STATE_TOPIC_PREFIX MQTT_BASE_TOPIC "/light/"   // Префикс для состояния light/group/+/state
#define MQTT_CONFIG_TOPIC     MQTT_BASE_TOPIC "/config/set" // Топик для получения конфигурации
#define MQTT_CONFIG_GET_TOPIC MQTT_BASE_TOPIC "/config/get" // Топик для запроса текущей конфигурации
#define MQTT_CONFIG_GET_RESPONSE_TOPIC MQTT_BASE_TOPIC "/config/get/response" // Топик для ответа на запрос конфигурации
#define MQTT_GROUP_MANAGEMENT_SET_TOPIC MQTT_BASE_TOPIC "/config/group/set" // Топик для установки членства в группе
#define MQTT_GROUP_MANAGEMENT_RESULT_TOPIC MQTT_BASE_TOPIC "/config/group/result" // Топик для результата операции управления группой

#define MQTT_PAYLOAD_ONLINE   "online"
#define MQTT_PAYLOAD_OFFLINE  "offline"

// --- DALI ---
#define DALI_MAX_DEVICES      64
#define DALI_MAX_GROUPS       16
#define DALI_DEFAULT_POLL_INTERVAL_MS (5 * 1000) // 5 секунд
#define DALI_TRANSACTION_TIMEOUT_MS 100 // Таймаут для ожидания ответа DALI
#define DALI_INTER_FRAME_DELAY_MS 20   // Минимальная пауза между кадрами DALI
#define DALI_POLL_DELAY_MS 50          // Пауза между опросами разных адресов

// --- NVS (Non-Volatile Storage) ---
#define NVS_NAMESPACE         "dali_cfg"
#define NVS_KEY_WIFI_SSID     "wifi_ssid"
#define NVS_KEY_WIFI_PASS     "wifi_pass"
#define NVS_KEY_MQTT_URI      "mqtt_uri"
#define NVS_KEY_POLL_INTERVAL "poll_int"
#define NVS_KEY_POLL_GROUPS   "poll_groups" // Маска групп для опроса (uint16_t)
#define NVS_KEY_POLL_DEVICES  "poll_devices" // Маска устройств для опроса (uint64_t)

// --- Задачи и Таймеры ---
#define DALI_POLL_TASK_PRIORITY  5
#define DALI_POLL_TASK_STACK_SIZE 4096

// --- Структура конфигурации ---
typedef struct {
    char wifi_ssid[33];      // +1 for null terminator
    char wifi_pass[65];      // +1 for null terminator
    char mqtt_uri[128];
    uint32_t poll_interval_ms;
    uint16_t poll_groups_mask; // Битовая маска для групп 0-15
    uint64_t poll_devices_mask;// Битовая маска для устройств 0-63
} app_config_t;

// --- Глобальные переменные ---
// Определены в main.c, доступны в других модулях через extern
extern app_config_t g_app_config;       // Глобальная структура с текущей конфигурацией


#endif //DEFINITIONS_H
