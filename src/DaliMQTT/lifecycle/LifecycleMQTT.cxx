#include "sdkconfig.h"
#include "ConfigManager.hxx"
#include "MQTTClient.hxx"
#include "DaliDeviceController.hxx"
#include "MQTTDiscovery.hxx"
#include "Lifecycle.hxx"
#include "MQTTCommandHandler.hxx"

namespace daliMQTT {

    static constexpr char  TAG[] = "LifecycleMQTT";

    void Lifecycle::setupAndRunMqtt() {
        const auto config = ConfigManager::getInstance().getConfig();
        auto& mqtt = MQTTClient::getInstance();

        const std::string availability_topic = std::format("{}{}", config.mqtt_base_topic, CONFIG_DALI2MQTT_MQTT_AVAILABILITY_TOPIC);
        mqtt.init(config.mqtt_uri, config.client_id, availability_topic, config.mqtt_user, config.mqtt_pass);
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

        // base/light/LONG_ADDR/set
        std::string light_single_topic = std::format("{}{}", config.mqtt_base_topic, "/light/+/set");
        mqtt.subscribe(light_single_topic);
        ESP_LOGI(TAG, "Subscribed to lights: %s", light_single_topic.c_str());

        // base/light/group/GROUP_ID/set
        std::string light_group_topic = std::format("{}{}", config.mqtt_base_topic, "/light/group/+/set");
        mqtt.subscribe(light_group_topic);
        ESP_LOGI(TAG, "Subscribed to light groups: %s", light_group_topic.c_str());

        // base/light/broadcast/set
        std::string light_broadcast_topic = std::format("{}{}", config.mqtt_base_topic, "/light/broadcast/set");
        mqtt.subscribe(light_broadcast_topic);
        ESP_LOGI(TAG, "Subscribed to broadcast: %s", light_broadcast_topic.c_str());

        // base/config/group/set
        std::string config_group_topic = std::format("{}{}", config.mqtt_base_topic, CONFIG_DALI2MQTT_MQTT_GROUP_SET_SUBTOPIC);
        mqtt.subscribe(config_group_topic);
        ESP_LOGI(TAG, "Subscribed to group config: %s", config_group_topic.c_str());

        // base/scene/set
        std::string scene_cmd_topic = std::format("{}{}", config.mqtt_base_topic, CONFIG_DALI2MQTT_MQTT_SCENE_CMD_SUBTOPIC);
        mqtt.subscribe(scene_cmd_topic);
        ESP_LOGI(TAG, "Subscribed to scenes: %s", scene_cmd_topic.c_str());

        MQTTDiscovery mqtt_discovery;
        mqtt_discovery.publishAllDevices();
        ESP_LOGI(TAG, "MQTT discovery messages published.");

        DaliDeviceController::getInstance().start();
    }

    void Lifecycle::onMqttData(const std::string& topic, const std::string& data) {
        MQTTCommandHandler::getInstance().handle(topic, data);
    }
}