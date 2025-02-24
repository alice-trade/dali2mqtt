//
// Created by danil on 23.02.2025.
//

#include "mqtt_client.hpp"

#include <esp_event_base.h>
#include <mqtt_client.h>
#include <esp_log.h>
#include <map>

static const char *TAG = "MqttClient";

namespace MqttClient {

struct Subscription {
    MqttMessageHandler handler;
    int qos;
};

static esp_mqtt_client_handle_t client = nullptr;
static std::map<std::string, Subscription> subscriptions;

static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data) {
    // ... (Обработчик событий, без изменений)
     esp_mqtt_event_handle_t event = static_cast<esp_mqtt_event_handle_t>(event_data);

    switch (static_cast<esp_mqtt_event_id_t>(event_id)) {
        case MQTT_EVENT_CONNECTED:
            ESP_LOGI(TAG, "MQTT_EVENT_CONNECTED");
            // Resubscribe to all topics on reconnection.
            for (const auto& pair : subscriptions) {
                esp_mqtt_client_subscribe(client, pair.first.c_str(), pair.second.qos);
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
            std::string topic(event->topic, event->topic_len);
            std::string payload(event->data, event->data_len);

            if (subscriptions.count(topic)) {
                subscriptions[topic].handler(topic, payload);
            } else {
                ESP_LOGW(TAG, "Received message for unsubscribed topic: %s", topic.c_str());
            }
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

esp_err_t init(const std::string& broker_url, const std::string& client_id) {
    // ... (Инициализация MQTT клиента, без изменений)
     esp_mqtt_client_config_t mqtt_cfg = {};
    mqtt_cfg.broker.address.uri = broker_url.c_str();
    //mqtt_cfg.credentials.client_id = client_id.c_str(); // Set if using a persistent client ID.
    mqtt_cfg.session.keepalive = 60;

    client = esp_mqtt_client_init(&mqtt_cfg);
    if (!client) {
        ESP_LOGE(TAG, "Failed to initialize MQTT client");
        return ESP_FAIL;
    }

    esp_err_t err = esp_mqtt_client_register_event(client, static_cast<esp_mqtt_event_id_t>(ESP_EVENT_ANY_ID), mqtt_event_handler, nullptr);
    if (err) {
        ESP_LOGE(TAG, "Failed to register MQTT event handler");
        esp_mqtt_client_destroy(client);
        return err;
    }

    return ESP_OK;
}

esp_err_t start() {
   // ... (Старт MQTT клиента, без изменений)
    if (!client) {
        ESP_LOGE(TAG, "MQTT client not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    return esp_mqtt_client_start(client);
}

esp_err_t stop() {
   // ... (Остановка MQTT клиента, без изменений)
     if (!client) {
        ESP_LOGE(TAG, "MQTT client not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    // Unsubscribe from all topics before stopping.
    for (const auto& pair : subscriptions) {
        esp_mqtt_client_unsubscribe(client, pair.first.c_str());
    }
    subscriptions.clear(); // Clear the subscription map

    return esp_mqtt_client_stop(client);
}

bool publish(const std::string& topic, const std::string& payload, int qos, bool retain) {
    if (!client) {
        ESP_LOGE(TAG, "MQTT client not initialized");
        return false;
    }
    int msg_id = esp_mqtt_client_publish(client, topic.c_str(), payload.c_str(), payload.length(), qos, retain);
    return msg_id >= 0;
}

bool subscribe(const std::string& topic, int qos, MqttMessageHandler handler) {
   if (!client) {
        ESP_LOGE(TAG, "MQTT client not initialized");
        return false;
    }
    int msg_id = esp_mqtt_client_subscribe(client, topic.c_str(), qos);
    if (msg_id < 0) {
        ESP_LOGE(TAG, "Failed to subscribe to topic: %s", topic.c_str());
        return false;
    }
    // Store the handler and QoS for later use (e.g., re-subscription on reconnect).
    subscriptions[topic] = {handler, qos};
    return true;
}

bool unsubscribe(const std::string& topic) {
     if (!client) {
        ESP_LOGE(TAG, "MQTT client not initialized");
        return false;
    }
    int msg_id = esp_mqtt_client_unsubscribe(client, topic.c_str());
      if (msg_id < 0) {
        ESP_LOGE(TAG, "Failed to unsubscribe to topic: %s", topic.c_str());
        return false;
    }
    subscriptions.erase(topic);
    return true;
}

} // namespace MqttClient