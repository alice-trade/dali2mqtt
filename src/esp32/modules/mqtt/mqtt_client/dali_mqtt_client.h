#ifndef MQTT_CLIENT_H
#define MQTT_CLIENT_H

#include <esp_err.h>

// Define a function pointer type for MQTT message handlers
typedef void (*mqtt_message_handler_t)(const char *topic, const char *payload);

esp_err_t mqtt_client_init(const char *broker_url, const char *client_id);
esp_err_t mqtt_client_start(void);
esp_err_t mqtt_client_stop(void);
bool mqtt_client_publish(const char *topic, const char *payload, int qos, bool retain);
bool mqtt_client_subscribe(const char *topic, int qos, mqtt_message_handler_t handler);
bool mqtt_client_unsubscribe(const char *topic);

#endif // MQTT_CLIENT_H