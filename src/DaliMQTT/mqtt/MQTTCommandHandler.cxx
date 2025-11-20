#include "MQTTCommandHandler.hxx"
#include "ConfigManager.hxx"
#include "MQTTClient.hxx"
#include "DaliDeviceController.hxx"
#include "DaliGroupManagement.hxx"
#include "DaliSceneManagement.hxx"

namespace daliMQTT {

    static constexpr char TAG[] = "MQTTCommandHandler";


     void MQTTCommandHandler::publishLightState(dali_addressType_t addr_type, uint8_t target_id, const std::string& state, std::optional<uint8_t> brightness) {
        auto& device_controller = DaliDeviceController::getInstance();

         auto update_device = [&](const DaliLongAddress_t long_addr) {
             uint8_t level_to_set = 0;

             if (state == "OFF") {
                 level_to_set = 0;
             } else {
                 if (brightness.has_value()) {
                     level_to_set = *brightness;
                 } else {
                     level_to_set = device_controller.getLastLevel(long_addr).value_or(254);
                 }
             }

             device_controller.updateDeviceState(long_addr, level_to_set);
         };

        switch (addr_type) {
            case DALI_ADDRESS_TYPE_SHORT: {
                if (const auto long_addr_opt = device_controller.getLongAddress(target_id)) {
                    update_device(*long_addr_opt);
                }
                break;
            }
            case DALI_ADDRESS_TYPE_GROUP: {
                const auto& group_manager = DaliGroupManagement::getInstance();
                auto all_assignments = group_manager.getAllAssignments();
                for (const auto& [long_addr, groups] : all_assignments) {
                    if (groups.test(target_id)) {
                        ESP_LOGD(TAG, "Group Update: Device %s belongs to group %d -> Level %d",
                                longAddressToString(long_addr).data(), target_id, brightness.value_or(0));
                        update_device(long_addr);
                    }
                }
                break;
            }
            case DALI_ADDRESS_TYPE_BROADCAST: {
                auto devices = device_controller.getDevices();
                for (const auto& [long_addr, device] : devices) {
                    if(device.is_present) {
                        update_device(long_addr);
                    }
                }
                break;
            }
            default:
                break;
        }
    }

    void MQTTCommandHandler::handleLightCommand(const std::vector<std::string_view>& parts, const std::string& data) {
       // topic format: light/{long_addr_hex}/set OR light/group/{id}/set
        if (parts.size() < 3 || parts[0] != "light" || parts.back() != "set") return;

        dali_addressType_t addr_type = DALI_ADDRESS_TYPE_SHORT;
        uint8_t target_id = 0;

        if (parts[1] == "group") {
            if (parts.size() < 4) return;
            addr_type = DALI_ADDRESS_TYPE_GROUP;
            int parsed_id = -1;
            auto [ptr, ec] = std::from_chars(parts[2].data(), parts[2].data() + parts[2].size(), parsed_id);
            if (ec != std::errc() || parsed_id < 0 || parsed_id > 15) {
                ESP_LOGW(TAG, "Invalid group ID received");
                return;
            }
            target_id = static_cast<uint8_t>(parsed_id);
        } else if (parts[1] == "broadcast") {
            addr_type = DALI_ADDRESS_TYPE_BROADCAST;
            target_id = 0;
        } else {
            addr_type = DALI_ADDRESS_TYPE_SHORT;
            const auto long_addr_opt = stringToLongAddress(parts[1]);
            if (!long_addr_opt) return;
            const auto short_addr_opt = DaliDeviceController::getInstance().getShortAddress(*long_addr_opt);
            if (!short_addr_opt) {
                ESP_LOGD(TAG, "Received command for unknown long address: %s", std::string(parts[1]).c_str());
                return;
            }
            target_id = *short_addr_opt;
        }

        cJSON* root = cJSON_Parse(data.c_str());
        if (!root) return;

        auto& dali = DaliAPI::getInstance();
        std::optional<bool> target_on_state;
        std::optional<uint8_t> target_brightness;

        cJSON* state_item = cJSON_GetObjectItem(root, "state");
        if (state_item && cJSON_IsString(state_item)) {
            if (strcmp(state_item->valuestring, "ON") == 0) {
                target_on_state = true;
            } else if (strcmp(state_item->valuestring, "OFF") == 0) {
                target_on_state = false;
            }
        }
        cJSON* brightness_item = cJSON_GetObjectItem(root, "brightness");
        if (brightness_item && cJSON_IsNumber(brightness_item)) {
            target_brightness = static_cast<uint8_t>(std::clamp(brightness_item->valueint, 0, 254));
        }


        if (target_on_state.has_value() && !(*target_on_state)) {  // OFF
            ESP_LOGD(TAG, "MQTT Command: OFF for target %u (type %d)", target_id, addr_type);
            dali.sendCommand(addr_type, target_id, DALI_COMMAND_OFF);
            publishLightState(addr_type, target_id, "OFF", 0);
        }
        else if (target_on_state.has_value() && *target_on_state) {
            if (target_brightness.has_value() && *target_brightness > 0) { // ON + Level
                ESP_LOGD(TAG, "MQTT Command: ON with brightness %d for target %u (type %d)", *target_brightness, target_id, addr_type);
                dali.sendDACP(addr_type, target_id, *target_brightness);
                publishLightState(addr_type, target_id, "ON", *target_brightness);
            } else { // ON (Restore)
                ESP_LOGD(TAG, "MQTT Command: ON (to max level) for target %u (type %d)", target_id, addr_type);
                dali.sendCommand(addr_type, target_id, DALI_COMMAND_RECALL_MAX_LEVEL);
                publishLightState(addr_type, target_id, "ON", std::nullopt);
            }
        }
        else if (target_brightness.has_value()) { // Level Direct Change
            uint8_t level = *target_brightness;
            if (level > 0) {
                ESP_LOGD(TAG, "MQTT Command: Set brightness to %d for target %u (type %d)", level, target_id, addr_type);
                dali.sendDACP(addr_type, target_id, level);
                publishLightState(addr_type, target_id, "ON", level);
            } else {
                ESP_LOGD(TAG, "MQTT Command: Set brightness to 0 (OFF) for target %u (type %d)", target_id, addr_type);
                dali.sendCommand(addr_type, target_id, DALI_COMMAND_OFF);
                publishLightState(addr_type, target_id, "OFF", 0);
            }
        }

        cJSON_Delete(root);
        }

    void MQTTCommandHandler::handleGroupCommand(const std::string& data) {
        cJSON* root = cJSON_Parse(data.c_str());
        if (!root) {
            ESP_LOGE(TAG, "Failed to parse group command JSON");
            return;
        }

        cJSON* addr_item = cJSON_GetObjectItem(root, "long_address");
        cJSON* group_item = cJSON_GetObjectItem(root, "group");
        cJSON* state_item = cJSON_GetObjectItem(root, "state");

        if (!cJSON_IsString(addr_item) || !cJSON_IsNumber(group_item) || !cJSON_IsString(state_item)) {
            ESP_LOGE(TAG, "Invalid group command JSON structure");
            cJSON_Delete(root);
            return;
        }

        auto long_addr_opt = stringToLongAddress(addr_item->valuestring);
        if (!long_addr_opt) return; // Invalid long address format

        uint8_t group = group_item->valueint;
        bool assign = (strcmp(state_item->valuestring, "add") == 0);

        DaliGroupManagement::getInstance().setGroupMembership(*long_addr_opt, group, assign);

        auto config = ConfigManager::getInstance().getConfig();
        auto const& mqtt = MQTTClient::getInstance();
        std::string result_topic = std::format("{}{}", config.mqtt_base_topic, CONFIG_DALI2MQTT_MQTT_GROUP_RES_SUBTOPIC);
        std::string payload = std::format(R"({{"status":"success","device":"{}","group":{},"action":"{}"}})", addr_item->valuestring, group, (assign ? "added" : "removed"));
        mqtt.publish(result_topic, payload);

        cJSON_Delete(root);
    }

    void MQTTCommandHandler::handleSceneCommand(const std::string& data) {
        cJSON* root = cJSON_Parse(data.c_str());
        if (!root) {
            ESP_LOGE(TAG, "Failed to parse scene command JSON");
            return;
        }

        cJSON* scene_item = cJSON_GetObjectItem(root, "scene");
        if (!cJSON_IsNumber(scene_item)) {
            ESP_LOGE(TAG, "Invalid scene command JSON structure, 'scene' field is missing or not a number");
            cJSON_Delete(root);
            return;
        }
        uint8_t scene_id = scene_item->valueint;
        DaliSceneManagement::getInstance().activateScene(scene_id);

        cJSON_Delete(root);
    }


    void MQTTCommandHandler::handle(const std::string& topic, const std::string& data) {
        ESP_LOGD(TAG, "MQTT Rx: %s -> %s", topic.c_str(), data.c_str());

        const auto config = ConfigManager::getInstance().getConfig();
        std::string_view topic_sv(topic);

        if (!topic_sv.starts_with(config.mqtt_base_topic)) return;
        topic_sv.remove_prefix(config.mqtt_base_topic.length());

        std::vector<std::string_view> parts;
        for (const auto part : std::views::split(topic_sv, '/')) {
             if (!part.empty()) parts.emplace_back(part.begin(), part.end());
        }

        if (parts.empty()) return;

        if (parts[0] == "light") {
            handleLightCommand(parts, data);
        } else if (parts[0] == "config" && parts.size() > 2 && parts[1] == "group" && parts[2] == "set") {
            handleGroupCommand(data);
        } else if (parts[0] == "scene" && parts.size() > 1 && parts[1] == "set") {
             std::string scene_str = data;
             // HASS Scene Select
             if (scene_str.starts_with("Scene ")) {
                 scene_str.erase(0, 6); // "Scene "
                 int scene_id = std::stoi(scene_str);
                 DaliSceneManagement::getInstance().activateScene(scene_id);
             } else {
                 handleSceneCommand(data);
             }
        }
    }

} // namespace daliMQTT