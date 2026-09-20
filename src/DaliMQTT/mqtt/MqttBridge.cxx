// Copyright (c) 2026 Alice-Trade Inc.
// SPDX-License-Identifier: GPL-3.0-or-later

#include "mqtt/MqttBridge.hxx"
#include "utils/DaliLongAddrConversions.hxx"
#include "utils/DaliSensorMath.hxx"
#include <ArduinoJson.h>
#include <esp_log.h>
#include <charconv>

namespace daliMQTT {

static constexpr char TAG[] = "MqttBridge";

MqttBridge::MqttBridge(MqttClient& mqtt, DaliDeviceRegistry& daliRegistry, DaliBusEngine& daliBus, ConfigStore& config,
                       OtaService& ota,  const NetworkPlatform& network)
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

    m_ota.setProgressCallback(&onOtaProgressChanged, this);
    m_ota.setVersionCallback(&onOtaVersionReceived, this);
    m_mqtt.setConnectedCallback(&onMqttConnectedBridge, this);
    m_mqtt.setDataCallback(&onMqttDataReceivedBridge, this);

    if (!m_taskHandle) {
        const BaseType_t res = xTaskCreate(bridgeTaskRunner, "mqtt_bridge_task", 4096, this, 6, &m_taskHandle);
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
            DeviceStateChangeEvent ev{
                .longAddress = gear->longAddress,
                .internalAddress = gear->internalAddress,
                .level = gear->currentLevel,
                .statusByte = gear->statusByte,
                .available = gear->available
            };
            if (gear->color.has_value()) {
                ev.colorTemp = gear->color->currentTc;
                ev.rgb = gear->color->currentRgb;
            }

            onDaliDeviceStateChanged(ev, const_cast<MqttBridge*>(this));
            vTaskDelay(pdMS_TO_TICKS(2));
        }
    }

    for (uint8_t busId = 0; busId < DaliDeviceRegistry::BUS_COUNT; ++busId) {
        for (uint8_t g = 0; g < 16; ++g) {
            DaliGroupState gState = m_daliRegistry.getGroupState(busId, g);
            GroupStateChangeEvent ev{
                .busId = busId,
                .groupId = g,
                .level = gState.currentLevel,
                .colorTemp = gState.colorTemp,
                .rgb = gState.rgb
            };
            onDaliGroupStateChanged(ev, const_cast<MqttBridge*>(this));

            vTaskDelay(pdMS_TO_TICKS(2));
        }
    }

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
    MqttIncomingMessage msg{};

    while (true) {
        if (xQueueReceive(m_cmdQueue, &msg, portMAX_DELAY) == pdTRUE) {
            const std::string_view topic(msg.topic.c_str(), msg.topic.length());
            const std::string_view payload(msg.payload.c_str(), msg.payload.length());

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
        char resTopic[128];
        snprintf(resTopic, sizeof(resTopic), "%s/config/get/response", m_baseTopic.c_str());
        JsonDocument doc;
        doc["mqtt_base"] = cfg->mqttBaseTopic.c_str();
        doc["client_id"] = cfg->clientId.c_str();
        doc["dali_poll"] = cfg->daliPollIntervalMs;
        doc["hass_disc"] = cfg->hassDiscoveryEnabled;
        char resBuf[256];
        serializeJson(doc, resBuf, sizeof(resBuf));
        m_mqtt.publish(resTopic, resBuf, 0, false);
    } else if (subTopic == "/config/group/set") {
        handleGroupConfigCommand(payload);
    } else if (subTopic == "/config/bus/scan") {
        ESP_LOGI(TAG, "MQTT Bus Scan initiated");
        m_daliRegistry.scanBus();
    } else if (subTopic == "/config/bus/initialize") {
        ESP_LOGI(TAG, "MQTT Bus Commissioning initiated");
        m_daliRegistry.commissionNewDevices();
    } else if (subTopic == "/config/input_device/initialize") {
        ESP_LOGI(TAG, "MQTT Input Device Commissioning initiated");
        m_daliRegistry.commission24BitDevices();
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
        m_ota.startUpdate(nullptr, true);
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

    m_daliRegistry.setDeviceGroupMembership(*longAddrOpt, group, assigned);
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

} // namespace daliMQTT