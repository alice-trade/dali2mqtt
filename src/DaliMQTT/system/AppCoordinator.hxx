// Copyright (c) 2026 Alice-Trade Inc.
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef DALIMQTT_APPCOORDINATOR_HXX
#define DALIMQTT_APPCOORDINATOR_HXX

#include "system/SystemEvent.hxx"
#include "network/NetworkPlatform.hxx"
#include "dali/DaliService.hxx"
#include "mqtt/MqttBridge.hxx"
#include "mqtt/MqttClient.hxx"
#include "system/ConfigStore.hxx"
#include "system/OtaService.hxx"
#include "system/SyslogService.hxx"
#include "system/SystemControls.hxx"
#include "system/SystemScheduler.hxx"
#include "system/WebUIController.hxx"
#include <atomic>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

namespace daliMQTT {

enum class AppState : uint8_t { Uninitialized, Provisioning, ConnectingNetwork, RunningNormal };

class AppCoordinator {
  public:
    struct Dependencies {
        ConfigStore& config;
        SystemControls& controls;
        SystemScheduler& scheduler;
        SyslogService& syslog;
        OtaService& ota;
        NetworkPlatform& network;
        DaliService& dali;
        MqttClient& mqttClient;
        MqttBridge& mqttBridge;
        WebUIController& webUi;
        QueueHandle_t systemQueue{nullptr};
    };

    explicit AppCoordinator(const Dependencies& deps);
    ~AppCoordinator() = default;

    AppCoordinator(const AppCoordinator&) = delete;
    AppCoordinator& operator=(const AppCoordinator&) = delete;

    void start();
    void handleConfigReload() const;
    void factoryReset() const;
    inline void handleSystemEvent(SystemEventType event);
    [[nodiscard]] inline AppState getState() const noexcept;
    inline void eventLoop(uint32_t waitMs);

  private:
    void handleNetworkConnected();
    void handleNetworkDisconnected();

    static void onFactoryResetRequestedEntry(void* ctx);
    static void onMinuteTelemetry(void* ctx);
    static void onBusHealthCheck(void* ctx);
    static void onOtaCheck(void* ctx);

    Dependencies m_deps;
    std::atomic<AppState> m_state{AppState::Uninitialized};
    int64_t m_lastTickSec{0};
};

} // namespace daliMQTT

#include "system/AppCoordinator.icc"

#endif // DALIMQTT_APPCOORDINATOR_HXX