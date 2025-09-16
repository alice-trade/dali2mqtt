#include <esp_log.h>
#include "MQTTClient.hxx"
#include "sdkconfig.h"

namespace daliMQTT
{
    static constexpr char  TAG[] = "MQTTClient";


    void MQTTClient::init(const std::string& uri, const std::string& client_id, const std::string& availability_topic, const std::string& username, const std::string& password) {
        esp_mqtt_client_config_t mqtt_cfg = {};
        mqtt_cfg.broker.address.uri = uri.c_str();
        mqtt_cfg.credentials.client_id = client_id.c_str();
        if (!username.empty()) {
            mqtt_cfg.credentials.username = username.c_str();
        }
        if (!password.empty()) {
            mqtt_cfg.credentials.authentication.password = password.c_str();
        }
        mqtt_cfg.session.last_will.topic = availability_topic.c_str();
        mqtt_cfg.session.last_will.msg = CONFIG_DALI2MQTT_MQTT_PAYLOAD_OFFLINE;
        mqtt_cfg.session.last_will.qos = 1;
        mqtt_cfg.session.last_will.retain = true;

        client_handle = esp_mqtt_client_init(&mqtt_cfg);
        esp_mqtt_client_register_event(client_handle, MQTT_EVENT_ANY, mqttEventHandler, this);
        status = MqttStatus::DISCONNECTED;
    }

    void MQTTClient::connect()
    {
        if (client_handle) {
            status = MqttStatus::CONNECTING;
            esp_mqtt_client_start(client_handle);
        }
    }

    void MQTTClient::disconnect()
    {
        if (client_handle) {
            status = MqttStatus::DISCONNECTED;
            esp_mqtt_client_stop(client_handle);
        }
    }

    MqttStatus MQTTClient::getStatus() const {
        return status;
    }

    void MQTTClient::publish(const std::string& topic, const std::string& payload, int qos, bool retain) const
    {
        if (!client_handle) return;
        esp_mqtt_client_publish(client_handle, topic.c_str(), payload.c_str(), payload.length(), qos, retain);
    }

    void MQTTClient::subscribe(const std::string& topic, int qos) const
    {
        if (!client_handle) return;
        esp_mqtt_client_subscribe(client_handle, topic.c_str(), qos);
    }

    void MQTTClient::mqttEventHandler(void* handler_args, [[maybe_unused]] esp_event_base_t base, int32_t event_id, void* event_data) {
        auto* client = static_cast<MQTTClient*>(handler_args);
        auto const* event = static_cast<esp_mqtt_event_handle_t>(event_data);
        if (!client || !event) return;

        switch (static_cast<esp_mqtt_event_id_t>(event_id)) {
            case MQTT_EVENT_CONNECTED:
                ESP_LOGI(TAG, "MQTT_EVENT_CONNECTED");
                client->status = MqttStatus::CONNECTED;
                if(client->onConnected) client->onConnected();
                break;
            case MQTT_EVENT_DISCONNECTED:
                ESP_LOGW(TAG, "MQTT_EVENT_DISCONNECTED");
                client->status = MqttStatus::DISCONNECTED;
                if(client->onDisconnected) client->onDisconnected();
                break;
            case MQTT_EVENT_SUBSCRIBED:
                ESP_LOGI(TAG, "MQTT_EVENT_SUBSCRIBED, msg_id=%d", event->msg_id);
                break;
            case MQTT_EVENT_UNSUBSCRIBED:
                ESP_LOGI(TAG, "MQTT_EVENT_UNSUBSCRIBED, msg_id=%d", event->msg_id);
                break;
            case MQTT_EVENT_DATA:
                ESP_LOGI(TAG, "MQTT_EVENT_DATA");
                if (client->onData) {
                    std::string topic(event->topic, event->topic_len);
                    std::string data(event->data, event->data_len);
                    client->onData(topic, data);
                }
                break;
            case MQTT_EVENT_ERROR:
                ESP_LOGE(TAG, "MQTT_EVENT_ERROR");
                break;
            default:
                ESP_LOGI(TAG, "Other event id:%d", event->event_id);
                break;
        }
    }
} // daliMQTT