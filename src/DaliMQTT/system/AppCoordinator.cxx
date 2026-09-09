// Copyright (c) 2026 Alice-Trade Inc.
// SPDX-License-Identifier: GPL-3.0-or-later

#include "system/AppCoordinator.hxx"
#include <esp_log.h>
#include <esp_system.h>
#include <esp_timer.h>

namespace daliMQTT {

static constexpr char TAG[] = "AppCoordinator";

AppCoordinator::AppCoordinator(const Dependencies& deps) : m_deps(deps) {}

void AppCoordinator::start() {
    ESP_LOGI(TAG, "==========================================");
    ESP_LOGI(TAG, "  DALI-to-MQTT Bridge Core v.%s (%s)", DALIMQTT_VERSION, NetworkPlatform::getInterfaceName());
    ESP_LOGI(TAG, "==========================================");

    m_deps.controls.checkAndValidateOta();
    m_deps.controls.init(GPIO_NUM_0);
    m_deps.controls.setResetCallback(&onFactoryResetRequestedEntry, this);

    m_deps.config.init();
    m_deps.config.load();
    const auto cfg = m_deps.config.get();

    m_deps.scheduler.setTelemetryCallback(&onMinuteTelemetry, this, cfg->telemetryIntervalSec);
    m_deps.scheduler.setBusHealthCallback(&onBusHealthCheck, this, 10);
    m_deps.scheduler.setOtaCheckCallback(&onOtaCheck, this, static_cast<uint32_t>(cfg->otaCheckIntervalDays) * 86400);

    m_deps.dali.init(cfg->buses[0]);

    m_deps.network.setEventQueue(m_deps.systemQueue);
    m_deps.network.init();

    if (NetworkPlatform::supportsAccessPoint() && !NetworkPlatform::isConfigured(*cfg)) {
        m_state.store(AppState::Provisioning);
        ESP_LOGI(TAG, "Network not configured. Starting Provisioning AP: '%s'...", CONFIG_DALI2MQTT_WIFI_AP_SSID);
        m_deps.network.startAccessPoint(CONFIG_DALI2MQTT_WIFI_AP_SSID, CONFIG_DALI2MQTT_WIFI_AP_PASS);
        m_deps.network.startMdns(CONFIG_DALI2MQTT_WEBUI_DEFAULT_MDNS_DOMAIN, "Setup");
    } else {
        m_state.store(AppState::ConnectingNetwork);
        ESP_LOGI(TAG, "Connecting to %s infrastructure...", NetworkPlatform::getInterfaceName());
        m_deps.network.start(*cfg);
    }

    m_deps.webServer.start();
    m_deps.dali.Registry().requestBroadcastSync(2000, 1400);
}

void AppCoordinator::handleNetworkConnected() {
    const auto cfg = m_deps.config.get();
    ESP_LOGI(TAG, "[%s] Connected! IP: %s", NetworkPlatform::getInterfaceName(), m_deps.network.getIpAddress().c_str());

    m_deps.network.startMdns(cfg->httpDomain.c_str(), cfg->clientId.c_str());

    if (cfg->syslogEnabled && !cfg->syslogServer.empty()) {
        m_deps.syslog.start(cfg->syslogServer.c_str());
    }

    if (cfg->isMqttConfigured()) {
        ESP_LOGI(TAG, "Connecting to MQTT broker: %s", cfg->mqttUri.c_str());
        m_deps.mqttClient.init(*cfg);
        m_deps.mqttBridge.start();
        m_deps.mqttClient.connect();
        m_state.store(AppState::RunningNormal);
    } else {
        ESP_LOGW(TAG, "MQTT broker URI is not configured! Please configure MQTT via WebUI at http://%s.local", cfg->httpDomain.c_str());
        m_state.store(AppState::Provisioning);
    }
}

void AppCoordinator::handleNetworkDisconnected() {
    ESP_LOGW(TAG, "[%s] Connection lost. Suspending MQTT and Syslog.", NetworkPlatform::getInterfaceName());
    m_state.store(AppState::ConnectingNetwork);
    m_deps.mqttClient.disconnect();
    m_deps.syslog.stop();
}

void AppCoordinator::handleConfigReload() const {
    const auto cfg = m_deps.config.get();
    if (cfg->isMqttConfigured()) {
        ESP_LOGI(TAG, "Reloading MQTT parameters...");
        m_deps.mqttBridge.stop();
        m_deps.mqttClient.reload(*cfg);
        m_deps.mqttBridge.start();
    }
}

void AppCoordinator::onFactoryResetRequestedEntry(void* ctx) {
    const auto* self = static_cast<AppCoordinator*>(ctx);
    if (self && self->m_deps.systemQueue) {
        constexpr auto ev = SystemEventType::FactoryResetRequested;
        xQueueSend(self->m_deps.systemQueue, &ev, 0);
    }
}

void AppCoordinator::factoryReset() const {
    xTaskCreate(
        [](void* arg) {
            auto* self = static_cast<const AppCoordinator*>(arg);
            ESP_LOGW(TAG, "Factory Reset confirmed. Erasing NVS and rebooting...");
            self->m_deps.config.factoryReset();
            vTaskDelay(pdMS_TO_TICKS(1000));
            esp_restart();
        },
        "fact_rst_task", 4096, const_cast<AppCoordinator*>(this), 5, nullptr);
}

void AppCoordinator::onMinuteTelemetry(void* ctx) {
    const auto* self = static_cast<AppCoordinator*>(ctx);
    if (self->m_deps.mqttClient.isConnected()) {
        self->m_deps.mqttBridge.publishTelemetry();
    }
}

void AppCoordinator::onBusHealthCheck(void* ctx) {
    auto* self = static_cast<AppCoordinator*>(ctx);
    const auto health = self->m_deps.dali.checkHealth();
    static auto lastHealth = BusHealth::Ok;

    if (health != lastHealth) {
        lastHealth = health;
        if (self->m_deps.mqttClient.isConnected()) {
            const auto cfg = self->m_deps.config.get();
            char topic[128];
            snprintf(topic, sizeof(topic), "%s/system/fault", cfg->mqttBaseTopic.c_str());
            const char* payload = (health == BusHealth::ShortCircuit)
                ? R"({"bus_fault":"short_circuit_or_no_power"})"
                : R"({"bus_fault":"none"})";
            self->m_deps.mqttClient.publish(topic, payload, 1, true);
        }
    }
}

void AppCoordinator::onOtaCheck(void* ctx) {
    auto* self = static_cast<AppCoordinator*>(ctx);
    if (self->m_deps.network.getStatus() != NetworkStatus::Connected) return;

    const auto cfg = self->m_deps.config.get();
    if (!cfg->otaBaseUrl.empty()) {
        self->m_deps.ota.checkForUpdateAsync(cfg->otaBaseUrl.c_str());
    }
}

} // namespace daliMQTT