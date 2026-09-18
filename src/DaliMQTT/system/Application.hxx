// Copyright (c) 2026 Alice-Trade Inc.
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef DALIMQTT_APPLICATION_HXX
#define DALIMQTT_APPLICATION_HXX

#include "dali/DaliService.hxx"
#include "mqtt/MqttBridge.hxx"
#include "mqtt/MqttClient.hxx"
#include "network/NetworkPlatform.hxx"
#include "system/AppCoordinator.hxx"
#include "system/ConfigStore.hxx"
#include "system/OtaService.hxx"
#include "system/SyslogService.hxx"
#include "system/SystemControls.hxx"
#include "system/SystemEvent.hxx"
#include "system/SystemScheduler.hxx"
#include "system/WebUIController.hxx"
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

namespace daliMQTT {

struct Application {
    QueueHandle_t systemQueue{nullptr};
    ConfigStore config;
    SystemControls controls;
    SyslogService syslog;
    SystemScheduler scheduler;
    OtaService ota;
    NetworkPlatform network;
    DaliService dali;
    MqttClient mqttClient;

    WebUIController webUi;
    MqttBridge mqttBridge;
    AppCoordinator coordinator;

    Application()
        : systemQueue(xQueueCreate(16, sizeof(SystemEventType))),
          webUi(config, network, dali.Registry(), mqttClient, ota),
          mqttBridge(mqttClient, dali.Registry(), dali.Bus(), config, ota, network),
          coordinator(AppCoordinator::Dependencies{.config = config,
                                                   .controls = controls,
                                                   .scheduler = scheduler,
                                                   .syslog = syslog,
                                                   .ota = ota,
                                                   .network = network,
                                                   .dali = dali,
                                                   .mqttClient = mqttClient,
                                                   .mqttBridge = mqttBridge,
                                                   .webUi = webUi,
                                                   .systemQueue = systemQueue}) {}

    ~Application() {
        if (systemQueue) {
            vQueueDelete(systemQueue);
            systemQueue = nullptr;
        }
    }
};

} // namespace daliMQTT

#endif // DALIMQTT_APPLICATION_HXX
