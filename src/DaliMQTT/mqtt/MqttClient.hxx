// Copyright (c) 2026 Alice-Trade Inc.
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef DALIMQTT_MQTTCLIENT_HXX
#define DALIMQTT_MQTTCLIENT_HXX

#include "system/ConfigStructure.hxx"
#include <atomic>
#include <esp_err.h>
#include <mqtt_client.h>
#include <mutex>
#include <string_view>

namespace daliMQTT {

enum class MqttStatus : uint8_t { Disconnected, Connecting, Connected };

using MqttConnectedCallback = void (*)(void* userCtx);
using MqttDisconnectedCallback = void (*)(void* userCtx);
using MqttDataCallback = void (*)(const char* topic, int topicLen, const char* data, int dataLen, void* userCtx);

class MqttClient {
  public:
    MqttClient();
    ~MqttClient();

    MqttClient(const MqttClient&) = delete;
    MqttClient& operator=(const MqttClient&) = delete;

    esp_err_t init(const ConfigStructure& config);
    void connect();
    void disconnect();
    void reload(const ConfigStructure& config);

    esp_err_t publish(const char* topic, const char* payload, int qos = 0, bool retain = false);
    esp_err_t subscribe(const char* topic, int qos = 0);

    [[nodiscard]] inline MqttStatus getStatus() const noexcept;
    [[nodiscard]] inline bool isConnected() const noexcept;

    inline void setConnectedCallback(MqttConnectedCallback cb, void* ctx) noexcept;
    inline void setDisconnectedCallback(MqttDisconnectedCallback cb, void* ctx) noexcept;
    inline void setDataCallback(MqttDataCallback cb, void* ctx) noexcept;

  private:
    static void mqttEventHandler(void* handlerArgs, esp_event_base_t base, int32_t eventId, void* eventData);

    mutable std::mutex m_mutex{};
    esp_mqtt_client_handle_t m_clientHandle{nullptr};
    std::atomic<MqttStatus> m_status{MqttStatus::Disconnected};

    MqttConnectedCallback m_connectedCb{nullptr};
    void* m_connectedCtx{nullptr};

    MqttDisconnectedCallback m_disconnectedCb{nullptr};
    void* m_disconnectedCtx{nullptr};

    MqttDataCallback m_dataCb{nullptr};
    void* m_dataCtx{nullptr};
};

} // namespace daliMQTT

#include "mqtt/MqttClient.icc"

#endif // DALIMQTT_MQTTCLIENT_HXX