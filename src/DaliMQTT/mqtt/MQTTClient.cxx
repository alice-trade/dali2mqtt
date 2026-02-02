#include "mqtt/MQTTClient.hxx"
#include "mqtt/MQTTCommandProcess.hxx"
namespace daliMQTT
{
    static constexpr char  TAG[] = "MQTTClient";

    void MQTTClient::init(const std::string& uri, const std::string& client_id, const std::string& availability_topic, const std::string& username, const std::string& password) {

        esp_mqtt_client_config_t mqtt_cfg = {};
        mqtt_cfg.session.protocol_ver = MQTT_PROTOCOL_V_3_1_1;
        mqtt_cfg.broker.address.uri = uri.c_str();
        mqtt_cfg.credentials.client_id = client_id.c_str();
        if (!username.empty()) {
            mqtt_cfg.credentials.username = username.data();
        }
        if (!password.empty()) {
            mqtt_cfg.credentials.authentication.password = password.data();
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

    void MQTTClient::publish(const std::string& topic, const std::string& payload, const int qos, const bool retain) const
    {
        if (!client_handle) return;
        esp_mqtt_client_publish(client_handle, topic.c_str(), payload.c_str(), payload.length(), qos, retain);
    }

    void MQTTClient::subscribe(const std::string& topic, const int qos) const
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
                MQTTCommandProcess::Instance().enqueueMqttMessage(
                   event->topic,
                   event->topic_len,
                   event->data,
                   event->data_len
               );
                break;
            case MQTT_EVENT_ERROR:
                ESP_LOGE(TAG, "MQTT_EVENT_ERROR");
                break;
            default:
                ESP_LOGI(TAG, "Other event id:%d", event->event_id);
                break;
        }
    }
    void MQTTClient::reloadConfig(const std::string& uri, const std::string& client_id, const std::string& username, const std::string& password, const std::string& availability_topic) {
        disconnect();
        if (client_handle) {
            esp_mqtt_client_destroy(client_handle);
            client_handle = nullptr;
        }
        init(uri, client_id, availability_topic, username, password);
        connect();
    }
} // daliMQTT