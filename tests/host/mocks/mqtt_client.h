//  Copyright (c) 2026 Alice-Trade Inc.
//  SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include <stdint.h>
#include <stddef.h>
#include "esp_err.h"

#define MQTT_PROTOCOL_V_3_1_1 3

typedef void* esp_mqtt_client_handle_t;
typedef const char* esp_event_base_t;

typedef enum {
    MQTT_EVENT_ANY = -1,
    MQTT_EVENT_ERROR = 0,
    MQTT_EVENT_CONNECTED,
    MQTT_EVENT_DISCONNECTED,
    MQTT_EVENT_SUBSCRIBED,
    MQTT_EVENT_UNSUBSCRIBED,
    MQTT_EVENT_PUBLISHED,
    MQTT_EVENT_DATA,
    MQTT_EVENT_BEFORE_CONNECT,
    MQTT_EVENT_DELETED
} esp_mqtt_event_id_t;

typedef struct {
    esp_mqtt_event_id_t event_id;
    const char* topic;
    int topic_len;
    const char* data;
    int data_len;
} esp_mqtt_event_t;

typedef esp_mqtt_event_t* esp_mqtt_event_handle_t;

typedef struct {
    struct {
        int protocol_ver;
        struct {
            const char* topic;
            const char* msg;
            int qos;
            int retain;
        } last_will;
    } session;
    struct {
        struct {
            const char* uri;
        } address;
        struct {
            const char* certificate;
            esp_err_t (*crt_bundle_attach)(void*);
        } verification;
    } broker;
    struct {
        const char* client_id;
        const char* username;
        struct {
            const char* password;
        } authentication;
    } credentials;
    struct {
        int stack_size;
    } task;
} esp_mqtt_client_config_t;

#ifdef __cplusplus
extern "C" {
#endif
esp_mqtt_client_handle_t esp_mqtt_client_init(const esp_mqtt_client_config_t* config);
esp_err_t esp_mqtt_client_destroy(esp_mqtt_client_handle_t client);
esp_err_t esp_mqtt_client_start(esp_mqtt_client_handle_t client);
esp_err_t esp_mqtt_client_stop(esp_mqtt_client_handle_t client);
int esp_mqtt_client_publish(esp_mqtt_client_handle_t client, const char* topic, const char* data, int len, int qos, int retain);
int esp_mqtt_client_subscribe(esp_mqtt_client_handle_t client, const char* topic, int qos);
esp_err_t esp_mqtt_client_register_event(esp_mqtt_client_handle_t client, esp_mqtt_event_id_t event_id, void (*handler)(void*, esp_event_base_t, int32_t, void*), void* args);
#ifdef __cplusplus
}
#endif