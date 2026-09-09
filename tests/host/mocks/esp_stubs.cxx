//  Copyright (c) 2026 Alice-Trade Inc.
//  SPDX-License-Identifier: GPL-3.0-or-later

#include <chrono>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "esp_err.h"
#include "esp_timer.h"
#include "driver/gpio.h"
#include "driver/rmt_tx.h"
#include "driver/rmt_rx.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "mqtt_client.h"
#include "nvs.h"
#include "nvs_flash.h"

namespace HostTestUtils {
    struct MqttMsg {
        std::string topic;
        std::string payload;
        int qos;
        bool retain;
    };

    static std::vector<MqttMsg> g_published_mqtt;
    static int64_t g_mock_time_offset_us = 0;

    std::vector<MqttMsg>& getMqttMessages() { return g_published_mqtt; }
    void clearMqttMessages() { g_published_mqtt.clear(); }

    struct NvsStorage {
        std::map<std::string, std::vector<uint8_t>> blobs;
        std::map<std::string, std::string> strings;
        std::map<std::string, uint64_t> numbers;
    };

    static std::map<std::string, NvsStorage> g_nvs_database;
    static std::map<nvs_handle_t, std::string> g_open_handles;
    static nvs_handle_t g_next_handle = 1;

    void resetNvs() {
        g_nvs_database.clear();
        g_open_handles.clear();
        g_next_handle = 1;
    }

    void advanceTimeMs(uint32_t ms) {
        g_mock_time_offset_us += static_cast<int64_t>(ms) * 1000;
    }

    void setTimeUs(int64_t target_us) {
        g_mock_time_offset_us = target_us;
    }
} // namespace HostTestUtils

extern "C" {

static uint32_t g_last_task_notify_val = 0;
static void (*g_mqtt_event_handler)(void*, esp_event_base_t, int32_t, void*) = nullptr;
static void* g_mqtt_event_args = nullptr;

const char* esp_err_to_name(esp_err_t code) {
    switch (code) {
        case ESP_OK: return "ESP_OK";
        case ESP_FAIL: return "ESP_FAIL";
        case ESP_ERR_NO_MEM: return "ESP_ERR_NO_MEM";
        case ESP_ERR_INVALID_ARG: return "ESP_ERR_INVALID_ARG";
        case ESP_ERR_INVALID_STATE: return "ESP_ERR_INVALID_STATE";
        case ESP_ERR_NOT_FOUND: return "ESP_ERR_NOT_FOUND";
        case ESP_ERR_TIMEOUT: return "ESP_ERR_TIMEOUT";
        default: return "ESP_UNKNOWN_ERR";
    }
}

void esp_log_write(int level, const char* tag, const char* format, ...) {
    (void)level;
    printf("[%s] ", tag);
    va_list args;
    va_start(args, format);
    vprintf(format, args);
    va_end(args);
    printf("\n");
}

uint32_t esp_log_timestamp(void) {
    static const auto start = std::chrono::steady_clock::now();
    const auto now = std::chrono::steady_clock::now();
    return static_cast<uint32_t>(std::chrono::duration_cast<std::chrono::milliseconds>(now - start).count());
}

int64_t esp_timer_get_time(void) {
    static const auto start = std::chrono::steady_clock::now();
    const auto now = std::chrono::steady_clock::now();
    int64_t real_us = std::chrono::duration_cast<std::chrono::microseconds>(now - start).count();
    return real_us + HostTestUtils::g_mock_time_offset_us;
}

uint32_t esp_random(void) {
    static uint32_t seed = 0x12345678;
    seed = seed * 1664525 + 1013904223;
    return seed;
}

void esp_restart(void) {}

esp_err_t esp_read_mac(uint8_t* mac, int type) {
    (void)type;
    mac[0] = 0x00; mac[1] = 0x11; mac[2] = 0x22;
    mac[3] = 0xAA; mac[4] = 0xBB; mac[5] = 0xCC;
    return ESP_OK;
}

void esp_rom_delay_us(uint32_t us) {
    (void)us;
}

// FreeRTOS Task Stubs
TaskHandle_t xTaskGetCurrentTaskHandle(void) {
    return reinterpret_cast<TaskHandle_t>(0xDEADBEEF);
}

void vTaskDelay(TickType_t ticks) {
    (void)ticks;
}

BaseType_t xTaskCreate(void (*pxTaskCode)(void*), const char* const pcName, const uint32_t usStackDepth,
                       void* const pvParameters, UBaseType_t uxPriority, TaskHandle_t* const pxCreatedTask) {
    (void)pxTaskCode; (void)pcName; (void)usStackDepth; (void)pvParameters; (void)uxPriority;
    if (pxCreatedTask) {
        *pxCreatedTask = reinterpret_cast<TaskHandle_t>(0xDEADBEEF);
    }
    return 1; // pdPASS
}

void vTaskDelete(TaskHandle_t xTask) {
    (void)xTask;
}

BaseType_t xTaskNotify(TaskHandle_t xTaskToNotify, uint32_t ulValue, eNotifyAction eAction) {
    (void)xTaskToNotify; (void)ulValue; (void)eAction;
    return 1; // pdPASS
}

BaseType_t xTaskNotifyFromISR(TaskHandle_t xTaskToNotify, uint32_t ulValue, eNotifyAction eAction,
                              BaseType_t* pxHigherPriorityTaskWoken) {
    (void)xTaskToNotify; (void)ulValue; (void)eAction;
    if (pxHigherPriorityTaskWoken) {
        *pxHigherPriorityTaskWoken = 0; // pdFALSE
    }
    return 1; // pdPASS
}

BaseType_t xTaskNotifyWait(uint32_t ulBitsToClearOnEntry, uint32_t ulBitsToClearOnExit,
                           uint32_t* pulNotificationValue, TickType_t xTicksToWait) {
    (void)ulBitsToClearOnEntry; (void)ulBitsToClearOnExit; (void)xTicksToWait;
    if (pulNotificationValue) {
        *pulNotificationValue = 0;
    }
    return 1; // pdPASS
}

BaseType_t xTaskNotifyWaitIndexed(UBaseType_t uxIndexToWaitOn, uint32_t ulBitsToClearOnEntry,
                                  uint32_t ulBitsToClearOnExit, uint32_t* pulNotificationValue,
                                  TickType_t xTicksToWait) {
    (void)uxIndexToWaitOn;
    (void)ulBitsToClearOnEntry;
    (void)ulBitsToClearOnExit;
    (void)xTicksToWait;
    if (pulNotificationValue) {
        *pulNotificationValue = g_last_task_notify_val;
    }
    return 1; // pdTRUE
}

BaseType_t xTaskNotifyIndexed(TaskHandle_t xTaskToNotify, UBaseType_t uxIndexToNotify,
                              uint32_t ulValue, eNotifyAction eAction) {
    (void)xTaskToNotify; (void)uxIndexToNotify; (void)eAction;
    g_last_task_notify_val = ulValue;
    return 1; // pdPASS
}

void xTaskNotifyStateClearIndexed(TaskHandle_t xTask, UBaseType_t uxIndexToClear) {
    (void)xTask; (void)uxIndexToClear;
}

// FreeRTOS Mutex Stubs
SemaphoreHandle_t xSemaphoreCreateRecursiveMutex(void) {
    return reinterpret_cast<SemaphoreHandle_t>(0x1000);
}

BaseType_t xSemaphoreTakeRecursive(SemaphoreHandle_t xMutex, TickType_t xBlockTime) {
    (void)xMutex; (void)xBlockTime;
    return 1; // pdTRUE
}

BaseType_t xSemaphoreGiveRecursive(SemaphoreHandle_t xMutex) {
    (void)xMutex;
    return 1; // pdTRUE
}

void vSemaphoreDelete(SemaphoreHandle_t xMutex) {
    (void)xMutex;
}

// FreeRTOS Queue Stubs
QueueHandle_t xQueueCreate(uint32_t uxQueueLength, uint32_t uxItemSize) {
    (void)uxQueueLength; (void)uxItemSize;
    return reinterpret_cast<QueueHandle_t>(0x2000);
}

BaseType_t xQueueSend(QueueHandle_t xQueue, const void* pvItemToQueue, TickType_t xTicksToWait) {
    (void)xQueue; (void)pvItemToQueue; (void)xTicksToWait;
    return 1; // pdTRUE
}

BaseType_t xQueueReceive(QueueHandle_t xQueue, void* pvBuffer, TickType_t xTicksToWait) {
    (void)xQueue; (void)pvBuffer; (void)xTicksToWait;
    return 0; // pdFALSE (timeout)
}

void vQueueDelete(QueueHandle_t xQueue) {
    (void)xQueue;
}

BaseType_t xQueueOverwrite(QueueHandle_t xQueue, const void* pvItemToQueue) {
    (void)xQueue; (void)pvItemToQueue;
    return 1; // pdTRUE
}

BaseType_t xQueueReset(QueueHandle_t xQueue) {
    (void)xQueue;
    return 1; // pdTRUE
}
UBaseType_t uxQueueMessagesWaiting(QueueHandle_t xQueue) {
    (void)xQueue;
    return 0;
}

// NVS In-Memory Storage Stubs
esp_err_t nvs_flash_init(void) { return ESP_OK; }
esp_err_t nvs_flash_erase(void) { HostTestUtils::g_nvs_database.clear(); return ESP_OK; }

esp_err_t nvs_open(const char* name, int open_mode, nvs_handle_t* out_handle) {
    (void)open_mode;
    nvs_handle_t h = HostTestUtils::g_next_handle++;
    HostTestUtils::g_open_handles[h] = name ? name : "";
    *out_handle = h;
    return ESP_OK;
}

void nvs_close(nvs_handle_t handle) {
    HostTestUtils::g_open_handles.erase(handle);
}

esp_err_t nvs_commit(nvs_handle_t handle) {
    (void)handle;
    return ESP_OK;
}

esp_err_t nvs_erase_all(nvs_handle_t handle) {
    auto it = HostTestUtils::g_open_handles.find(handle);
    if (it == HostTestUtils::g_open_handles.end()) return ESP_FAIL;
    HostTestUtils::g_nvs_database[it->second] = HostTestUtils::NvsStorage{};
    return ESP_OK;
}

esp_err_t nvs_set_u8(nvs_handle_t handle, const char* key, uint8_t value) {
    auto it = HostTestUtils::g_open_handles.find(handle);
    if (it == HostTestUtils::g_open_handles.end()) return ESP_FAIL;
    HostTestUtils::g_nvs_database[it->second].numbers[key] = value;
    return ESP_OK;
}

esp_err_t nvs_get_u8(nvs_handle_t handle, const char* key, uint8_t* out_value) {
    auto it = HostTestUtils::g_open_handles.find(handle);
    if (it == HostTestUtils::g_open_handles.end()) return ESP_FAIL;
    auto& db = HostTestUtils::g_nvs_database[it->second].numbers;
    auto val_it = db.find(key);
    if (val_it == db.end()) return ESP_ERR_NVS_NOT_FOUND;
    if (out_value) *out_value = static_cast<uint8_t>(val_it->second);
    return ESP_OK;
}

esp_err_t nvs_set_u32(nvs_handle_t handle, const char* key, uint32_t value) {
    auto it = HostTestUtils::g_open_handles.find(handle);
    if (it == HostTestUtils::g_open_handles.end()) return ESP_FAIL;
    HostTestUtils::g_nvs_database[it->second].numbers[key] = value;
    return ESP_OK;
}

esp_err_t nvs_get_u32(nvs_handle_t handle, const char* key, uint32_t* out_value) {
    auto it = HostTestUtils::g_open_handles.find(handle);
    if (it == HostTestUtils::g_open_handles.end()) return ESP_FAIL;
    auto& db = HostTestUtils::g_nvs_database[it->second].numbers;
    auto val_it = db.find(key);
    if (val_it == db.end()) return ESP_ERR_NVS_NOT_FOUND;
    if (out_value) *out_value = static_cast<uint32_t>(val_it->second);
    return ESP_OK;
}

esp_err_t nvs_set_i8(nvs_handle_t handle, const char* key, int8_t value) {
    return nvs_set_u8(handle, key, static_cast<uint8_t>(value));
}

esp_err_t nvs_get_i8(nvs_handle_t handle, const char* key, int8_t* out_value) {
    return nvs_get_u8(handle, key, reinterpret_cast<uint8_t*>(out_value));
}

esp_err_t nvs_set_str(nvs_handle_t handle, const char* key, const char* value) {
    auto it = HostTestUtils::g_open_handles.find(handle);
    if (it == HostTestUtils::g_open_handles.end()) return ESP_FAIL;
    HostTestUtils::g_nvs_database[it->second].strings[key] = value ? value : "";
    return ESP_OK;
}

esp_err_t nvs_get_str(nvs_handle_t handle, const char* key, char* out_value, size_t* length) {
    auto it = HostTestUtils::g_open_handles.find(handle);
    if (it == HostTestUtils::g_open_handles.end()) return ESP_FAIL;
    auto& db = HostTestUtils::g_nvs_database[it->second].strings;
    auto val_it = db.find(key);
    if (val_it == db.end()) return ESP_ERR_NVS_NOT_FOUND;

    const std::string& val = val_it->second;
    const size_t req_len = val.size() + 1;

    if (out_value == nullptr) {
        if (length) *length = req_len;
        return ESP_OK;
    }
    if (length && *length < req_len) {
        *length = req_len;
        return ESP_ERR_NO_MEM;
    }
    memcpy(out_value, val.c_str(), req_len);
    if (length) *length = req_len;
    return ESP_OK;
}

esp_err_t nvs_set_blob(nvs_handle_t handle, const char* key, const void* value, size_t length) {
    auto it = HostTestUtils::g_open_handles.find(handle);
    if (it == HostTestUtils::g_open_handles.end()) return ESP_FAIL;
    const auto* bytes = static_cast<const uint8_t*>(value);
    HostTestUtils::g_nvs_database[it->second].blobs[key] = std::vector<uint8_t>(bytes, bytes + length);
    return ESP_OK;
}

esp_err_t nvs_get_blob(nvs_handle_t handle, const char* key, void* out_value, size_t* length) {
    auto it = HostTestUtils::g_open_handles.find(handle);
    if (it == HostTestUtils::g_open_handles.end()) return ESP_FAIL;
    auto& db = HostTestUtils::g_nvs_database[it->second].blobs;
    auto val_it = db.find(key);
    if (val_it == db.end()) return ESP_ERR_NVS_NOT_FOUND;

    const auto& blob = val_it->second;
    if (out_value == nullptr) {
        if (length) *length = blob.size();
        return ESP_OK;
    }
    if (length && *length < blob.size()) {
        return ESP_ERR_NO_MEM;
    }
    memcpy(out_value, blob.data(), blob.size());
    if (length) *length = blob.size();
    return ESP_OK;
}

// RMT Driver Stubs
esp_err_t rmt_new_tx_channel(const rmt_tx_channel_config_t* config, rmt_channel_handle_t* ret_chan) {
    (void)config; *ret_chan = reinterpret_cast<void*>(0x3001); return ESP_OK;
}
esp_err_t rmt_new_rx_channel(const rmt_rx_channel_config_t* config, rmt_channel_handle_t* ret_chan) {
    (void)config; *ret_chan = reinterpret_cast<void*>(0x3002); return ESP_OK;
}
esp_err_t rmt_new_copy_encoder(const rmt_copy_encoder_config_t* config, rmt_encoder_handle_t* ret_encoder) {
    (void)config; *ret_encoder = reinterpret_cast<void*>(0x3003); return ESP_OK;
}
esp_err_t rmt_del_channel(rmt_channel_handle_t channel) { (void)channel; return ESP_OK; }
esp_err_t rmt_del_encoder(rmt_encoder_handle_t encoder) { (void)encoder; return ESP_OK; }
esp_err_t rmt_enable(rmt_channel_handle_t channel) { (void)channel; return ESP_OK; }
esp_err_t rmt_disable(rmt_channel_handle_t channel) { (void)channel; return ESP_OK; }
esp_err_t rmt_rx_register_event_callbacks(rmt_channel_handle_t channel, const rmt_rx_event_callbacks_t* cbs, void* user_data) {
    (void)channel; (void)cbs; (void)user_data; return ESP_OK;
}
esp_err_t rmt_receive(rmt_channel_handle_t channel, void* buffer, size_t buffer_size, const rmt_receive_config_t* config) {
    (void)channel; (void)buffer; (void)buffer_size; (void)config; return ESP_OK;
}
esp_err_t rmt_transmit(rmt_channel_handle_t channel, rmt_encoder_handle_t encoder, const void* payload, size_t payload_bytes, const rmt_transmit_config_t* config) {
    (void)channel; (void)encoder; (void)payload; (void)payload_bytes; (void)config; return ESP_OK;
}
esp_err_t rmt_tx_wait_all_done(rmt_channel_handle_t channel, int timeout_ms) {
    (void)channel; (void)timeout_ms; return ESP_OK;
}

// MQTT Driver Stubs
esp_mqtt_client_handle_t esp_mqtt_client_init(const esp_mqtt_client_config_t* config) {
    (void)config;
    return reinterpret_cast<esp_mqtt_client_handle_t>(0x4001);
}

esp_err_t esp_mqtt_client_destroy(esp_mqtt_client_handle_t client) {
    (void)client;
    return ESP_OK;
}

esp_err_t esp_mqtt_client_stop(esp_mqtt_client_handle_t client) {
    (void)client;
    return ESP_OK;
}

int esp_mqtt_client_subscribe(esp_mqtt_client_handle_t client, const char* topic, int qos) {
    (void)client; (void)topic; (void)qos;
    return 1;
}

int esp_mqtt_client_publish(esp_mqtt_client_handle_t client, const char* topic, const char* data, int len, int qos, int retain) {
    (void)client;
    std::string payload_str = data ? std::string(data, len > 0 ? static_cast<size_t>(len) : strlen(data)) : "";
    HostTestUtils::g_published_mqtt.push_back({topic ? topic : "", payload_str, qos, retain != 0});
    return 1;
}

esp_err_t esp_mqtt_client_register_event(esp_mqtt_client_handle_t client, esp_mqtt_event_id_t event_id,
                                         void (*handler)(void*, esp_event_base_t, int32_t, void*), void* args) {
    (void)client; (void)event_id;
    g_mqtt_event_handler = handler;
    g_mqtt_event_args = args;
    return ESP_OK;
}

esp_err_t esp_mqtt_client_start(esp_mqtt_client_handle_t client) {
    (void)client;
    if (g_mqtt_event_handler) {
        esp_mqtt_event_t ev{};
        ev.event_id = MQTT_EVENT_CONNECTED;
        g_mqtt_event_handler(g_mqtt_event_args, "MQTT", MQTT_EVENT_CONNECTED, &ev);
    }
    return ESP_OK;
}

} // extern "C"