// Copyright (c) 2026 Alice-Trade Inc.
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef DALIMQTT_MQTTBRIDGE_HXX
#define DALIMQTT_MQTTBRIDGE_HXX

#include "network/NetworkPlatform.hxx"
#include "dali/DaliBusEngine.hxx"
#include "dali/DaliDeviceRegistry.hxx"
#include "mqtt/HomeAssistantDiscovery.hxx"
#include "mqtt/MqttClient.hxx"
#include "mqtt/MqttMessage.hxx"
#include "system/ConfigStore.hxx"
#include "system/OtaService.hxx"
#include <etl/string.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include <string_view>

namespace daliMQTT {

class MqttBridge {
  public:
    static constexpr size_t CMD_QUEUE_CAPACITY = 24;

    MqttBridge(MqttClient& mqtt,
               DaliDeviceRegistry& daliRegistry,
               DaliBusEngine& daliBus,
               ConfigStore& config,
               OtaService& ota,
                const NetworkPlatform& network);
    ~MqttBridge();

    MqttBridge(const MqttBridge&) = delete;
    MqttBridge& operator=(const MqttBridge&) = delete;

    esp_err_t start();
    void stop();

    void publishHomeAssistantDiscovery() const;
    bool enqueueIncomingMessage(const char* topic, int topicLen, const char* data, int dataLen) const;
    void publishTelemetry() const;

  private:
    static void onMqttConnectedBridge(void* ctx);
    static void onMqttDataReceivedBridge(const char* topic, int tLen, const char* data, int dLen, void* ctx);

    static void onDaliDeviceStateChanged(const DeviceStateChangeEvent& event, void* ctx);
    static void onDaliGroupStateChanged(const GroupStateChangeEvent& event, void* ctx);
    static void onDaliInputEvent(const InputDeviceEvent& event, void* ctx);
    static void onOtaProgressChanged(const OtaProgressEvent& event, void* ctx);
    static void onOtaVersionReceived(const OtaVersionInfo& info, void* ctx);

    static void bridgeTaskRunner(void* arg);
    [[noreturn]] void bridgeWorkerLoop() const;

    void routeIncomingCommand(std::string_view topic, std::string_view payload) const;
    void handleLightCommand(std::string_view targetPath, std::string_view payload) const;
    void handleGroupConfigCommand(std::string_view payload) const;
    void handleGroupConfigGetCommand() const;
    void handleGroupRefreshCommand() const;
    void handleSceneCommand(std::string_view busStr, std::string_view payload) const;
    void handleRawDaliCommand(std::string_view payload) const;
    void handleSyncCommand(std::string_view payload) const;
    void handleConfigGetCommand() const;
    void handleConfigSetCommand(std::string_view payload) const;
    void handleSceneConfigGetCommand(std::string_view payload) const;
    void handleSceneConfigSetCommand(std::string_view payload) const;
    void handleNamesGetCommand() const;
    void handleNamesSetCommand(std::string_view payload) const;
    void replayAllCachedStates() const;
    void publishOtaState(const char* latestVersion, const char* releaseUrl, bool inProgress, uint8_t pct) const;
    void handleHomeAssistantStatus(std::string_view payload) const;

    void handleBusScanCommand() const;
    void handleBusInitializeCommand() const;
    void handleInputDeviceInitializeCommand() const;
    void publishBusSyncStatus(const char* status, const char* lastAction = nullptr) const;
    
    static void onDaliDeviceAttributesLoaded(const ControlGear& gear, void* ctx);
    void publishDeviceAttributes(const ControlGear& gear) const;
    mutable std::atomic<bool> m_busOperationBusy{false};

    void publishDeviceGroups(DaliLongAddress_t longAddr, const GroupMask& groups) const;
    void publishAllDeviceGroups() const;

    MqttClient& m_mqtt;
    DaliDeviceRegistry& m_daliRegistry;
    DaliBusEngine& m_daliBus;
    ConfigStore& m_config;
    OtaService& m_ota;
    const NetworkPlatform& m_network;
    HomeAssistantDiscovery m_discovery;

    mutable std::array<char, 1024> m_bridgeScratchpad{};
    mutable MqttIncomingMessage m_currentMsg{};
    mutable std::atomic<bool> m_replayInProgress{false};

    etl::string<64> m_baseTopic{"dali_bridge"};
    QueueHandle_t m_cmdQueue{nullptr};
    TaskHandle_t m_taskHandle{nullptr};
};

} // namespace daliMQTT

#include "mqtt/MqttBridge.icc"

#endif // DALIMQTT_MQTTBRIDGE_HXX