#include "sdkconfig.h"
#include "ConfigManager.hxx"
#include "MQTTClient.hxx"
#include "DaliDeviceController.hxx"
#include "WebUI.hxx"
#include "MQTTDiscovery.hxx"
#include "Lifecycle.hxx"
#include "MQTTCommandHandler.hxx"

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
        std::string light_cmd_topic = std::format("{}{}", config.mqtt_base_topic, "/light/+/set");
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

    void Lifecycle::onMqttData(const std::string& topic, const std::string& data) {
        MQTTCommandHandler::getInstance().handle(topic, data);
    }
}