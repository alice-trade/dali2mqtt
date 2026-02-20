// Copyright (c) 2026 Alice-Trade Inc.
// SPDX-License-Identifier: GPL-2.0-or-later

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

    void MQTTCommandHandler::publishLightState(DaliAddressType addr_type, const uint8_t target_id,
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
                auto grp = DaliGroupManagement::Instance().getGroupState(target_id);
                groupState.level = (grp.last_level > 0) ? grp.last_level : 254;
            } else if (state_str == "OFF") {
                groupState.level = 0;
            }
            DaliGroupManagement::Instance().updateGroupState(target_id, groupState);
        }

        switch (addr_type) {
            case DaliAddressType::Short: {
                if (const auto long_addr_opt = device_controller.getLongAddress(target_id)) {
                    update_device(*long_addr_opt);
                }
                break;
            }
            case DaliAddressType::Group: {
                const auto &group_manager = DaliGroupManagement::Instance();
                auto all_assignments = group_manager.getAllAssignments();
                for (const auto &[long_addr, groups]: all_assignments) {
                    if (groups.test(target_id)) {
                        update_device(long_addr);
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

    void MQTTCommandHandler::handleLightCommand(const std::vector<std::string_view> &parts, const std::string &data) {
        // topic format: light/{long_addr_hex}/set OR light/group/{id}/set
        if (parts.size() < 3 || parts[0] != "light" || parts.back() != "set") return;

        DaliAddressType addr_type = DaliAddressType::Short;
        uint8_t target_id = 0;

        if (parts[1] == "group") {
            if (parts.size() < 4) return;
            addr_type = DaliAddressType::Group;
            int parsed_id = -1;
            auto [ptr, ec] = std::from_chars(parts[2].data(), parts[2].data() + parts[2].size(), parsed_id);
            if (ec != std::errc() || parsed_id < 0 || parsed_id > 15) {
                ESP_LOGW(TAG, "Invalid group ID received");
                return;
            }
            target_id = static_cast<uint8_t>(parsed_id);
        } else if (parts[1] == "broadcast") {
            addr_type = DaliAddressType::Broadcast;
            target_id = 0;
        } else {
            addr_type = DaliAddressType::Short;
            const auto long_addr_opt = utils::stringToLongAddress(parts[1]);
            if (!long_addr_opt) return;
            const auto short_addr_opt = DaliDeviceController::Instance().getShortAddress(*long_addr_opt);
            if (!short_addr_opt) {
                ESP_LOGD(TAG, "Received command for unknown long address: %s", std::string(parts[1]).c_str());
                return;
            }
            target_id = *short_addr_opt;
        }

        JsonDocument doc;
        if (deserializeJson(doc, data)) return;

        auto &dali = DaliAdapter::Instance();
        DaliPublishState targetState;
        std::optional<bool> target_on_state;

        if (doc["state"].is<const char*>()) {
            const char* state_val = doc["state"];
            if (strcmp(state_val, "ON") == 0) target_on_state = true;
            else if (strcmp(state_val, "OFF") == 0) target_on_state = false;
        }

        if (doc["brightness"].is<int>()) {
            targetState.level = static_cast<uint8_t>(std::clamp(doc["brightness"].as<int>(), 0, 254));
        }

        if (doc["color_temp"].is<int>()) {
            targetState.color_temp = static_cast<uint16_t>(doc["color_temp"].as<int>());
        }

        if (doc["color"].is<JsonObject>()) {
            JsonObject color = doc["color"];
            if (color["r"].is<int>() && color["g"].is<int>() && color["b"].is<int>()) {
                targetState.rgb = DaliRGB{
                    static_cast<uint8_t>(color["r"].as<int>()),
                    static_cast<uint8_t>(color["g"].as<int>()),
                    static_cast<uint8_t>(color["b"].as<int>())
                };
            }
        }

        if (targetState.color_temp.has_value() || targetState.rgb.has_value()) {
            DaliPublishState stateUpdateForMode;

            if (targetState.color_temp.has_value()) {
                dali.setDT8ColorTemp(addr_type, target_id, *targetState.color_temp);
                stateUpdateForMode.active_mode = DaliColorMode::Tc;
            }

            if (targetState.rgb.has_value()) {
                dali.setDT8RGB(addr_type, target_id, targetState.rgb->r, targetState.rgb->g, targetState.rgb->b);
                stateUpdateForMode.active_mode = DaliColorMode::Rgb;
            }

            if (stateUpdateForMode.active_mode.has_value()) {
                auto& controller = DaliDeviceController::Instance();

                if (addr_type == DaliAddressType::Short) {
                    if (auto long_addr = controller.getLongAddress(target_id)) {
                        controller.updateDeviceState(*long_addr, stateUpdateForMode);
                    }
                }
                else if (addr_type == DaliAddressType::Group) {
                    auto all_assignments = DaliGroupManagement::Instance().getAllAssignments();
                    for (const auto& [long_addr, groups] : all_assignments) {
                        if (groups.test(target_id)) {
                            controller.updateDeviceState(long_addr, stateUpdateForMode);
                        }
                    }
                }
                else if (addr_type == DaliAddressType::Broadcast) {
                    for (auto devices = controller.getDevices(); const auto& dev : devices) {
                        if (getIdentity(dev).available) {
                            controller.updateDeviceState(getIdentity(dev).long_address, stateUpdateForMode);
                        }
                    }
                }
            }
        }

        if (target_on_state.has_value() && !(*target_on_state)) {
            // OFF
            ESP_LOGD(TAG, "MQTT Command: OFF for target %u (type %d)", target_id, addr_type);
            dali.sendCommand(addr_type, target_id, Commands::OpCode::Off);
            publishLightState(addr_type, target_id, "OFF", targetState);
        } else if (target_on_state.has_value() && *target_on_state) {
             if (targetState.level.has_value() && *targetState.level > 0) {
                // ON + Level
                ESP_LOGD(TAG, "MQTT Command: ON with brightness %d for target %u (type %d)", *targetState.level,
                         target_id, addr_type);
                 dali.sendDACP(addr_type, target_id, *targetState.level);
                 publishLightState(addr_type, target_id, "ON", targetState);
            } else {
                // ON (Restore)
                std::optional<uint8_t> restore_level;
                if (addr_type == DaliAddressType::Short) {
                    auto &controller = DaliDeviceController::Instance();
                    if (auto long_addr = controller.getLongAddress(target_id)) {
                        auto saved = controller.getLastLevel(*long_addr);
                        if (saved.has_value() && *saved > 0) {
                            restore_level = saved;
                        }
                    }
                }

                if (restore_level.has_value()) {
                    targetState.level = *restore_level;
                    ESP_LOGD(TAG, "MQTT Command: ON (Restore) -> restoring level %d for target %u", targetState.level.value(), target_id);
                    dali.sendDACP(addr_type, target_id, targetState.level.value());
                    publishLightState(addr_type, target_id, "ON", targetState);
                } else {
                    targetState.level = 254;
                    ESP_LOGD(TAG, "MQTT Command: ON (Default) -> RECALL_MAX_LEVEL for target %u (type %d)", target_id,
                             addr_type);
                    dali.sendCommand(addr_type, target_id, Commands::OpCode::RecallMaxLevel);
                    publishLightState(addr_type, target_id, "ON", targetState);
                }
            }
        } else if (targetState.level.has_value()) {
            // Level Direct Change
            if (targetState.level > 0) {
                ESP_LOGD(TAG, "MQTT Command: Set brightness to %d for target %u (type %d)", targetState.level.value(), target_id,
                         addr_type);
                dali.sendDACP(addr_type, target_id, targetState.level.value());
                publishLightState(addr_type, target_id, "ON", targetState);
            } else {
                targetState.level = 0;
                ESP_LOGD(TAG, "MQTT Command: Set brightness to 0 (OFF) for target %u (type %d)", target_id, addr_type);
                dali.sendCommand(addr_type, target_id, Commands::OpCode::Off);
                publishLightState(addr_type, target_id, "OFF", targetState);
            }
        } else if (targetState.color_temp.has_value() || targetState.rgb.has_value()) {
            publishLightState(addr_type, target_id, "ON", targetState);
        }
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
        std::string result_topic = utils::stringFormat("%s%s", config.mqtt_base_topic.c_str(), CONFIG_DALI2MQTT_MQTT_GROUP_RES_SUBTOPIC);
        std::string payload = utils::stringFormat(R"({"status":"success","device":"%s","group":%d,"action":"%s"})",
                                                  doc["long_address"].as<const char*>(), group, (assign ? "added" : "removed"));
        mqtt.publish(result_topic.c_str(), payload.c_str());
    }

    void MQTTCommandHandler::handleSceneCommand(const std::string &data) {
        JsonDocument doc;
        if (deserializeJson(doc, data) || !doc["scene"].is<int>()) {
            ESP_LOGE(TAG, "Invalid scene command JSON structure");
            return;
        }

        uint8_t scene_id = doc["scene"].as<int>();
        DaliSceneManagement::Instance().activateScene(scene_id);
    }

    void MQTTCommandHandler::processSendDALICommand(const std::string &data) {
        JsonDocument doc;
        if (deserializeJson(doc, data)) return;

        if (doc["addr"].is<int>() && doc["cmd"].is<int>()) {
            const auto addr_val = static_cast<uint32_t>(doc["addr"].as<int>());
            const auto cmd_val = static_cast<uint32_t>(doc["cmd"].as<int>());
            uint8_t bits = 16;

            if (doc["bits"].is<int>()) {
                bits = static_cast<uint8_t>(doc["bits"].as<int>());
            } else {
                if (addr_val > 0xFF) bits = 24;
            }

            const uint32_t raw_data = (addr_val << 8) | cmd_val;
            auto& dali = DaliAdapter::Instance();

            if (!doc["tag"].isNull()) {
                const auto result = dali.sendRawQuery(raw_data, bits);
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
                dali.sendRaw(raw_data, bits);
                if (doc["twice"].as<bool>()) {
                    vTaskDelay(pdMS_TO_TICKS(10));
                    dali.sendRaw(raw_data, bits);
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
            if (strcmp(doc["addr_type"].as<const char*>(), "broadcast") == 0) {
                is_broadcast = true;
            }
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
                    auto short_addr_opt = controller.getShortAddress(*long_addr_opt);
                    if (short_addr_opt) {
                        controller.requestDeviceSync(*short_addr_opt, delay_ms);
                    } else {
                        ESP_LOGD(TAG, "Sync requested for unknown device long address: %s", addr_str.c_str());
                    }
                } else {
                    ESP_LOGD(TAG, "Invalid address format in sync command: %s", addr_str.c_str());
                }
            } else {
                ESP_LOGD(TAG, "Sync command missing 'address' field for device sync");
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
                AppController::Instance().onConfigReloadRequest();
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
        snprintf(status_topic, sizeof(status_topic), "%s/config/bus/sync_status", config.mqtt_base_topic.c_str());

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
        snprintf(status_topic, sizeof(status_topic), "%s/config/bus/sync_status", config.mqtt_base_topic.c_str());

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
        snprintf(status_topic, sizeof(status_topic), "%s/config/input_device/sync_status", config.mqtt_base_topic.c_str());

        mqtt.publish(status_topic, R"({"status":"initializing"})", 0, false);

        DaliDeviceController::Instance().perform24BitDeviceInitialization();
        DaliGroupManagement::Instance().refreshAssignmentsFromBus();

        mqtt.publish(status_topic, R"({"status":"idle", "last_action":"init_complete"})", 0, false);
        ESP_LOGI(TAG, "MQTT-initiated DALI Input Device initialization finished.");
        g_mqtt_bus_busy = false;
        vTaskDelete(nullptr);
    }

    void MQTTCommandHandler::handleScanCommand() {
        if (g_mqtt_bus_busy.exchange(true)) {
            ESP_LOGW(TAG, "Bus operation already in progress. Ignoring scan request.");
            return;
        }
        if (xTaskCreate(backgroundScanTask, "mqtt_scan_task", 8192, nullptr, 4, nullptr) != pdPASS) {
            ESP_LOGE(TAG, "Failed to create scan task");
            g_mqtt_bus_busy = false;
        }
    }

    void MQTTCommandHandler::handleInitializeCommand() {
        if (g_mqtt_bus_busy.exchange(true)) {
            ESP_LOGW(TAG, "Bus operation already in progress. Ignoring init request.");
            return;
        }
        if (xTaskCreate(backgroundInitTask, "mqtt_init_task", 8192, nullptr, 4, nullptr) != pdPASS) {
            ESP_LOGE(TAG, "Failed to create init task");
            g_mqtt_bus_busy = false;
        }
    }

    void MQTTCommandHandler::handle(const std::string &topic, const std::string &data) {
        ESP_LOGD(TAG, "MQTT Rx: %s -> %s", topic.c_str(), data.c_str());

        const auto config = ConfigManager::Instance().getConfig();
        std::string_view topic_sv(topic);

        if (!topic_sv.starts_with(config.mqtt_base_topic)) return;
        topic_sv.remove_prefix(config.mqtt_base_topic.length());

        std::vector<std::string_view> parts;
        for (const auto part: std::views::split(topic_sv, '/')) {
            if (!part.empty()) parts.emplace_back(part.begin(), part.end());
        }

        if (parts.empty()) return;

        if (parts[0] == "light") {
            handleLightCommand(parts, data);
        } else if (parts[0] == "config") {
            if (parts.size() == 2) {
                if (parts[1] == "get") {
                    handleConfigGet();
                } else if (parts[1] == "set") {
                    handleConfigSet(data);
                }
            } else if (parts.size() > 2 && parts[1] == "group" && parts[2] == "set") {
                handleGroupCommand(data);
            }
            else if (parts.size() > 2 && parts[1] == "bus") {
                if (parts[2] == "scan") {
                    handleScanCommand();
                } else if (parts[2] == "initialize") {
                    handleInitializeCommand();
                }
            }
            else if (parts.size() > 2 && parts[1] == "input_device") {
                if (parts[2] == "scan") {
                    handleScanCommand();
                } else if (parts[2] == "initialize") {
                    if (g_mqtt_bus_busy.exchange(true)) {
                        ESP_LOGW(TAG, "Bus operation already in progress. Ignoring input init request.");
                        return;
                    }
                    if (xTaskCreate(backgroundInputInitTask, "mqtt_input_init", 4096, nullptr, 4, nullptr) != pdPASS) {
                        ESP_LOGE(TAG, "Failed to create input init task");
                        g_mqtt_bus_busy = false;
                    }
                }
            } else if (parts[0] == "config" && parts.size() > 1 && parts[1] == "discovery") {
                if (parts.size() > 2 && parts[2] == "publish") {
                    AppController::Instance().publishHAMqttDiscovery();
                }
            }
        } else if (parts[0] == "scene" && parts.size() > 1 && parts[1] == "set") {
            std::string scene_str = data;
            // HASS Scene Select
            if (scene_str.starts_with("Scene ")) {
                scene_str.erase(0, 6); // "Scene "
                int scene_id = std::stoi(scene_str);
                DaliSceneManagement::Instance().activateScene(scene_id);
            } else {
                handleSceneCommand(data);
            }
        } else if (parts[0] == "cmd" && parts.size() > 1 && parts[1] == "send") {
            processSendDALICommand(data);
        } else if (parts[0] == "cmd" && parts.size() > 1 && parts[1] == "sync") {
            handleSyncCommand(data);
        }

    }
} // namespace daliMQTT
