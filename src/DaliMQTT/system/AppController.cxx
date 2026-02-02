#include "system/ConfigManager.hxx"
#include "dali/DaliAdapter.hxx"
#include "mqtt/MQTTCommandProcess.hxx"
#include "system/SystemHardwareControls.hxx"
#include "dali/DaliGroupManagement.hxx"
#include "dali/DaliSceneManagement.hxx"
#include "system/AppController.hxx"
#include "mqtt/MQTTClient.hxx"
#include "dali/DaliDeviceController.hxx"
#include "mqtt/HADiscovery.hxx"
#include <utils/StringUtils.hxx>
#include "webui/WebUI.hxx"
#include "wifi/Wifi.hxx"
#include "system/SyslogConfig.hxx"

namespace daliMQTT
{
    static constexpr  char TAG[] = "AppController";

    void AppController::startProvisioningMode() {
        ESP_LOGI(TAG, "Starting in provisioning mode...");
        SystemHardwareControls::checkOtaValidation();
        auto& wifi = Wifi::Instance();
        wifi.init();

        wifi.startAP(CONFIG_DALI2MQTT_WIFI_AP_SSID, CONFIG_DALI2MQTT_WIFI_AP_PASS);

        WebUI::Instance().start();
        ESP_LOGI(TAG, "Web service for provisioning is running.");
    }

    void AppController::startNormalMode() {
        ESP_LOGI(TAG, "Starting in normal mode...");

        SystemHardwareControls::checkOtaValidation();
        SystemHardwareControls::startResetConfigurationButtonMonitor();
        initDaliSubsystem();
        MQTTCommandProcess::Instance().init();
        WebUI::Instance().start();
        initNetworkSubsystem();
    }

    void AppController::initDaliSubsystem() {
        ESP_LOGI(TAG, "Initializing DALI Subsystem...");
        auto& dali_api = DaliAdapter::Instance();
        dali_api.init(static_cast<gpio_num_t>(CONFIG_DALI2MQTT_DALI_RX_PIN), static_cast<gpio_num_t>(CONFIG_DALI2MQTT_DALI_TX_PIN));

        auto& dali_manager = DaliDeviceController::Instance();
        dali_manager.init();
        dali_manager.start();

        DaliGroupManagement::Instance().init();
        DaliSceneManagement::Instance().init();
    }

    void AppController::initNetworkSubsystem() {
        const auto config = ConfigManager::Instance().getConfig();
        auto& wifi = Wifi::Instance();

        wifi.init();

        wifi.onConnected = [this]() { this->onNetworkConnected(); };
        wifi.onDisconnected = [this]() { this->onNetworkDisconnected(); };

        wifi.connectToAP(config.wifi_ssid, config.wifi_password);
    }

    void AppController::onNetworkConnected() {
        ESP_LOGI(TAG, "Network Connected. IP: %s", Wifi::Instance().getIpAddress().c_str());
        m_network_connected = true;

        const auto config = ConfigManager::Instance().getConfig();

        if (config.syslog_enabled && !config.syslog_server.empty()) {
            SyslogConfig::Instance().init(config.syslog_server);
        }

        auto& mqtt = MQTTClient::Instance();
        const std::string availability_topic = utils::stringFormat("%s%s", config.mqtt_base_topic.c_str(), CONFIG_DALI2MQTT_MQTT_AVAILABILITY_TOPIC);

        mqtt.init(config.mqtt_uri, config.client_id, availability_topic, config.mqtt_user, config.mqtt_pass);
        mqtt.onConnected = [this]() { this->onMqttConnected(); };
        mqtt.onDisconnected = [this]() { this->onMqttDisconnected(); };
        mqtt.connect();
    }

    void AppController::onNetworkDisconnected() {
        ESP_LOGW(TAG, "Network Disconnected.");
        m_network_connected = false;
    }

    void AppController::onMqttConnected() {
        ESP_LOGI(TAG, "MQTT connected successfully.");
        m_mqtt_connected = true;
        auto config = ConfigManager::Instance().getConfig();
        auto const& mqtt = MQTTClient::Instance();

        std::string availability_topic = utils::stringFormat("%s%s", config.mqtt_base_topic.c_str(), CONFIG_DALI2MQTT_MQTT_AVAILABILITY_TOPIC);
        mqtt.publish(availability_topic, CONFIG_DALI2MQTT_MQTT_PAYLOAD_ONLINE, 1, true);

        std::string ip_topic = utils::stringFormat("%s/ip_addr", config.mqtt_base_topic.c_str());
        std::string ip_addr = Wifi::Instance().getIpAddress();
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
            publishHAMqttDiscovery();
        }

        DaliGroupManagement::Instance().publishAllGroups();
    }

    void AppController::onMqttDisconnected() {
        ESP_LOGW(TAG, "MQTT Disconnected.");
        m_mqtt_connected = false;
    }

    void AppController::publishHAMqttDiscovery() const
    {
        if (!m_mqtt_connected) {
            ESP_LOGW(TAG, "Cannot publish discovery: MQTT not connected.");
            return;
        }
        ESP_LOGI(TAG, "Publishing HA Discovery...");
        MQTTHomeAssistantDiscovery hass_discovery;
        hass_discovery.publishAllDevices();
    }

    void AppController::onConfigReloadRequest() {
        ESP_LOGI(TAG, "Hot-reloading MQTT Configuration...");
        const auto config = ConfigManager::Instance().getConfig();
        const std::string availability_topic = utils::stringFormat("%s%s", config.mqtt_base_topic.c_str(), CONFIG_DALI2MQTT_MQTT_AVAILABILITY_TOPIC);

        MQTTClient::Instance().reloadConfig(
            config.mqtt_uri,
            config.client_id,
            config.mqtt_user,
            config.mqtt_pass,
            availability_topic
        );
    }
} // daliMQTT