// Copyright (c) 2026 Alice-Trade Inc.
// SPDX-License-Identifier: GPL-3.0-or-later

#include "mqtt/MQTTCommandHandler.hxx"
#include <system/AppController.hxx>
#include <utils/StringUtils.hxx>
#include "system/ConfigManager.hxx"
#include "mqtt/MQTTClient.hxx"
#include "dali/DaliDeviceController.hxx"
#include "dali/DaliGroupManagement.hxx"
#include "dali/DaliSceneManagement.hxx"
#include "utils/DaliLongAddrConversions.hxx"

namespace daliMQTT {

    static constexpr char TAG[] = "MQTTCommandHandler";
    static std::atomic<bool> g_mqtt_bus_busy{false};

    namespace {
        struct LightTarget {
            DaliAddressType type{DaliAddressType::Short};
            uint8_t bus{0};
            uint8_t id{0};
            bool valid{false};
        };

        struct LightPayload {
            std::optional<bool> power_on;
            DaliPublishState state;
        };

        LightTarget parseLightTopic(const etl::vector<std::string_view, 16>& parts) {
            LightTarget target;
            // format 1: light/bus/B/group/G/set
            if (parts.size() >= 5 && parts[1] == "bus" && parts[3] == "group") {
                target.type = DaliAddressType::Group;
                target.bus = std::stoi(std::string(parts[2]));
                target.id = std::stoi(std::string(parts[4]));
                target.valid = true;
            }
            // format 2: light/broadcast/set
            else if (parts.size() >= 3 && parts[1] == "broadcast") {
                target.type = DaliAddressType::Broadcast;
                target.valid = true;
            }
            // format 3: light/LONG_ADDR_HEX/set
            else if (parts.size() >= 3) {
                target.type = DaliAddressType::Short;
                auto long_addr_opt = utils::stringToLongAddress(parts[1]);
                if (long_addr_opt) {
                    auto int_addr_opt = DaliDeviceController::Instance().getInternalAddress(*long_addr_opt);
                    if (int_addr_opt) {
                        target.bus = int_addr_opt->bus();
                        target.id = int_addr_opt->shortAddr();
                        target.valid = true;
                    } else {
                        ESP_LOGD(TAG, "Received command for unknown long address: %s", std::string(parts[1]).c_str());
                    }
                }
            }
            return target;
        }

        LightPayload parseLightPayload(const std::string& data) {
            LightPayload payload;
            JsonDocument doc;
            if (deserializeJson(doc, data)) return payload;

            if (doc["state"].is<const char*>()) {
                std::string_view state_str = doc["state"].as<const char*>();
                if (state_str == "ON") payload.power_on = true;
                else if (state_str == "OFF") payload.power_on = false;
            }

            if (doc["brightness"].is<int>()) {
                payload.state.level = static_cast<uint8_t>(std::clamp(doc["brightness"].as<int>(), 0, 254));
            }

            if (doc["color_temp"].is<int>()) {
                payload.state.color_temp = static_cast<uint16_t>(doc["color_temp"].as<int>());
            }

            if (doc["color"].is<JsonObject>()) {
                JsonObject color = doc["color"];
                if (color["r"].is<int>() && color["g"].is<int>() && color["b"].is<int>()) {
                    payload.state.rgb = DaliRGB{
                        static_cast<uint8_t>(color["r"].as<int>()),
                        static_cast<uint8_t>(color["g"].as<int>()),
                        static_cast<uint8_t>(color["b"].as<int>())
                    };
                }
            }
            return payload;
        }

        void applyColorSettings(DaliAdapter* adapter, DaliAddressType addr_type, uint8_t target_id, const DaliPublishState& state) {
            if (state.color_temp.has_value()) {
                adapter->setDT8ColorTemp(addr_type, target_id, *state.color_temp);
            }
            if (state.rgb.has_value()) {
                adapter->setDT8RGB(addr_type, target_id, state.rgb->r, state.rgb->g, state.rgb->b);
            }
        }

        void applyPowerAndLevel(DaliAdapter* adapter, DaliAddressType addr_type, uint8_t target_id, const LightPayload& payload) {
            if (payload.power_on.has_value() && !(*payload.power_on)) {
                adapter->sendCommand(addr_type, target_id, Commands::OpCode::Off);
                return;
            }

            if (payload.state.level.has_value()) {
                if (*payload.state.level > 0) {
                    adapter->sendDACP(addr_type, target_id, *payload.state.level);
                } else {
                    adapter->sendCommand(addr_type, target_id, Commands::OpCode::Off);
                }
                return;
            }

            if (payload.power_on.has_value() && *payload.power_on) {
                std::optional<uint8_t> restore_level;
                if (addr_type == DaliAddressType::Short) {
                    auto& controller = DaliDeviceController::Instance();
                    if (auto long_addr = controller.getLongAddress(DaliInternalAddr(adapter->getBusId(), target_id))) {
                        auto saved = controller.getLastLevel(*long_addr);
                        if (saved.has_value() && *saved > 0) restore_level = saved;
                    }
                }

                if (restore_level.has_value()) {
                    adapter->sendDACP(addr_type, target_id, *restore_level);
                } else {
                    adapter->sendCommand(addr_type, target_id, Commands::OpCode::RecallMaxLevel);
                }
            }
        }

        void executeLightCommand(uint8_t bus, DaliAddressType addr_type, uint8_t target_id, const LightPayload& payload) {
            auto* adapter = DaliDeviceController::Instance().getAdapter(bus);
            if (!adapter || !adapter->isInitialized()) return;

            applyColorSettings(adapter, addr_type, target_id, payload.state);
            applyPowerAndLevel(adapter, addr_type, target_id, payload);
        }

    }

    void MQTTCommandHandler::publishLightState(DaliAddressType addr_type, uint8_t target_bus, const uint8_t target_id,
                                               const std::string &state_str, const DaliPublishState& state_data) {
        auto &device_controller = DaliDeviceController::Instance();

        auto update_device = [&](const DaliLongAddress_t long_addr) {
            DaliPublishState devState = state_data;

            if (state_str == "OFF") {
                devState.level = 0;
            } else {
                if (!devState.level.has_value()) {
                    devState.level = device_controller.getLastLevel(long_addr).value_or(254);
                }
            }
            device_controller.updateDeviceState(long_addr, devState);
        };

        if (addr_type == DaliAddressType::Group) {
            DaliPublishState groupState = state_data;
            if (state_str == "ON" && !groupState.level.has_value()) {
                auto grp = DaliGroupManagement::Instance().getGroupState(target_bus, target_id);
                groupState.level = (grp.last_level > 0) ? grp.last_level : 254;
            } else if (state_str == "OFF") {
                groupState.level = 0;
            }
            DaliGroupManagement::Instance().updateGroupState(target_bus, target_id, groupState);
        }

        switch (addr_type) {
            case DaliAddressType::Short: {
                if (const auto long_addr_opt = device_controller.getLongAddress(DaliInternalAddr(target_bus, target_id))) {
                    update_device(*long_addr_opt);
                }
                break;
            }
            case DaliAddressType::Group: {
                const auto &group_manager = DaliGroupManagement::Instance();
                auto all_assignments = group_manager.getAllAssignments();
                for (const auto &[long_addr, groups]: all_assignments) {
                    if (groups.test(target_id)) {
                        auto int_addr = device_controller.getInternalAddress(long_addr);
                        if (int_addr && int_addr->bus() == target_bus) {
                            update_device(long_addr);
                        }
                    }
                }
                break;
            }
            case DaliAddressType::Broadcast: {
                for (auto devices = device_controller.getDevices(); const auto& device: devices) {
                    if (const auto& id = getIdentity(device); id.available) {
                        update_device(id.long_address);
                    }
                }
                break;
            }
            default:
                break;
        }
    }

    void MQTTCommandHandler::handleLightCommand(const etl::vector<std::string_view, 16> &parts, const std::string &data) {
        if (parts[0] != "light" || parts.back() != "set") return;

        LightTarget target = parseLightTopic(parts);
        if (!target.valid) return;

        LightPayload payload = parseLightPayload(data);

        if (target.type == DaliAddressType::Broadcast) {
            for (uint8_t i = 0; i < FirmwareConfig::BusLimit; ++i) {
                executeLightCommand(i, target.type, 0, payload);
            }
        } else {
            executeLightCommand(target.bus, target.type, target.id, payload);
        }

        if (payload.state.color_temp.has_value()) payload.state.active_mode = DaliColorMode::Tc;
        if (payload.state.rgb.has_value()) payload.state.active_mode = DaliColorMode::Rgb;

        std::string final_state_str = "ON";
        if (payload.power_on.has_value() && !(*payload.power_on)) {
            final_state_str = "OFF";
        } else if (payload.state.level.has_value() && payload.state.level.value() == 0) {
            final_state_str = "OFF";
        }

        publishLightState(target.type, target.bus, target.id, final_state_str, payload.state);
    }

    void MQTTCommandHandler::handleGroupCommand(const std::string &data) {
        JsonDocument doc;
        if (deserializeJson(doc, data) || doc["long_address"].isNull() || doc["group"].isNull() || doc["state"].isNull()) {
            ESP_LOGE(TAG, "Invalid group command JSON structure");
            return;
        }

        auto long_addr_opt = utils::stringToLongAddress(doc["long_address"].as<const char*>());
        if (!long_addr_opt) return;

        const uint8_t group = doc["group"].as<int>();
        bool assign = (strcmp(doc["state"].as<const char*>(), "add") == 0);

        DaliGroupManagement::Instance().setGroupMembership(*long_addr_opt, group, assign);

        const auto config = ConfigManager::Instance().getConfig();
        auto const &mqtt = MQTTClient::Instance();
        std::string result_topic = utils::stringFormat("%s%s", config->mqtt_base_topic.c_str(), CONFIG_DALI2MQTT_MQTT_GROUP_RES_SUBTOPIC);
        std::string payload = utils::stringFormat(R"({"status":"success","device":"%s","group":%d,"action":"%s"})",
                                                  doc["long_address"].as<const char*>(), group, (assign ? "added" : "removed"));
        mqtt.publish(result_topic.c_str(), payload.c_str());
    }

    void MQTTCommandHandler::handleSceneCommand(uint8_t bus_id, const std::string &data) {
        std::string scene_str = data;
        if (scene_str.starts_with("Scene ")) {
            scene_str.erase(0, 6);
            int scene_id = std::stoi(scene_str);
            DaliSceneManagement::activateScene(bus_id, scene_id);
            return;
        }

        JsonDocument doc;
        if (deserializeJson(doc, data) || !doc["scene"].is<int>()) {
            ESP_LOGE(TAG, "Invalid scene command JSON structure");
            return;
        }

        uint8_t scene_id = doc["scene"].as<int>();
        DaliSceneManagement::activateScene(bus_id, scene_id);
    }

    void MQTTCommandHandler::processSendDALICommand(const std::string &data) {
        JsonDocument doc;
        if (deserializeJson(doc, data)) return;

        if (doc["addr"].is<int>() && doc["cmd"].is<int>()) {
            uint8_t target_bus = 0;
            if (doc["bus"].is<int>()) target_bus = doc["bus"].as<int>();

            const auto addr_val = static_cast<uint32_t>(doc["addr"].as<int>());
            const auto cmd_val = static_cast<uint32_t>(doc["cmd"].as<int>());
            uint8_t bits = 16;

            if (doc["bits"].is<int>()) bits = static_cast<uint8_t>(doc["bits"].as<int>());
            else if (addr_val > 0xFF) bits = 24;

            const uint32_t raw_data = (addr_val << 8) | cmd_val;
            auto* adapter = DaliDeviceController::Instance().getAdapter(target_bus);
            if (!adapter || !adapter->isInitialized()) return;

            if (!doc["tag"].isNull()) {
                const auto result = adapter->sendRawQuery(raw_data, bits);
                JsonDocument resp;

                resp["tag"] = doc["tag"];
                if (result) { resp["status"] = "ok"; resp["response"] = *result; }
                else { resp["status"] = "no_reply"; }

                std::string payload;
                serializeJson(resp, payload);

                char res_topic[128];
                snprintf(res_topic, sizeof(res_topic), "%s/cmd/res", ConfigManager::Instance().getMqttBaseTopic().c_str());
                MQTTClient::Instance().publish(res_topic, payload.c_str());
            } else {
                adapter->sendRaw(raw_data, bits);
                if (doc["twice"].as<bool>()) {
                    vTaskDelay(pdMS_TO_TICKS(10));
                    adapter->sendRaw(raw_data, bits);
                }
            }
        }
    }

    void MQTTCommandHandler::handleSyncCommand(const std::string& data) {
        JsonDocument doc;
        if (deserializeJson(doc, data)) {
            ESP_LOGE(TAG, "Failed to parse sync command JSON");
            return;
        }

        uint32_t delay_ms = doc["delay_ms"].as<uint32_t>();
        bool is_broadcast = false;

        if (doc["addr_type"].is<const char*>()) {
            if (strcmp(doc["addr_type"].as<const char*>(), "broadcast") == 0) is_broadcast = true;
        }

        auto& controller = DaliDeviceController::Instance();

        if (is_broadcast) {
            uint32_t stagger = !doc["stagger_ms"].isNull() ? doc["stagger_ms"].as<uint32_t>() : 100;
            controller.requestBroadcastSync(delay_ms, stagger);
        } else {
            if (doc["address"].is<const char*>()) {
                std::string addr_str = doc["address"].as<const char*>();
                auto long_addr_opt = utils::stringToLongAddress(addr_str);

                if (long_addr_opt) {
                    auto internal_addr_opt = controller.getInternalAddress(*long_addr_opt);
                    if (internal_addr_opt) {
                        controller.requestDeviceSync(*internal_addr_opt, delay_ms);
                    } else {
                        ESP_LOGD(TAG, "Sync requested for unknown device long address: %s", addr_str.c_str());
                    }
                } else {
                    ESP_LOGD(TAG, "Invalid address format in sync command: %s", addr_str.c_str());
                }
            }
        }
    }

    void MQTTCommandHandler::handleConfigGet() {
        std::string json_string = ConfigManager::Instance().getSerializedConfig(true);
        char reply_topic[128];
        snprintf(reply_topic, sizeof(reply_topic), "%s/config", ConfigManager::Instance().getMqttBaseTopic().c_str());
        MQTTClient::Instance().publish(reply_topic, json_string.c_str(), 0, false);
    }

    void MQTTCommandHandler::handleConfigSet(const std::string& data) {
        ConfigUpdateResult result = ConfigManager::Instance().updateConfigFromJson(data.c_str());
        auto const &mqtt = MQTTClient::Instance();
        std::string status_topic = ConfigManager::Instance().getMqttBaseTopic() + "/config/status";

        switch (result) {
        case ConfigUpdateResult::MQTTUpdate:
            ESP_LOGI(TAG, "MQTT Config changed. Reloading...");
            mqtt.publish(status_topic.c_str(), R"({"status":"updated", "action":"reconnecting_mqtt"})", 0, false);
            xTaskCreate([](void*){
                vTaskDelay(pdMS_TO_TICKS(500));
                AppController::onConfigReloadRequest();
                vTaskDelete(nullptr);
            }, "mqtt_reload", 4096, nullptr, 5, nullptr);
            break;
        case ConfigUpdateResult::SystemUpdate:
        case ConfigUpdateResult::WIFIUpdate:
            ESP_LOGI(TAG, "System/WiFi Config changed. Rebooting...");
            mqtt.publish(status_topic.c_str(), R"({"status":"updated", "action":"rebooting"})", 0, false);
            vTaskDelay(pdMS_TO_TICKS(1000));
            esp_restart();
            break;
        case ConfigUpdateResult::NoUpdate:
        default:
            ESP_LOGI(TAG, "Config update received but no significant changes.");
            break;
        }
    }

    void MQTTCommandHandler::backgroundScanTask(void* arg) {
        auto config = ConfigManager::Instance().getConfig();
        char status_topic[128];
        snprintf(status_topic, sizeof(status_topic), "%s/config/bus/sync_status", config->mqtt_base_topic.c_str());

        MQTTClient::Instance().publish(status_topic, R"({"status":"scanning"})", 0, false);
        DaliDeviceController::Instance().performScan();

        DaliGroupManagement::Instance().refreshAssignmentsFromBus();
        MQTTClient::Instance().publish(status_topic, R"({"status":"idle", "last_action":"scan_complete"})", 0, false);
        ESP_LOGI(TAG, "MQTT-initiated DALI scan finished.");

        g_mqtt_bus_busy = false;
        vTaskDelete(nullptr);
    }

    void MQTTCommandHandler::backgroundInitTask(void* arg) {
        ESP_LOGI(TAG, "Starting MQTT-initiated DALI initialization...");
        auto const& mqtt = MQTTClient::Instance();
        auto config = ConfigManager::Instance().getConfig();
        char status_topic[128];
        snprintf(status_topic, sizeof(status_topic), "%s/config/bus/sync_status", config->mqtt_base_topic.c_str());

        mqtt.publish(status_topic, R"({"status":"initializing"})", 0, false);

        DaliDeviceController::Instance().performFullInitialization();
        DaliGroupManagement::Instance().refreshAssignmentsFromBus();

        mqtt.publish(status_topic, R"({"status":"idle", "last_action":"init_complete"})", 0, false);
        ESP_LOGI(TAG, "MQTT-initiated DALI initialization finished.");
        g_mqtt_bus_busy = false;
        vTaskDelete(nullptr);
    }

    void MQTTCommandHandler::backgroundInputInitTask(void* arg) {
        ESP_LOGI(TAG, "Starting MQTT-initiated DALI Input Device initialization...");
        auto const& mqtt = MQTTClient::Instance();
        auto config = ConfigManager::Instance().getConfig();
        char status_topic[128];
        snprintf(status_topic, sizeof(status_topic), "%s/config/input_device/sync_status", config->mqtt_base_topic.c_str());

        mqtt.publish(status_topic, R"({"status":"initializing"})", 0, false);

        DaliDeviceController::Instance().perform24BitDeviceInitialization();
        DaliGroupManagement::Instance().refreshAssignmentsFromBus();

        mqtt.publish(status_topic, R"({"status":"idle", "last_action":"init_complete"})", 0, false);
        ESP_LOGI(TAG, "MQTT-initiated DALI Input Device initialization finished.");
        g_mqtt_bus_busy = false;
        vTaskDelete(nullptr);
    }

    void MQTTCommandHandler::handleScanCommand() {
        if (g_mqtt_bus_busy.exchange(true)) return;
        if (xTaskCreate(backgroundScanTask, "mqtt_scan_task", 8192, nullptr, 4, nullptr) != pdPASS) g_mqtt_bus_busy = false;
    }

    void MQTTCommandHandler::handleInitializeCommand() {
        if (g_mqtt_bus_busy.exchange(true)) return;
        if (xTaskCreate(backgroundInitTask, "mqtt_init_task", 8192, nullptr, 4, nullptr) != pdPASS) g_mqtt_bus_busy = false;
    }


    void MQTTCommandHandler::handle(const std::string &topic, const std::string &data) {
        ESP_LOGD(TAG, "MQTT Rx: %s -> %s", topic.c_str(), data.c_str());

        const auto config = ConfigManager::Instance().getConfig();
        std::string_view topic_sv(topic);

        if (!topic_sv.starts_with(config->mqtt_base_topic)) return;
        topic_sv.remove_prefix(config->mqtt_base_topic.length());

        etl::vector<std::string_view, 16> parts;
        for (const auto part: std::views::split(topic_sv, '/')) {
            if (!part.empty() && !parts.full()) parts.emplace_back(part.begin(), part.end());
        }

        if (parts.empty()) return;

        if (parts[0] == "light") {
            handleLightCommand(parts, data);
        } else if (parts[0] == "config") {
            if (parts.size() == 2) {
                if (parts[1] == "get") handleConfigGet();
                else if (parts[1] == "set") handleConfigSet(data);
            } else if (parts.size() > 2 && parts[1] == "group" && parts[2] == "set") {
                handleGroupCommand(data);
            } else if (parts.size() > 2 && parts[1] == "bus") {
                if (parts[2] == "scan") handleScanCommand();
                else if (parts[2] == "initialize") handleInitializeCommand();
            } else if (parts.size() > 2 && parts[1] == "input_device") {
                if (parts[2] == "scan") handleScanCommand();
                else if (parts[2] == "initialize") {
                    if (g_mqtt_bus_busy.exchange(true)) return;
                    if (xTaskCreate(backgroundInputInitTask, "mqtt_input_init", 4096, nullptr, 4, nullptr) != pdPASS) g_mqtt_bus_busy = false;
                }
            } else if (parts.size() > 2 && parts[1] == "discovery" && parts[2] == "publish") {
                AppController::Instance().publishHAMqttDiscovery();
            }
        } else if (parts[0] == "scene" && parts.size() >= 4 && parts[1] == "bus" && parts[3] == "set") {
            // scene/bus/{bus_id}/set
            uint8_t bus_id = std::stoi(std::string(parts[2]));
            handleSceneCommand(bus_id, data);
        } else if (parts[0] == "cmd" && parts.size() > 1 && parts[1] == "send") {
            processSendDALICommand(data);
        } else if (parts[0] == "cmd" && parts.size() > 1 && parts[1] == "sync") {
            handleSyncCommand(data);
        }
    }
} // namespace daliMQTT
