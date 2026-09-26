// Copyright (c) 2026 Alice-Trade Inc.
// SPDX-License-Identifier: GPL-3.0-or-later

#include "mqtt/MqttBridge.hxx"

#include "system/ConfigJson.hxx"
#include "utils/DaliLongAddrConversions.hxx"
#include "utils/DaliSensorMath.hxx"
#include "utils/NvsHandle.hxx"
#include <ArduinoJson.h>
#include <charconv>
#include <esp_log.h>

namespace daliMQTT {

static constexpr char TAG[] = "MqttBridge";

MqttBridge::MqttBridge(MqttClient& mqtt, DaliDeviceRegistry& daliRegistry, DaliBusEngine& daliBus, ConfigStore& config,
                       OtaService& ota, const NetworkPlatform& network)
    : m_mqtt(mqtt), m_daliRegistry(daliRegistry), m_daliBus(daliBus), m_config(config), m_ota(ota), m_network(network),
      m_discovery(m_mqtt, m_daliRegistry) {
    m_cmdQueue = xQueueCreate(CMD_QUEUE_CAPACITY, sizeof(MqttIncomingMessage));
}

MqttBridge::~MqttBridge() {
    stop();
    if (m_cmdQueue)
        vQueueDelete(m_cmdQueue);
}

esp_err_t MqttBridge::start() {
    const auto cfg = m_config.get();
    m_baseTopic = cfg->mqttBaseTopic;

    m_daliRegistry.setDeviceStateCallback(&onDaliDeviceStateChanged, this);
    m_daliRegistry.setGroupStateCallback(&onDaliGroupStateChanged, this);
    m_daliRegistry.setInputEventCallback(&onDaliInputEvent, this);
    m_daliRegistry.setDeviceAttributesCallback(&onDaliDeviceAttributesLoaded, this);

    m_ota.setProgressCallback(&onOtaProgressChanged, this);
    m_ota.setVersionCallback(&onOtaVersionReceived, this);
    m_mqtt.setConnectedCallback(&onMqttConnectedBridge, this);
    m_mqtt.setDataCallback(&onMqttDataReceivedBridge, this);

    if (!m_taskHandle) {
        const BaseType_t res = xTaskCreate(bridgeTaskRunner, "mqtt_bridge_task", 6144, this, 6, &m_taskHandle);
        if (res != pdPASS) {
            ESP_LOGE(TAG, "Failed to start bridge worker task");
            return ESP_ERR_NO_MEM;
        }
    }
    ESP_LOGI(TAG, "MQTT Bridge initialized (Base Topic: %s)", m_baseTopic.c_str());
    return ESP_OK;
}

void MqttBridge::stop() {
    if (m_taskHandle) {
        vTaskDelete(m_taskHandle);
        m_taskHandle = nullptr;
    }
}

void MqttBridge::publishHomeAssistantDiscovery() const {
    const auto cfg = m_config.get();
    m_discovery.publishAll(*cfg);
}

void MqttBridge::onMqttConnectedBridge(void* ctx) {
    const auto* self = static_cast<MqttBridge*>(ctx);
    const auto cfg = self->m_config.get();

    char avTopic[128];
    snprintf(avTopic, sizeof(avTopic), "%s%s", self->m_baseTopic.c_str(), CONFIG_DALI2MQTT_MQTT_AVAILABILITY_TOPIC);
    self->m_mqtt.publish(avTopic, CONFIG_DALI2MQTT_MQTT_PAYLOAD_ONLINE, 1, true);

    char metaTopic[128];

    snprintf(metaTopic, sizeof(metaTopic), "%s/version", self->m_baseTopic.c_str());
    self->m_mqtt.publish(metaTopic, DALIMQTT_VERSION, 0, true);

#ifdef DALIMQTT_CONFIGURED_TIMESTAMP
    snprintf(metaTopic, sizeof(metaTopic), "%s/build_time", self->m_baseTopic.c_str());
    self->m_mqtt.publish(metaTopic, DALIMQTT_CONFIGURED_TIMESTAMP, 0, true);
#endif

    snprintf(metaTopic, sizeof(metaTopic), "%s/ip_addr", self->m_baseTopic.c_str());
    self->m_mqtt.publish(metaTopic, self->m_network.getIpAddress().c_str(), 0, true);

    char topicBuf[128];

    snprintf(topicBuf, sizeof(topicBuf), "%s/light/+/set", self->m_baseTopic.c_str());
    self->m_mqtt.subscribe(topicBuf);

    snprintf(topicBuf, sizeof(topicBuf), "%s/light/broadcast/set", self->m_baseTopic.c_str());
    self->m_mqtt.subscribe(topicBuf);

    snprintf(topicBuf, sizeof(topicBuf), "%s/light/bus/+/short/+/set", self->m_baseTopic.c_str());
    self->m_mqtt.subscribe(topicBuf);

    snprintf(topicBuf, sizeof(topicBuf), "%s/light/bus/+/group/+/set", self->m_baseTopic.c_str());
    self->m_mqtt.subscribe(topicBuf);

    snprintf(topicBuf, sizeof(topicBuf), "%s/scene/bus/+/set", self->m_baseTopic.c_str());
    self->m_mqtt.subscribe(topicBuf);

    snprintf(topicBuf, sizeof(topicBuf), "%s/cmd/send", self->m_baseTopic.c_str());
    self->m_mqtt.subscribe(topicBuf);

    snprintf(topicBuf, sizeof(topicBuf), "%s/cmd/sync", self->m_baseTopic.c_str());
    self->m_mqtt.subscribe(topicBuf);

    snprintf(topicBuf, sizeof(topicBuf), "%s/cmd/query", self->m_baseTopic.c_str());
    self->m_mqtt.subscribe(topicBuf);

    snprintf(topicBuf, sizeof(topicBuf), "%s/config/get", self->m_baseTopic.c_str());
    self->m_mqtt.subscribe(topicBuf);
    snprintf(topicBuf, sizeof(topicBuf), "%s/config/set", self->m_baseTopic.c_str());
    self->m_mqtt.subscribe(topicBuf);

    snprintf(topicBuf, sizeof(topicBuf), "%s/config/group/set", self->m_baseTopic.c_str());
    self->m_mqtt.subscribe(topicBuf);
    snprintf(topicBuf, sizeof(topicBuf), "%s/config/group/get", self->m_baseTopic.c_str());
    self->m_mqtt.subscribe(topicBuf);
    snprintf(topicBuf, sizeof(topicBuf), "%s/config/group/refresh", self->m_baseTopic.c_str());
    self->m_mqtt.subscribe(topicBuf);

    snprintf(topicBuf, sizeof(topicBuf), "%s/config/scene/get", self->m_baseTopic.c_str());
    self->m_mqtt.subscribe(topicBuf);
    snprintf(topicBuf, sizeof(topicBuf), "%s/config/scene/set", self->m_baseTopic.c_str());
    self->m_mqtt.subscribe(topicBuf);

    snprintf(topicBuf, sizeof(topicBuf), "%s/config/names/get", self->m_baseTopic.c_str());
    self->m_mqtt.subscribe(topicBuf);
    snprintf(topicBuf, sizeof(topicBuf), "%s/config/names/set", self->m_baseTopic.c_str());
    self->m_mqtt.subscribe(topicBuf);

    snprintf(topicBuf, sizeof(topicBuf), "%s/config/bus/scan", self->m_baseTopic.c_str());
    self->m_mqtt.subscribe(topicBuf);
    snprintf(topicBuf, sizeof(topicBuf), "%s/config/bus/initialize", self->m_baseTopic.c_str());
    self->m_mqtt.subscribe(topicBuf);

    snprintf(topicBuf, sizeof(topicBuf), "%s/config/input_device/initialize", self->m_baseTopic.c_str());
    self->m_mqtt.subscribe(topicBuf);

    snprintf(topicBuf, sizeof(topicBuf), "%s/config/discovery/publish", self->m_baseTopic.c_str());
    self->m_mqtt.subscribe(topicBuf);

    snprintf(topicBuf, sizeof(topicBuf), "%s/update/check", self->m_baseTopic.c_str());
    self->m_mqtt.subscribe(topicBuf);

    snprintf(topicBuf, sizeof(topicBuf), "%s/update/install", self->m_baseTopic.c_str());
    self->m_mqtt.subscribe(topicBuf);

    const auto otaInfo = self->m_ota.getVersionInfo();
    if (otaInfo.lastCheckTsSec > 0) {
        self->publishOtaState(otaInfo.latestVersion.c_str(), otaInfo.releaseUrl.c_str(), false, 0);
    }

    if (cfg->hassDiscoveryEnabled) {
        self->publishHomeAssistantDiscovery();

        char haStatusTopic[64];
        snprintf(haStatusTopic, sizeof(haStatusTopic), "%s/status", cfg->hassDiscoveryPrefix.c_str());
        self->m_mqtt.subscribe(haStatusTopic);
    }
}

void MqttBridge::onMqttDataReceivedBridge(const char* topic, const int tLen, const char* data, int dLen, void* ctx) {
    const auto* self = static_cast<MqttBridge*>(ctx);
    self->enqueueIncomingMessage(topic, tLen, data, dLen);
}

void MqttBridge::onDaliDeviceStateChanged(const DeviceStateChangeEvent& event, void* ctx) {
    const auto* self = static_cast<MqttBridge*>(ctx);
    const auto addrStr = utils::longAddressToString(event.longAddress);

    char topic[128];
    snprintf(topic, sizeof(topic), "%s/light/%s/state", self->m_baseTopic.c_str(), addrStr.data());

    JsonDocument doc;
    doc["state"] = (event.level > 0) ? "ON" : "OFF";
    doc["brightness"] = event.level;
    doc["status_byte"] = event.statusByte;
    doc["available"] = event.available;
    doc["lamp_failure"] = (event.statusByte & 0x02) != 0;
    doc["gear_failure"] = (event.statusByte & 0x01) != 0;

    if (event.colorTemp.has_value())
        doc["color_temp"] = *event.colorTemp;
    if (event.rgb.has_value()) {
        const auto c = doc["color"].to<JsonObject>();
        c["r"] = event.rgb->r;
        c["g"] = event.rgb->g;
        c["b"] = event.rgb->b;
    }

    char payload[256];
    serializeJson(doc, payload, sizeof(payload));
    self->m_mqtt.publish(topic, payload, 0, false);
}

void MqttBridge::onDaliGroupStateChanged(const GroupStateChangeEvent& event, void* ctx) {
    const auto* self = static_cast<MqttBridge*>(ctx);

    char topic[128];
    snprintf(topic, sizeof(topic), "%s/light/bus/%d/group/%d/state", self->m_baseTopic.c_str(), event.busId,
             event.groupId);

    JsonDocument doc;
    doc["state"] = (event.level > 0) ? "ON" : "OFF";
    doc["brightness"] = event.level;

    if (event.colorTemp.has_value())
        doc["color_temp"] = *event.colorTemp;
    if (event.rgb.has_value()) {
        const auto c = doc["color"].to<JsonObject>();
        c["r"] = event.rgb->r;
        c["g"] = event.rgb->g;
        c["b"] = event.rgb->b;
    }

    char payload[256];
    serializeJson(doc, payload, sizeof(payload));
    self->m_mqtt.publish(topic, payload, 0, false);
}

void MqttBridge::onOtaProgressChanged(const OtaProgressEvent& event, void* ctx) {
    const auto* self = static_cast<MqttBridge*>(ctx);

    char topic[128];
    snprintf(topic, sizeof(topic), "%s/update/state", self->m_baseTopic.c_str());

    JsonDocument doc;
    doc["installed_version"] = DALIMQTT_VERSION;
    doc["in_progress"] = (event.status == OtaStatus::InProgress);
    if (event.status == OtaStatus::InProgress) {
        doc["update_percentage"] = event.percentage;
    }

    char payload[192];
    serializeJson(doc, payload, sizeof(payload));
    self->m_mqtt.publish(topic, payload, 0, false);
}

void MqttBridge::replayAllCachedStates() const {
    ESP_LOGI(TAG, "Replaying cached states from RAM to MQTT...");

    const auto devices = m_daliRegistry.getDevicesSnapshot();
    for (const auto& dev : devices) {
        if (const auto* gear = etl::get_if<ControlGear>(&dev)) {
            DeviceStateChangeEvent ev{.longAddress = gear->longAddress,
                                      .internalAddress = gear->internalAddress,
                                      .level = gear->currentLevel,
                                      .statusByte = gear->statusByte,
                                      .available = gear->available};
            if (gear->color.has_value()) {
                ev.colorTemp = gear->color->currentTc;
                ev.rgb = gear->color->currentRgb;
            }
            onDaliDeviceStateChanged(ev, const_cast<MqttBridge*>(this));
            if (gear->staticDataLoaded) {
                publishDeviceAttributes(*gear);
            }
            vTaskDelay(pdMS_TO_TICKS(2));
        }
    }

    for (uint8_t busId = 0; busId < DaliDeviceRegistry::BUS_COUNT; ++busId) {
        for (uint8_t g = 0; g < 16; ++g) {
            DaliGroupState gState = m_daliRegistry.getGroupState(busId, g);
            GroupStateChangeEvent ev{.busId = busId,
                                     .groupId = g,
                                     .level = gState.currentLevel,
                                     .colorTemp = gState.colorTemp,
                                     .rgb = gState.rgb};
            onDaliGroupStateChanged(ev, const_cast<MqttBridge*>(this));

            vTaskDelay(pdMS_TO_TICKS(2));
        }
    }
    publishAllDeviceGroups();
    ESP_LOGI(TAG, "Replay complete.");
}

void MqttBridge::onDaliInputEvent(const InputDeviceEvent& event, void* ctx) {
    const auto* self = static_cast<MqttBridge*>(ctx);

    auto typeStr = "short";
    char addrStr[16];

    if (event.addressType == InputAddressType::Short) {
        if (event.longAddress != InvalidLongAddr) {
            typeStr = "long";
            auto la = utils::longAddressToString(event.longAddress);
            snprintf(addrStr, sizeof(addrStr), "%s", la.data());
        } else {
            snprintf(addrStr, sizeof(addrStr), "%d", event.shortAddress);
        }
    } else {
        snprintf(addrStr, sizeof(addrStr), "%d", event.shortAddress);
    }

    char topic[128];
    snprintf(topic, sizeof(topic), "%s/event/%s/%s", self->m_baseTopic.c_str(), typeStr, addrStr);

    JsonDocument doc;
    doc["type"] = "event";
    doc["bus"] = event.busId;
    doc["address"] = event.shortAddress;
    doc["instance"] = event.instanceNumber;
    doc["instance_type"] = event.instanceType;
    doc["event_code"] = event.eventCode;

    if (event.instanceType == 4) {
        const uint8_t rawLux = static_cast<uint8_t>(event.eventCode & 0xFF);
        doc["illuminance_lux"] = utils::rawToLux(rawLux);
    } else if (event.instanceType == 3) {
        doc["occupied"] = utils::isOccupied(static_cast<uint8_t>(event.eventCode & 0xFF));
    }

    char payload[256];
    serializeJson(doc, payload, sizeof(payload));
    self->m_mqtt.publish(topic, payload, 0, false);
}

void MqttBridge::bridgeTaskRunner(void* arg) {
    static_cast<MqttBridge*>(arg)->bridgeWorkerLoop();
}

[[noreturn]] void MqttBridge::bridgeWorkerLoop() const {
    while (true) {
        if (xQueueReceive(m_cmdQueue, &m_currentMsg, portMAX_DELAY) == pdTRUE) {
            const std::string_view topic(m_currentMsg.topic.c_str(), m_currentMsg.topic.length());
            const std::string_view payload(m_currentMsg.payload.c_str(), m_currentMsg.payload.length());

            routeIncomingCommand(topic, payload);
        }
    }
}

void MqttBridge::handleHomeAssistantStatus(std::string_view payload) const {
    if (payload == "online") {
        ESP_LOGI(TAG, "Home Assistant is ONLINE (Birth Message received)!");
        vTaskDelay(pdMS_TO_TICKS(300));
        replayAllCachedStates();
    } else if (payload == "offline") {
        ESP_LOGW(TAG, "Home Assistant is OFFLINE (LWT)");
    }
}

void MqttBridge::routeIncomingCommand(std::string_view topic, std::string_view payload) const {
    const auto cfg = m_config.get();

    if (cfg->hassDiscoveryEnabled) {
        char haStatusTopic[64];
        snprintf(haStatusTopic, sizeof(haStatusTopic), "%s/status", cfg->hassDiscoveryPrefix.c_str());
        if (topic == haStatusTopic) {
            handleHomeAssistantStatus(payload);
            return;
        }
    }

    if (!topic.starts_with(m_baseTopic.c_str())) {
        return;
    }

    std::string_view subTopic = topic;
    subTopic.remove_prefix(m_baseTopic.length());

    if (subTopic.starts_with("/light/")) {
        subTopic.remove_prefix(7);
        handleLightCommand(subTopic, payload);
    } else if (subTopic.starts_with("/scene/bus/")) {
        subTopic.remove_prefix(11);
        handleSceneCommand(subTopic, payload);
    } else if (subTopic == "/config/get") {
        handleConfigGetCommand();
    } else if (subTopic == "/config/set") {
        handleConfigSetCommand(payload);
    } else if (subTopic == "/config/group/set") {
        handleGroupConfigCommand(payload);
    } else if (subTopic == "/config/group/get") {
        handleGroupConfigGetCommand();
    } else if (subTopic == "/config/group/refresh") {
        handleGroupRefreshCommand();
    } else if (subTopic == "/config/scene/get") {
        handleSceneConfigGetCommand(payload);
    } else if (subTopic == "/config/scene/set") {
        handleSceneConfigSetCommand(payload);
    } else if (subTopic == "/config/names/get") {
        handleNamesGetCommand();
    } else if (subTopic == "/config/names/set") {
        handleNamesSetCommand(payload);
    } else if (subTopic == "/config/bus/scan") {
        handleBusScanCommand();
    } else if (subTopic == "/config/bus/initialize") {
        handleBusInitializeCommand();
    } else if (subTopic == "/config/input_device/initialize") {
        handleInputDeviceInitializeCommand();
    } else if (subTopic == "/config/discovery/publish") {
        publishHomeAssistantDiscovery();
    } else if (subTopic == "/cmd/send") {
        handleRawDaliCommand(payload);
    } else if (subTopic == "/cmd/sync") {
        handleSyncCommand(payload);
    } else if (subTopic == "/update/check") {
        ESP_LOGI(TAG, "MQTT command: check for updates");
        m_ota.checkForUpdateAsync(cfg->otaBaseUrl.c_str());
    } else if (subTopic == "/update/install") {
        ESP_LOGI(TAG, "MQTT command: install update");
        m_ota.startUpdate(nullptr);
    } else if (subTopic == "/cmd/query") {
        ESP_LOGI(TAG, "External query command received, replaying state cache...");
        replayAllCachedStates();
    }
}

void MqttBridge::handleLightCommand(std::string_view targetPath, std::string_view payload) const {
    if (!targetPath.ends_with("/set"))
        return;
    targetPath.remove_suffix(4);

    JsonDocument doc;
    if (deserializeJson(doc, payload.data(), payload.size()) != DeserializationError::Ok)
        return;

    std::optional<bool> powerState;
    if (doc["state"].is<const char*>()) {
        const auto s = doc["state"].as<const char*>();
        if (strcmp(s, "ON") == 0)
            powerState = true;
        else if (strcmp(s, "OFF") == 0)
            powerState = false;
    }

    std::optional<uint8_t> brightness;
    if (doc["brightness"].is<int>()) {
        brightness = static_cast<uint8_t>(std::clamp(doc["brightness"].as<int>(), 0, 254));
    }

    if (targetPath == "broadcast") {
        if (brightness.has_value())
            m_daliBus.sendDAPC(DaliAddressType::Broadcast, 0, *brightness);
        else if (powerState.has_value()) {
            if (*powerState)
                m_daliBus.sendGearCommand(DaliAddressType::Broadcast, 0, OpCode::RecallMaxLevel);
            else
                m_daliBus.sendGearCommand(DaliAddressType::Broadcast, 0, OpCode::Off);
        }
        return;
    }

    if (targetPath.starts_with("bus/")) {
        targetPath.remove_prefix(4);
        const auto grpPos = targetPath.find("/group/");
        if (grpPos != std::string_view::npos) {
            uint8_t busId = 0;
            const auto busPart = targetPath.substr(0, grpPos);
            auto [p1, ec1] = std::from_chars(busPart.data(), busPart.data() + busPart.size(), busId);

            uint8_t groupId = 0;
            const auto grpPart = targetPath.substr(grpPos + 7);
            auto [p2, ec2] = std::from_chars(grpPart.data(), grpPart.data() + grpPart.size(), groupId);

            if (ec1 == std::errc{} && ec2 == std::errc{} && groupId < 16) {
                if (brightness.has_value())
                    m_daliRegistry.setGroupBrightness(busId, groupId, *brightness);
                else if (powerState.has_value())
                    m_daliRegistry.setGroupPower(busId, groupId, *powerState);
            }
            return;
        }

        const auto shortPos = targetPath.find("/short/");
        if (shortPos != std::string_view::npos) {
            uint8_t busId = 0;
            const auto busPart = targetPath.substr(0, shortPos);
            auto [p1, ec1] = std::from_chars(busPart.data(), busPart.data() + busPart.size(), busId);

            uint8_t sa = 0;
            const auto shortPart = targetPath.substr(shortPos + 7);
            auto [p2, ec2] = std::from_chars(shortPart.data(), shortPart.data() + shortPart.size(), sa);

            if (ec1 == std::errc{} && ec2 == std::errc{} && sa < 64) {
                auto longAddrOpt = m_daliRegistry.getLongAddress(DaliInternalAddr(busId, sa));
                if (longAddrOpt) {
                    if (brightness.has_value())
                        m_daliRegistry.setBrightness(*longAddrOpt, *brightness);
                    else if (powerState.has_value())
                        m_daliRegistry.setPower(*longAddrOpt, *powerState);
                }
            }
            return;
        }
    }

    const auto longAddrOpt = utils::stringToLongAddress(targetPath);
    if (!longAddrOpt)
        return;

    const DaliLongAddress_t longAddr = *longAddrOpt;

    if (doc["color_temp"].is<int>()) {
        m_daliRegistry.setColorTemp(longAddr, static_cast<uint16_t>(doc["color_temp"].as<int>()));
    }
    if (doc["color"].is<JsonObject>()) {
        JsonObject c = doc["color"];
        if (c["r"].is<int>() && c["g"].is<int>() && c["b"].is<int>()) {
            m_daliRegistry.setRgb(longAddr, c["r"], c["g"], c["b"]);
        }
    }

    if (brightness.has_value()) {
        m_daliRegistry.setBrightness(longAddr, *brightness);
    } else if (powerState.has_value()) {
        m_daliRegistry.setPower(longAddr, *powerState);
    }
}
void MqttBridge::handleGroupConfigGetCommand() const {
    const auto assignments = m_daliRegistry.getGroupAssignments();

    JsonDocument doc;
    for (const auto& [longAddr, groups] : assignments) {
        const auto laStr = utils::longAddressToString(longAddr);
        auto grpArr = doc[laStr.data()].to<JsonArray>();
        for (int i = 0; i < 16; ++i) {
            if (groups.test(i)) {
                grpArr.add(i);
            }
        }
    }

    char resTopic[128];
    snprintf(resTopic, sizeof(resTopic), "%s/config/group/get/response", m_baseTopic.c_str());
    char resBuf[1024];
    serializeJson(doc, resBuf, sizeof(resBuf));
    m_mqtt.publish(resTopic, resBuf, 0, false);
}

void MqttBridge::handleGroupRefreshCommand() const {
    ESP_LOGI(TAG, "MQTT requested group refresh from DALI bus...");
    xTaskCreate(
        [](void* arg) {
            auto* self = static_cast<const MqttBridge*>(arg);
            self->m_daliRegistry.refreshGroupAssignmentsFromBus();
            self->publishAllDeviceGroups();
            self->handleGroupConfigGetCommand();
            vTaskDelete(nullptr);
        },
        "mqtt_grp_ref", 4096, const_cast<MqttBridge*>(this), 4, nullptr);
}

void MqttBridge::handleGroupConfigCommand(std::string_view payload) const {
    JsonDocument doc;
    if (deserializeJson(doc, payload.data(), payload.size()) != DeserializationError::Ok)
        return;

    if (!doc["long_address"].is<const char*>() || !doc["group"].is<int>() || !doc["state"].is<const char*>())
        return;

    const auto longAddrOpt = utils::stringToLongAddress(doc["long_address"].as<const char*>());
    if (!longAddrOpt)
        return;

    const uint8_t group = doc["group"].as<uint8_t>();
    const bool assigned = (strcmp(doc["state"].as<const char*>(), "add") == 0);

    if (m_daliRegistry.setDeviceGroupMembership(*longAddrOpt, group, assigned) == ESP_OK) {
        const auto assignments = m_daliRegistry.getGroupAssignments();
        auto it = assignments.find(*longAddrOpt);
        if (it != assignments.end()) {
            publishDeviceGroups(*longAddrOpt, it->second);
        }
    }
}

void MqttBridge::handleSceneCommand(std::string_view busStr, std::string_view payload) const {
    uint8_t busId = 0;
    if (!busStr.empty()) {
        std::from_chars(busStr.data(), busStr.data() + busStr.size(), busId);
    }

    if (payload.starts_with("Scene ")) {
        const auto scenePart = payload.substr(6);
        uint8_t sceneId = 0;
        auto [ptr, ec] = std::from_chars(scenePart.data(), scenePart.data() + scenePart.size(), sceneId);

        if (ec == std::errc{} && sceneId < 16) {
            m_daliRegistry.activateScene(busId, sceneId);
        }
        return;
    }

    JsonDocument doc;
    if (deserializeJson(doc, payload.data(), payload.size()) != DeserializationError::Ok)
        return;

    if (doc["scene"].is<int>()) {
        m_daliRegistry.activateScene(busId, doc["scene"].as<uint8_t>());
    }
}

void MqttBridge::handleRawDaliCommand(std::string_view payload) const {
    JsonDocument doc;
    if (deserializeJson(doc, payload.data(), payload.size()) != DeserializationError::Ok)
        return;

    if (doc["addr"].is<int>() && doc["cmd"].is<int>()) {
        const uint32_t addr = doc["addr"].as<uint32_t>();
        const uint32_t cmd = doc["cmd"].as<uint32_t>();
        const uint8_t bits = doc["bits"].is<int>() ? doc["bits"].as<uint8_t>() : 16;
        const uint32_t rawData = (addr << 8) | cmd;

        if (!doc["tag"].isNull()) {
            const auto result = m_daliBus.queryRaw(rawData, bits);
            JsonDocument resp;
            resp["tag"] = doc["tag"];
            if (result) {
                resp["status"] = "ok";
                resp["response"] = *result;
            } else {
                resp["status"] = "no_reply";
            }
            char resPayload[128];
            serializeJson(resp, resPayload, sizeof(resPayload));

            char resTopic[128];
            snprintf(resTopic, sizeof(resTopic), "%s/cmd/res", m_baseTopic.c_str());
            m_mqtt.publish(resTopic, resPayload, 0, false);
        } else {
            m_daliBus.sendGearCommand(DaliAddressType::Short, addr, static_cast<OpCode>(cmd), doc["twice"].as<bool>());
        }
    }
}

void MqttBridge::handleSyncCommand(std::string_view payload) const {
    JsonDocument doc;
    if (deserializeJson(doc, payload.data(), payload.size()) != DeserializationError::Ok)
        return;

    if (doc["addr_type"].is<const char*>() && strcmp(doc["addr_type"].as<const char*>(), "broadcast") == 0) {
        uint32_t delayMs = doc["delay_ms"].is<uint32_t>() ? doc["delay_ms"].as<uint32_t>() : 200;
        uint32_t staggerMs = doc["stagger_ms"].is<uint32_t>() ? doc["stagger_ms"].as<uint32_t>() : 1400;
        m_daliRegistry.requestBroadcastSync(delayMs, staggerMs);
        return;
    }

    if (doc["address"].is<const char*>()) {
        const auto longAddrOpt = utils::stringToLongAddress(doc["address"].as<const char*>());
        if (longAddrOpt) {
            auto intAddr = m_daliRegistry.getInternalAddress(*longAddrOpt);
            if (intAddr) {
                m_daliRegistry.requestSync(*intAddr, doc["delay_ms"].as<uint32_t>());
            }
        }
    }
}

void MqttBridge::onOtaVersionReceived(const OtaVersionInfo& info, void* ctx) {
    const auto* self = static_cast<MqttBridge*>(ctx);
    self->publishOtaState(info.latestVersion.c_str(), info.releaseUrl.c_str(), false, 0);
}

void MqttBridge::publishOtaState(const char* latestVersion, const char* releaseUrl, const bool inProgress,
                                 const uint8_t pct) const {
    char topic[128];
    snprintf(topic, sizeof(topic), "%s/update/state", m_baseTopic.c_str());

    JsonDocument doc;
    doc["installed_version"] = DALIMQTT_VERSION;
    doc["latest_version"] = (latestVersion && strlen(latestVersion) > 0) ? latestVersion : DALIMQTT_VERSION;
    doc["update_available"] = (strcmp(doc["latest_version"], DALIMQTT_VERSION) != 0);
    doc["in_progress"] = inProgress;

    if (inProgress) {
        doc["update_percentage"] = pct;
    }
    if (releaseUrl && strlen(releaseUrl) > 0) {
        doc["release_url"] = releaseUrl;
    }

    char payload[256];
    serializeJson(doc, payload, sizeof(payload));
    m_mqtt.publish(topic, payload, 0, true);
}

void MqttBridge::publishTelemetry() const {
    if (!m_mqtt.isConnected())
        return;

    char topic[128];

    snprintf(topic, sizeof(topic), "%s/ip_addr", m_baseTopic.c_str());
    m_mqtt.publish(topic, m_network.getIpAddress().c_str(), 0, true);
}

void MqttBridge::handleConfigGetCommand() const {
    JsonDocument doc;
    ConfigJson::serialize(*m_config.get(), doc, /*maskSecrets=*/true);

    char resTopic[128];
    snprintf(resTopic, sizeof(resTopic), "%s/config/get/response", m_baseTopic.c_str());

    serializeJson(doc, m_bridgeScratchpad.data(), m_bridgeScratchpad.size());
    m_mqtt.publish(resTopic, m_bridgeScratchpad.data(), 0, false);
}

void MqttBridge::handleConfigSetCommand(std::string_view payload) const {
    JsonDocument doc;
    if (deserializeJson(doc, payload.data(), payload.size()) != DeserializationError::Ok) {
        return;
    }

    auto newCfg = *m_config.get();
    const auto result = ConfigJson::apply(doc, newCfg);

    char resTopic[128];
    snprintf(resTopic, sizeof(resTopic), "%s/config/set/response", m_baseTopic.c_str());

    if (!result.success) {
        char errBuf[128];
        snprintf(errBuf, sizeof(errBuf), R"({"status":"error","message":"%s"})", result.errorMessage);
        m_mqtt.publish(resTopic, errBuf, 0, false);
        return;
    }

    const esp_err_t err = m_config.save(newCfg);

    char resPayload[128];
    snprintf(resPayload, sizeof(resPayload), R"({"status":"%s","reboot":%s})", (err == ESP_OK) ? "ok" : "error",
             result.requiresReboot ? "true" : "false");
    m_mqtt.publish(resTopic, resPayload, 0, false);

    if (err != ESP_OK)
        return;

    if (result.hassDiscoveryToggled && !result.requiresReboot) {
        publishHomeAssistantDiscovery();
    }

    if (result.requiresReboot) {
        ESP_LOGW(TAG, "Critical configuration changed via MQTT. Rebooting in 1s...");
        xTaskCreate(
            [](void*) {
                vTaskDelay(pdMS_TO_TICKS(1000));
                esp_restart();
            },
            "mqtt_reboot_task", 2048, nullptr, 5, nullptr);
    }
}

void MqttBridge::handleSceneConfigGetCommand(std::string_view payload) const {
    JsonDocument reqDoc;
    if (deserializeJson(reqDoc, payload.data(), payload.size()) != DeserializationError::Ok)
        return;

    if (!reqDoc["scene"].is<int>())
        return;

    const uint8_t sceneId = reqDoc["scene"].as<uint8_t>();
    const uint8_t busId = reqDoc["bus"].is<int>() ? reqDoc["bus"].as<uint8_t>() : 0;

    const auto levels = m_daliRegistry.querySceneLevels(busId, sceneId);

    JsonDocument respDoc;
    respDoc["bus"] = busId;
    respDoc["scene"] = sceneId;
    const auto lObj = respDoc["levels"].to<JsonObject>();

    for (uint8_t sa = 0; sa < 64; ++sa) {
        if (levels[sa] != 255) {
            auto laOpt = m_daliRegistry.getLongAddress(DaliInternalAddr(busId, sa));
            if (laOpt) {
                lObj[utils::longAddressToString(*laOpt).data()] = levels[sa];
            }
        }
    }

    char resTopic[128];
    snprintf(resTopic, sizeof(resTopic), "%s/config/scene/get/response", m_baseTopic.c_str());
    char resBuf[1024];
    serializeJson(respDoc, resBuf, sizeof(resBuf));
    m_mqtt.publish(resTopic, resBuf, 0, false);
}

void MqttBridge::handleSceneConfigSetCommand(std::string_view payload) const {
    JsonDocument doc;
    if (deserializeJson(doc, payload.data(), payload.size()) != DeserializationError::Ok)
        return;

    if (!doc["scene"].is<int>())
        return;

    const uint8_t sceneId = doc["scene"].as<uint8_t>();
    const uint8_t busId = doc["bus"].is<int>() ? doc["bus"].as<uint8_t>() : 0;

    SceneLevels levels = m_daliRegistry.querySceneLevels(busId, sceneId);

    if (doc["levels"].is<JsonObject>()) {
        for (JsonPair kv : doc["levels"].as<JsonObject>()) {
            auto laOpt = utils::stringToLongAddress(kv.key().c_str());
            if (laOpt) {
                auto intAddr = m_daliRegistry.getInternalAddress(*laOpt);
                if (intAddr) {
                    levels[intAddr->shortAddr()] = kv.value().as<uint8_t>();
                }
            }
        }
    } else if (doc["address"].is<const char*>() && doc["level"].is<int>()) {
        auto laOpt = utils::stringToLongAddress(doc["address"].as<const char*>());
        if (laOpt) {
            auto intAddr = m_daliRegistry.getInternalAddress(*laOpt);
            if (intAddr) {
                levels[intAddr->shortAddr()] = doc["level"].as<uint8_t>();
            }
        }
    }

    const esp_err_t err = m_daliRegistry.saveSceneLevels(busId, sceneId, levels);

    char resTopic[128];
    snprintf(resTopic, sizeof(resTopic), "%s/config/scene/set/response", m_baseTopic.c_str());
    char resPayload[64];
    snprintf(resPayload, sizeof(resPayload), R"({"status":"%s","scene":%d})", (err == ESP_OK) ? "ok" : "error",
             sceneId);
    m_mqtt.publish(resTopic, resPayload, 0, false);
}

void MqttBridge::handleNamesGetCommand() const {
    const NvsHandle nvs("dali_names", NVS_READONLY);
    strncpy(m_bridgeScratchpad.data(), "{}", m_bridgeScratchpad.size());

    if (nvs) {
        size_t len = m_bridgeScratchpad.size();
        nvs_get_str(nvs.get(), "names_json", m_bridgeScratchpad.data(), &len);
    }

    char resTopic[128];
    snprintf(resTopic, sizeof(resTopic), "%s/config/names/get/response", m_baseTopic.c_str());
    m_mqtt.publish(resTopic, m_bridgeScratchpad.data(), 0, false);
}

void MqttBridge::handleNamesSetCommand(std::string_view payload) const {
    JsonDocument incoming;
    if (deserializeJson(incoming, payload.data(), payload.size()) != DeserializationError::Ok)
        return;

    // Считываем текущие имена в m_bridgeScratchpad
    strncpy(m_bridgeScratchpad.data(), "{}", m_bridgeScratchpad.size());
    {
        const NvsHandle readNvs("dali_names", NVS_READONLY);
        if (readNvs) {
            size_t len = m_bridgeScratchpad.size();
            nvs_get_str(readNvs.get(), "names_json", m_bridgeScratchpad.data(), &len);
        }
    }

    JsonDocument currentNames;
    deserializeJson(currentNames, static_cast<const char*>(m_bridgeScratchpad.data()));
    for (JsonPair kv : incoming.as<JsonObject>()) {
        currentNames[kv.key()] = kv.value();
    }
    serializeJson(currentNames, m_bridgeScratchpad.data(), m_bridgeScratchpad.size());

    NvsHandle writeNvs("dali_names", NVS_READWRITE);
    esp_err_t err = ESP_FAIL;
    if (writeNvs) {
        nvs_set_str(writeNvs.get(), "names_json", m_bridgeScratchpad.data());
        err = nvs_commit(writeNvs.get());
    }

    if (err == ESP_OK && m_config.get()->hassDiscoveryEnabled) {
        publishHomeAssistantDiscovery();
    }

    char resTopic[128];
    snprintf(resTopic, sizeof(resTopic), "%s/config/names/set/response", m_baseTopic.c_str());
    char resPayload[64];
    snprintf(resPayload, sizeof(resPayload), R"({"status":"%s"})", (err == ESP_OK) ? "ok" : "error");
    m_mqtt.publish(resTopic, resPayload, 0, false);
}

void MqttBridge::publishBusSyncStatus(const char* status, const char* lastAction) const {
    char topic[128];
    snprintf(topic, sizeof(topic), "%s/config/bus/sync_status", m_baseTopic.c_str());

    char payload[128];
    if (lastAction) {
        snprintf(payload, sizeof(payload), R"({"status":"%s","last_action":"%s"})", status, lastAction);
    } else {
        snprintf(payload, sizeof(payload), R"({"status":"%s"})", status);
    }

    m_mqtt.publish(topic, payload, 0, false);
}

void MqttBridge::handleBusScanCommand() const {
    if (m_busOperationBusy.exchange(true)) {
        ESP_LOGW(TAG, "Bus operation is already in progress. Ignoring scan request.");
        return;
    }

    ESP_LOGI(TAG, "Starting MQTT-initiated asynchronous DALI bus scan...");
    publishBusSyncStatus("scanning");

    xTaskCreate(
        [](void* arg) {
            auto* self = static_cast<const MqttBridge*>(arg);
            self->m_daliRegistry.scanBus();
            self->m_daliRegistry.refreshGroupAssignmentsFromBus();

            self->publishBusSyncStatus("idle", "scan_complete");
            ESP_LOGI(TAG, "MQTT-initiated DALI scan finished.");
            self->m_busOperationBusy.store(false);
            vTaskDelete(nullptr);
        },
        "mqtt_scan_task", 4096, const_cast<MqttBridge*>(this), 4, nullptr);
}

void MqttBridge::handleBusInitializeCommand() const {
    if (m_busOperationBusy.exchange(true)) {
        ESP_LOGW(TAG, "Bus operation is already in progress. Ignoring commissioning request.");
        return;
    }

    ESP_LOGI(TAG, "Starting MQTT-initiated DALI commissioning (Control Gear)...");
    publishBusSyncStatus("initializing");

    xTaskCreate(
        [](void* arg) {
            auto* self = static_cast<const MqttBridge*>(arg);
            self->m_daliRegistry.commissionNewDevices();
            self->m_daliRegistry.refreshGroupAssignmentsFromBus();

            self->publishBusSyncStatus("idle", "init_complete");
            ESP_LOGI(TAG, "MQTT-initiated DALI commissioning finished.");
            self->m_busOperationBusy.store(false);
            vTaskDelete(nullptr);
        },
        "mqtt_init_task", 4096, const_cast<MqttBridge*>(this), 4, nullptr);
}

void MqttBridge::handleInputDeviceInitializeCommand() const {
    if (m_busOperationBusy.exchange(true)) {
        ESP_LOGW(TAG, "Bus operation is already in progress. Ignoring input device init request.");
        return;
    }

    ESP_LOGI(TAG, "Starting MQTT-initiated DALI commissioning (24-bit Input Devices)...");
    publishBusSyncStatus("initializing");

    xTaskCreate(
        [](void* arg) {
            auto* self = static_cast<const MqttBridge*>(arg);
            self->m_daliRegistry.commission24BitDevices();
            self->m_daliRegistry.refreshGroupAssignmentsFromBus();

            self->publishBusSyncStatus("idle", "input_init_complete");
            ESP_LOGI(TAG, "MQTT-initiated Input Device commissioning finished.");
            self->m_busOperationBusy.store(false);
            vTaskDelete(nullptr);
        },
        "mqtt_inp_init_task", 4096, const_cast<MqttBridge*>(this), 4, nullptr);
}

void MqttBridge::onDaliDeviceAttributesLoaded(const ControlGear& gear, void* ctx) {
    const auto* self = static_cast<MqttBridge*>(ctx);
    self->publishDeviceAttributes(gear);
}

void MqttBridge::publishDeviceAttributes(const ControlGear& gear) const {
    const auto addrStr = utils::longAddressToString(gear.longAddress);

    char topic[128];
    snprintf(topic, sizeof(topic), "%s/light/%s/attributes", m_baseTopic.c_str(), addrStr.data());

    JsonDocument doc;
    doc["short_address"] = gear.internalAddress.shortAddr();
    doc["bus"] = gear.internalAddress.bus();
    if (gear.deviceType.has_value()) {
        doc["device_type"] = *gear.deviceType;
    }
    if (!gear.gtin.empty()) {
        doc["gtin"] = gear.gtin.c_str();
    }
    doc["dev_min_level"] = gear.minLevel;
    doc["dev_max_level"] = gear.maxLevel;
    doc["dev_power_on_level"] = gear.powerOnLevel;
    doc["dev_system_failure_level"] = gear.systemFailureLevel;

    char payload[384];
    serializeJson(doc, payload, sizeof(payload));
    m_mqtt.publish(topic, payload, 1, true);
}

void MqttBridge::publishDeviceGroups(const DaliLongAddress_t longAddr, const GroupMask& groups) const {
    const auto addrStr = utils::longAddressToString(longAddr);

    char topic[128];
    snprintf(topic, sizeof(topic), "%s/light/%s/groups", m_baseTopic.c_str(), addrStr.data());

    JsonDocument doc;
    auto grpArr = doc["groups"].to<JsonArray>();
    for (size_t i = 0; i < 16; ++i) {
        if (groups.test(i)) {
            grpArr.add(i);
        }
    }

    char payload[128];
    serializeJson(doc, payload, sizeof(payload));
    m_mqtt.publish(topic, payload, 1, true);
}

void MqttBridge::publishAllDeviceGroups() const {
    const auto assignments = m_daliRegistry.getGroupAssignments();
    for (const auto& [longAddr, mask] : assignments) {
        publishDeviceGroups(longAddr, mask);
        vTaskDelay(pdMS_TO_TICKS(2));
    }
}

} // namespace daliMQTT