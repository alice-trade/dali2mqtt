// Copyright (c) 2026 Alice-Trade Inc.
// SPDX-License-Identifier: GPL-3.0-or-later

#include "mqtt/MqttClient.hxx"
#include <cstring>
#include <esp_crt_bundle.h>
#include <esp_log.h>
#include <string_view>

namespace daliMQTT {

static constexpr char TAG[] = "MqttClient";

MqttClient::MqttClient() = default;

MqttClient::~MqttClient() {
    disconnect();
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_clientHandle) {
        esp_mqtt_client_destroy(m_clientHandle);
        m_clientHandle = nullptr;
    }
}

esp_err_t MqttClient::init(const ConfigStructure& config) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_clientHandle) {
        esp_mqtt_client_destroy(m_clientHandle);
        m_clientHandle = nullptr;
    }

    if (config.mqttUri.empty()) {
        ESP_LOGW(TAG, "MQTT Broker URI is empty. MQTT Client paused.");
        return ESP_ERR_INVALID_ARG;
    }

    char lwtTopic[128];
    snprintf(lwtTopic, sizeof(lwtTopic), "%s%s", config.mqttBaseTopic.c_str(),
             CONFIG_DALI2MQTT_MQTT_AVAILABILITY_TOPIC);

    esp_mqtt_client_config_t mqttCfg{};
    mqttCfg.session.protocol_ver = MQTT_PROTOCOL_V_3_1_1;
    mqttCfg.broker.address.uri = config.mqttUri.c_str();
    mqttCfg.credentials.client_id = config.clientId.c_str();

    if (!config.mqttUser.empty())
        mqttCfg.credentials.username = config.mqttUser.c_str();
    if (!config.mqttPass.empty())
        mqttCfg.credentials.authentication.password = config.mqttPass.c_str();

    std::string_view uriSv(config.mqttUri.c_str());
    const bool isTls =
        uriSv.starts_with("mqtts://") || uriSv.starts_with("ssl://") || uriSv.find(":8883") != std::string_view::npos;

    if (!config.mqttCaCert.empty()) {
        mqttCfg.broker.verification.certificate = config.mqttCaCert.c_str();
        ESP_LOGI(TAG, "MQTT TLS: Using CA certificate (%zu bytes)", config.mqttCaCert.length());
    } else if (isTls) {
        mqttCfg.broker.verification.crt_bundle_attach = esp_crt_bundle_attach;
        ESP_LOGI(TAG, "MQTT TLS: Using Root CRT Bundle");
    }

    mqttCfg.session.last_will.topic = lwtTopic;
    mqttCfg.session.last_will.msg = CONFIG_DALI2MQTT_MQTT_PAYLOAD_OFFLINE;
    mqttCfg.session.last_will.qos = 1;
    mqttCfg.session.last_will.retain = 1;

    mqttCfg.task.stack_size = 8192;

    m_clientHandle = esp_mqtt_client_init(&mqttCfg);
    if (!m_clientHandle) {
        ESP_LOGE(TAG, "Failed to initialize ESP-MQTT client");
        return ESP_FAIL;
    }

    esp_mqtt_client_register_event(m_clientHandle, MQTT_EVENT_ANY, mqttEventHandler, this);
    m_status.store(MqttStatus::Disconnected);

    ESP_LOGI(TAG, "MQTT Client initialized (URI: %s, ClientID: %s, TLS: %s)", config.mqttUri.c_str(),
             config.clientId.c_str(), isTls ? "YES" : "NO");
    return ESP_OK;
}

void MqttClient::connect() {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_clientHandle && m_status.load() == MqttStatus::Disconnected) {
        m_status.store(MqttStatus::Connecting);
        esp_mqtt_client_start(m_clientHandle);
    }
}

void MqttClient::disconnect() {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_clientHandle) {
        m_status.store(MqttStatus::Disconnected);
        esp_mqtt_client_stop(m_clientHandle);
    }
}

void MqttClient::reload(const ConfigStructure& config) {
    disconnect();
    init(config);
    connect();
}

esp_err_t MqttClient::publish(const char* topic, const char* payload, const int qos, const bool retain) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_clientHandle || !isConnected())
        return ESP_ERR_INVALID_STATE;
    const int len = payload ? static_cast<int>(strlen(payload)) : 0;
    const int msgId = esp_mqtt_client_publish(m_clientHandle, topic, payload, len, qos, retain ? 1 : 0);
    return (msgId >= 0) ? ESP_OK : ESP_FAIL;
}

esp_err_t MqttClient::subscribe(const char* topic, const int qos) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_clientHandle || !isConnected())
        return ESP_ERR_INVALID_STATE;
    const int msgId = esp_mqtt_client_subscribe(m_clientHandle, topic, qos);
    return (msgId >= 0) ? ESP_OK : ESP_FAIL;
}

void MqttClient::mqttEventHandler(void* handlerArgs, esp_event_base_t, int32_t eventId, void* eventData) {
    auto* self = static_cast<MqttClient*>(handlerArgs);
    auto* event = static_cast<esp_mqtt_event_handle_t>(eventData);

    switch (static_cast<esp_mqtt_event_id_t>(eventId)) {
    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG, "MQTT connected successfully");
        self->m_status.store(MqttStatus::Connected);
        if (self->m_connectedCb)
            self->m_connectedCb(self->m_connectedCtx);
        break;

    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGW(TAG, "MQTT disconnected");
        self->m_status.store(MqttStatus::Disconnected);
        if (self->m_disconnectedCb)
            self->m_disconnectedCb(self->m_disconnectedCtx);
        break;

    case MQTT_EVENT_DATA:
        if (self->m_dataCb && event->topic && event->data) {
            self->m_dataCb(event->topic, event->topic_len, event->data, event->data_len, self->m_dataCtx);
        }
        break;

    case MQTT_EVENT_ERROR:
        ESP_LOGE(TAG, "MQTT transport error (Check TLS/Certificates/Credentials)");
        break;

    default:
        break;
    }
}

} // namespace daliMQTT