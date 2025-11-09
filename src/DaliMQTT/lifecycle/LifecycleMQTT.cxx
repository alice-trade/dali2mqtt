#include "sdkconfig.h"
#include "ConfigManager.hxx"
#include "MQTTClient.hxx"
#include "DaliAPI.hxx"
#include "DaliDeviceController.hxx"
#include "DaliGroupManagement.hxx"
#include "DaliSceneManagement.hxx"
#include "WebUI.hxx"
#include "MQTTDiscovery.hxx"
#include "Lifecycle.hxx"

namespace daliMQTT {

    static constexpr char  TAG[] = "LifecycleMQTT";

    void Lifecycle::setupAndRunMqtt() {
        const auto config = ConfigManager::getInstance().getConfig();
        auto& mqtt = MQTTClient::getInstance();

        const std::string availability_topic = std::format("{}{}", config.mqtt_base_topic, CONFIG_DALI2MQTT_MQTT_AVAILABILITY_TOPIC);
        mqtt.init(config.mqtt_uri, config.mqtt_client_id, availability_topic, config.mqtt_user, config.mqtt_pass);
        mqtt.onConnected = [this]() { this->onMqttConnected(); };
        mqtt.onData = [this](const std::string& t, const std::string& d) { this->onMqttData(t, d); };

        mqtt.connect();
    }

    void Lifecycle::onMqttConnected() {
        ESP_LOGI(TAG, "MQTT connected successfully.");
        auto config = ConfigManager::getInstance().getConfig();
        auto const& mqtt = MQTTClient::getInstance();

        std::string availability_topic = std::format("{}{}", config.mqtt_base_topic, CONFIG_DALI2MQTT_MQTT_AVAILABILITY_TOPIC);
        mqtt.publish(availability_topic, CONFIG_DALI2MQTT_MQTT_PAYLOAD_ONLINE, 1, true);

        // Подписка на команды для светильников
        std::string light_cmd_topic = std::format("{}{}", config.mqtt_base_topic, "/light/+/+/set");
        mqtt.subscribe(light_cmd_topic);
        ESP_LOGI(TAG, "Subscribed to: %s", light_cmd_topic.c_str());

        // Подписка на команды управления группами
        std::string group_cmd_topic = std::format("{}{}", config.mqtt_base_topic, CONFIG_DALI2MQTT_MQTT_GROUP_SET_SUBTOPIC);
        mqtt.subscribe(group_cmd_topic);
        ESP_LOGI(TAG, "Subscribed to: %s", group_cmd_topic.c_str());

        // Подписка на команды управления сценами
        std::string scene_cmd_topic = std::format("{}{}", config.mqtt_base_topic, CONFIG_DALI2MQTT_MQTT_SCENE_CMD_SUBTOPIC);
        mqtt.subscribe(scene_cmd_topic);
        ESP_LOGI(TAG, "Subscribed to: %s", scene_cmd_topic.c_str());

        MQTTDiscovery mqtt_discovery;
        mqtt_discovery.publishAllDevices();
        ESP_LOGI(TAG, "MQTT discovery messages published.");

        DaliDeviceController::getInstance().start();
    }

    // Вспомогательная функция для обработки команд управления светом
    static void handleLightCommand(const std::vector<std::string_view>& parts, const std::string& data) {
        if (parts.size() != 4 || parts[0] != "light" || parts[3] != "set") return;

        dali_addressType_t addr_type;
        if (parts[1] == "short") addr_type = DALI_ADDRESS_TYPE_SHORT;
        else if (parts[1] == "group") addr_type = DALI_ADDRESS_TYPE_GROUP;
        else return;

        uint8_t id;
        auto [ptr, ec] = std::from_chars(parts[2].data(), parts[2].data() + parts[2].size(), id);
        if (ec != std::errc()) return;

        cJSON* root = cJSON_Parse(data.c_str());
        if (!root) return;

        auto& dali = DaliAPI::getInstance();

        if (cJSON const* state = cJSON_GetObjectItem(root, "state"); state && cJSON_IsString(state)) {
            if (strcmp(state->valuestring, "OFF") == 0) {
                dali.sendCommand(addr_type, id, DALI_OFF);
            }
        }

        if (cJSON* brightness = cJSON_GetObjectItem(root, "brightness"); brightness && cJSON_IsNumber(brightness)) {
             uint8_t level = static_cast<uint8_t>(std::clamp(brightness->valueint, 0, 254));
             dali.sendDACP(addr_type, id, level);
        }

        cJSON_Delete(root);
    }

    // Вспомогательная функция для обработки команд управления группами
    static void handleGroupCommand(const std::string& data) {
        cJSON* root = cJSON_Parse(data.c_str());
        if (!root) {
            ESP_LOGE(TAG, "Failed to parse group command JSON");
            return;
        }

        cJSON* addr_item = cJSON_GetObjectItem(root, "short_address");
        cJSON* group_item = cJSON_GetObjectItem(root, "group");
        cJSON* state_item = cJSON_GetObjectItem(root, "state");

        if (!cJSON_IsNumber(addr_item) || !cJSON_IsNumber(group_item) || !cJSON_IsString(state_item)) {
            ESP_LOGE(TAG, "Invalid group command JSON structure");
            cJSON_Delete(root);
            return;
        }

        uint8_t addr = addr_item->valueint;
        uint8_t group = group_item->valueint;
        bool assign = (strcmp(state_item->valuestring, "add") == 0);

        DaliGroupManagement::getInstance().setGroupMembership(addr, group, assign);

        auto config = ConfigManager::getInstance().getConfig();
        auto const& mqtt = MQTTClient::getInstance();
        std::string result_topic = std::format("{}{}", config.mqtt_base_topic, CONFIG_DALI2MQTT_MQTT_GROUP_RES_SUBTOPIC);
        std::string payload = std::format(R"({{"status":"success","device":{},"group":{},"action":"{}"}})", addr, group, (assign ? "added" : "removed"));
        mqtt.publish(result_topic, payload);

        cJSON_Delete(root);
    }

    // Вспомогательная функция для обработки команд сцен
    static void handleSceneCommand(const std::string& data) {
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

    void Lifecycle::onMqttData(const std::string& topic, const std::string& data) {
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
            ESP_LOGD("MQTTDEBUG", "MQTT Light: %s", data.c_str());
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
}