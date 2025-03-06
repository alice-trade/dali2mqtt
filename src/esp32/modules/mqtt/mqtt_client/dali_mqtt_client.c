//
// Created by danil on 23.02.2025.
//
// src/esp32/modules/mqtt/mqtt_client.c
#include "dali_mqtt_client.h"
#include <esp_log.h>
#include <mqtt_client.h>
#include <cstring> // For string manipulation

static const char *TAG = "MqttClient";

// Define a structure to hold subscription information
typedef struct {
    char *topic;
    mqtt_message_handler_t handler;
    int qos;
} subscription_t;

#define MAX_SUBSCRIPTIONS 10 // Define a maximum number of subscriptions for simplicity
static subscription_t subscriptions[MAX_SUBSCRIPTIONS];
static int subscription_count = 0;
static esp_mqtt_client_handle_t client = NULL;

// Event handler for the ESP-MQTT client.
static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data) {
    ESP_LOGD(TAG, "Event dispatched from event loop base=%s, event_id=%" PRId32, base, event_id);
    esp_mqtt_event_handle_t event = (esp_mqtt_event_handle_t)event_data;

    switch ((esp_mqtt_event_id_t)event_id) {
        case MQTT_EVENT_CONNECTED:
            ESP_LOGI(TAG, "MQTT_EVENT_CONNECTED");
            // Resubscribe to all topics on reconnection.
            for (int i = 0; i < subscription_count; ++i) {
                esp_mqtt_client_subscribe(client, subscriptions[i].topic, subscriptions[i].qos);
            }
            break;
        case MQTT_EVENT_DISCONNECTED:
            ESP_LOGI(TAG, "MQTT_EVENT_DISCONNECTED");
            break;
        case MQTT_EVENT_SUBSCRIBED:
            ESP_LOGI(TAG, "MQTT_EVENT_SUBSCRIBED, msg_id=%d", event->msg_id);
            break;
        case MQTT_EVENT_UNSUBSCRIBED:
            ESP_LOGI(TAG, "MQTT_EVENT_UNSUBSCRIBED, msg_id=%d", event->msg_id);
            break;
        case MQTT_EVENT_PUBLISHED:
            ESP_LOGI(TAG, "MQTT_EVENT_PUBLISHED, msg_id=%d", event->msg_id);
            break;
        case MQTT_EVENT_DATA: {
            ESP_LOGI(TAG, "MQTT_EVENT_DATA");
            // Call the registered handler for the topic.
            char *topic = strndup(event->topic, event->topic_len);
            char *payload = strndup(event->data, event->data_len);

            if (topic == NULL || payload == NULL) {
                ESP_LOGE(TAG, "Memory allocation failed for topic or payload.");
                free(topic);
                free(payload);
                break;
            }

            bool handler_found = false;
            for (int i = 0; i < subscription_count; ++i) {
                if (strcmp(subscriptions[i].topic, topic) == 0) {
                    subscriptions[i].handler(topic, payload);
                    handler_found = true;
                    break;
                }
            }
            if (!handler_found) {
                ESP_LOGW(TAG, "Received message for unsubscribed topic: %s", topic);
            }
            free(topic);
            free(payload);
            break;
        }
        case MQTT_EVENT_ERROR:
            ESP_LOGE(TAG, "MQTT_EVENT_ERROR");
            if (event->error_handle->error_type == MQTT_ERROR_TYPE_TCP_TRANSPORT) {
                ESP_LOGE(TAG, "Last error code reported from esp-tls: 0x%x", event->error_handle->esp_tls_last_esp_err);
                ESP_LOGE(TAG, "Last tls stack error number: 0x%x", event->error_handle->esp_tls_stack_err);
                ESP_LOGE(TAG, "Last captured errno : %d (%s)",  event->error_handle->esp_transport_sock_errno,
                         strerror(event->error_handle->esp_transport_sock_errno));
            } else if (event->error_handle->error_type == MQTT_ERROR_TYPE_CONNECTION_REFUSED) {
                ESP_LOGE(TAG, "Connection refused error: 0x%x", event->error_handle->connect_return_code);
            } else {
                ESP_LOGE(TAG, "Unknown error type: 0x%x", event->error_handle->error_type);
            }
            break;
        default:
            ESP_LOGI(TAG, "Other event id:%d", event->event_id);
            break;
    }
}

esp_err_t mqtt_client_init(const char *broker_url, const char *client_id) {
    esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri = broker_url,
        //.credentials.client_id = client_id, // Set if using a persistent client ID.
        .session.keepalive = 60,
    };

    client = esp_mqtt_client_init(&mqtt_cfg);
    if (client == NULL) {
        ESP_LOGE(TAG, "Failed to initialize MQTT client");
        return ESP_FAIL;
    }

    esp_err_t err = esp_mqtt_client_register_event(client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register MQTT event handler");
        esp_mqtt_client_destroy(client);
        client = NULL;
        return err;
    }

    return ESP_OK;
}

esp_err_t mqtt_client_start(void) {
    if (client == NULL) {
        ESP_LOGE(TAG, "MQTT client not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    return esp_mqtt_client_start(client);
}

esp_err_t mqtt_client_stop(void) {
    if (client == NULL) {
        ESP_LOGE(TAG, "MQTT client not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    // Unsubscribe from all topics before stopping.
    for (int i = 0; i < subscription_count; ++i) {
        esp_mqtt_client_unsubscribe(client, subscriptions[i].topic);
        free(subscriptions[i].topic); // Free allocated topic strings
        subscriptions[i].topic = NULL;
    }
    subscription_count = 0; // Reset subscription count

    esp_err_t err = esp_mqtt_client_stop(client);
    client = NULL; // Set client to NULL after stop to indicate it's no longer valid.
    return err;
}


bool mqtt_client_publish(const char *topic, const char *payload, int qos, bool retain) {
    if (client == NULL) {
        ESP_LOGE(TAG, "MQTT client not initialized");
        return false;
    }
    int msg_id = esp_mqtt_client_publish(client, topic, payload, 0, qos, retain); // payload_len = 0 means strlen
    if (msg_id < 0) {
        ESP_LOGE(TAG, "Failed to publish to topic: %s", topic);
        return false;
    }
    return true;
}

bool mqtt_client_subscribe(const char *topic, int qos, mqtt_message_handler_t handler) {
    if (client == NULL) {
        ESP_LOGE(TAG, "MQTT client not initialized");
        return false;
    }
    if (subscription_count >= MAX_SUBSCRIPTIONS) {
        ESP_LOGE(TAG, "Maximum number of subscriptions reached.");
        return false;
    }

    int msg_id = esp_mqtt_client_subscribe(client, topic, qos);
    if (msg_id < 0) {
        ESP_LOGE(TAG, "Failed to subscribe to topic: %s", topic);
        return false;
    }

    // Store the subscription information
    subscriptions[subscription_count].topic = strdup(topic); // Allocate and copy topic string
    if (subscriptions[subscription_count].topic == NULL) {
        ESP_LOGE(TAG, "Memory allocation failed for subscription topic.");
        esp_mqtt_client_unsubscribe(client, topic); // Try to unsubscribe if memory allocation fails
        return false;
    }
    subscriptions[subscription_count].handler = handler;
    subscriptions[subscription_count].qos = qos;
    subscription_count++;

    return true;
}

bool mqtt_client_unsubscribe(const char *topic) {
    if (client == NULL) {
        ESP_LOGE(TAG, "MQTT client not initialized");
        return false;
    }
    int msg_id = esp_mqtt_client_unsubscribe(client, topic);
    if (msg_id < 0) {
        ESP_LOGE(TAG, "Failed to unsubscribe from topic: %s", topic);
        return false;
    }

    // Remove subscription from the subscriptions array (shifting elements)
    for (int i = 0; i < subscription_count; ++i) {
        if (strcmp(subscriptions[i].topic, topic) == 0) {
            free(subscriptions[i].topic);
            subscriptions[i].topic = NULL; // Clear the topic pointer

            // Shift subsequent subscriptions to fill the gap
            for (int j = i; j < subscription_count - 1; ++j) {
                subscriptions[j] = subscriptions[j + 1];
            }
            subscription_count--;
            return true; // Successfully unsubscribed and removed
        }
    }

    ESP_LOGW(TAG, "Topic not found in subscriptions: %s", topic);
    return false; // Topic not found in subscriptions
}