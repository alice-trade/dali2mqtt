#include "system/ConfigManager.hxx"
#include "dali/DaliAdapter.hxx"
#include "mqtt/MQTTCommandProcess.hxx"
#include "system/SystemControl.hxx"
#include "dali/DaliGroupManagement.hxx"
#include "dali/DaliSceneManagement.hxx"
#include "lifecycle/Lifecycle.hxx"
#include "mqtt/MQTTClient.hxx"
#include "dali/DaliDeviceController.hxx"
#include "mqtt/HADiscovery.hxx"
#include <utils/StringUtils.hxx>
#include "webui/WebUI.hxx"
#include "wifi/Wifi.hxx"
#include "system/SyslogConfig.hxx"

namespace daliMQTT
{
    static constexpr  char TAG[] = "LifecycleBase";

    void Lifecycle::startProvisioningMode() {
        ESP_LOGI(TAG, "Starting in provisioning mode...");
        SystemControl::checkOtaValidation();
        auto& wifi = Wifi::getInstance();
        wifi.init();

        wifi.startAP(CONFIG_DALI2MQTT_WIFI_AP_SSID, CONFIG_DALI2MQTT_WIFI_AP_PASS);

        auto& web = WebUI::getInstance();
        web.start();

        ESP_LOGI(TAG, "Web server for provisioning is running.");
    }

    void Lifecycle::startNormalMode() {
        ESP_LOGI(TAG, "Starting in normal mode...");
        auto config = ConfigManager::getInstance().getConfig();
        SystemControl::checkOtaValidation();
        SystemControl::startResetConfigurationButtonMonitor();

        auto& wifi = Wifi::getInstance();
        wifi.init();
        wifi.onConnected = [config]() {
            ESP_LOGI(TAG, "WiFi connected, starting MQTT...");
            if (config.syslog_enabled && !config.syslog_server.empty()) {
                SyslogConfig::getInstance().init(config.syslog_server);
            }
            auto& mqtt = MQTTClient::getInstance();
            MQTTCommandProcess::getInstance().init();

            const std::string availability_topic = utils::stringFormat("%s%s", config.mqtt_base_topic.c_str(), CONFIG_DALI2MQTT_MQTT_AVAILABILITY_TOPIC);            mqtt.init(config.mqtt_uri, config.client_id, availability_topic, config.mqtt_user, config.mqtt_pass);
            mqtt.onConnected = []() { onMqttConnected(); };

            mqtt.connect();
        };
        wifi.onDisconnected = []() {
            ESP_LOGW(TAG, "WiFi disconnected, stopping MQTT.");
            MQTTClient::getInstance().disconnect();
        };

        auto& dali_api = DaliAdapter::getInstance();
        dali_api.init(static_cast<gpio_num_t>(CONFIG_DALI2MQTT_DALI_RX_PIN), static_cast<gpio_num_t>(CONFIG_DALI2MQTT_DALI_TX_PIN));

        auto& dali_manager = DaliDeviceController::getInstance();
        dali_manager.init();

        auto& group_manager = DaliGroupManagement::getInstance();
        group_manager.init();

        auto& scene_manager = DaliSceneManagement::getInstance();
        scene_manager.init();

        auto& web = WebUI::getInstance();
        web.start();
        wifi.connectToAP(config.wifi_ssid, config.wifi_password);
    }

    void Lifecycle::onMqttConnected() {
        ESP_LOGI(TAG, "MQTT connected successfully.");
        auto config = ConfigManager::getInstance().getConfig();
        auto const& mqtt = MQTTClient::getInstance();

        std::string availability_topic = utils::stringFormat("%s%s", config.mqtt_base_topic.c_str(), CONFIG_DALI2MQTT_MQTT_AVAILABILITY_TOPIC);
        mqtt.publish(availability_topic, CONFIG_DALI2MQTT_MQTT_PAYLOAD_ONLINE, 1, true);

        std::string ip_topic = utils::stringFormat("%s/ip_addr", config.mqtt_base_topic.c_str());
        std::string ip_addr = Wifi::getInstance().getIpAddress();
        mqtt.publish(ip_topic, ip_addr, 1, true);

        std::string version_topic = utils::stringFormat("%s/version", config.mqtt_base_topic.c_str());
        mqtt.publish(version_topic, DALIMQTT_VERSION, 1, true);

        // base/light/LONG_ADDR/set
        std::string light_single_topic = utils::stringFormat("%s/light/+/set", config.mqtt_base_topic.c_str());
        mqtt.subscribe(light_single_topic);
        ESP_LOGI(TAG, "Subscribed to lights: %s", light_single_topic.c_str());

        // base/light/group/GROUP_ID/set
        std::string light_group_topic = utils::stringFormat("%s/light/group/+/set", config.mqtt_base_topic.c_str());
        mqtt.subscribe(light_group_topic);
        ESP_LOGI(TAG, "Subscribed to light groups: %s", light_group_topic.c_str());

        // base/light/broadcast/set
        std::string light_broadcast_topic = utils::stringFormat("%s/light/broadcast/set", config.mqtt_base_topic.c_str());
        mqtt.subscribe(light_broadcast_topic);
        ESP_LOGI(TAG, "Subscribed to broadcast: %s", light_broadcast_topic.c_str());

        // base/config/group/set
        std::string config_group_topic = utils::stringFormat("%s%s", config.mqtt_base_topic.c_str(), CONFIG_DALI2MQTT_MQTT_GROUP_SET_SUBTOPIC);
        mqtt.subscribe(config_group_topic);
        ESP_LOGI(TAG, "Subscribed to group config: %s", config_group_topic.c_str());

        // base/scene/set
        std::string scene_cmd_topic = utils::stringFormat("%s%s", config.mqtt_base_topic.c_str(), CONFIG_DALI2MQTT_MQTT_SCENE_CMD_SUBTOPIC);
        mqtt.subscribe(scene_cmd_topic);
        ESP_LOGI(TAG, "Subscribed to scenes: %s", scene_cmd_topic.c_str());

        // base/cmd/send
        std::string cmd_topic = utils::stringFormat("%s/cmd/send", config.mqtt_base_topic.c_str());
        mqtt.subscribe(cmd_topic);
        ESP_LOGW(TAG, "DEBUG INTERFACE ENABLED. Subscribed to: %s", cmd_topic.c_str());

        // base/cmd/sync
        std::string sync_topic = utils::stringFormat("%s/cmd/sync", config.mqtt_base_topic.c_str());
        mqtt.subscribe(sync_topic);
        ESP_LOGI(TAG, "Subscribed to sync: %s", sync_topic.c_str());

        // base/config/bus/scan
        std::string bus_scan_topic = utils::stringFormat("%s/config/bus/scan", config.mqtt_base_topic.c_str());
        mqtt.subscribe(bus_scan_topic);

        // base/config/bus/initialize
        std::string bus_init_topic = utils::stringFormat("%s/config/bus/initialize", config.mqtt_base_topic.c_str());
        mqtt.subscribe(bus_init_topic);
        ESP_LOGI(TAG, "Subscribed to bus management: %s, %s", bus_scan_topic.c_str(), bus_init_topic.c_str());

        // base/config/get
        std::string config_get_topic = utils::stringFormat("%s/config/get", config.mqtt_base_topic.c_str());
        mqtt.subscribe(config_get_topic);

        // base/config/set
        std::string config_set_topic = utils::stringFormat("%s/config/set", config.mqtt_base_topic.c_str());
        mqtt.subscribe(config_set_topic);
        ESP_LOGI(TAG, "Subscribed to system config management: %s, %s", config_get_topic.c_str(), config_set_topic.c_str());

        if (config.hass_discovery_enabled) {
            MQTTHomeAssistantDiscovery hass_discovery;
            hass_discovery.publishAllDevices();
            ESP_LOGI(TAG, "MQTT discovery messages published.");
        }

        DaliGroupManagement::getInstance().publishAllGroups();

        DaliDeviceController::getInstance().start();
    }
} // daliMQTT