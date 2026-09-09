// Copyright (c) 2026 Alice-Trade Inc.
// SPDX-License-Identifier: GPL-3.0-or-later

#include "network/WifiBackend.hxx"
#include <cstring>
#include <esp_check.h>
#include <esp_event.h>
#include <esp_log.h>
#include <lwip/ip4_addr.h>
#include <mdns.h>

namespace daliMQTT {

static constexpr char TAG[] = "Wifi";

WifiBackend::~WifiBackend() {
    stop();
}

esp_err_t WifiBackend::init() {
    if (m_initialized) return ESP_OK;

    ESP_RETURN_ON_ERROR(esp_netif_init(), TAG, "Netif init failed");
    ESP_RETURN_ON_ERROR(esp_event_loop_create_default(), TAG, "Event loop init failed");

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_RETURN_ON_ERROR(esp_wifi_init(&cfg), TAG, "Wi-Fi init failed");

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifiEventHandler, this, nullptr));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifiEventHandler, this, nullptr));

    m_initialized = true;
    return ESP_OK;
}

esp_err_t WifiBackend::start(const ConfigStructure& cfg) {
    if (cfg.wifiSsid.empty()) return ESP_ERR_INVALID_ARG;
    m_status.store(NetworkStatus::Connecting);

    if (!m_staNetif) m_staNetif = esp_netif_create_default_wifi_sta();

    wifi_config_t wifiCfg{};
    strncpy(reinterpret_cast<char*>(wifiCfg.sta.ssid), cfg.wifiSsid.c_str(), sizeof(wifiCfg.sta.ssid) - 1);
    if (!cfg.wifiPass.empty()) {
        strncpy(reinterpret_cast<char*>(wifiCfg.sta.password), cfg.wifiPass.c_str(), sizeof(wifiCfg.sta.password) - 1);
    }

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifiCfg));
    return esp_wifi_start();
}

esp_err_t WifiBackend::startAccessPoint(const char* ssid, const char* password) {
    m_status.store(NetworkStatus::ProvisioningAp);
    if (!m_apNetif) m_apNetif = esp_netif_create_default_wifi_ap();

    wifi_config_t wifiCfg{};
    strncpy(reinterpret_cast<char*>(wifiCfg.ap.ssid), ssid, sizeof(wifiCfg.ap.ssid) - 1);
    wifiCfg.ap.ssid_len = static_cast<uint8_t>(strlen(ssid));
    wifiCfg.ap.max_connection = 4;
    wifiCfg.ap.authmode = (password && strlen(password) >= 8) ? WIFI_AUTH_WPA_WPA2_PSK : WIFI_AUTH_OPEN;

    if (password && strlen(password) >= 8) {
        strncpy(reinterpret_cast<char*>(wifiCfg.ap.password), password, sizeof(wifiCfg.ap.password) - 1);
    }

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifiCfg));
    return esp_wifi_start();
}

void WifiBackend::stop() {
    esp_wifi_disconnect();
    esp_wifi_stop();
    m_status.store(NetworkStatus::Disconnected);
}

etl::string<16> WifiBackend::getIpAddress() const {
    if (m_status.load() != NetworkStatus::Connected || !m_staNetif) {
        return "0.0.0.0";
    }
    esp_netif_ip_info_t ipInfo{};
    if (esp_netif_get_ip_info(m_staNetif, &ipInfo) == ESP_OK) {
        char buf[16];
        esp_ip4addr_ntoa(&ipInfo.ip, buf, sizeof(buf));
        return buf;
    }
    return "0.0.0.0";
}

void WifiBackend::startMdns(const char* hostname, const char* clientId) {
    if (m_mdnsStarted) return;
    if (mdns_init() != ESP_OK) return;

    mdns_hostname_set(hostname);
    char instanceName[64];
    snprintf(instanceName, sizeof(instanceName), "DALI Bridge (%s)", clientId);
    mdns_instance_name_set(instanceName);

    mdns_service_add(nullptr, "_http", "_tcp", 80, nullptr, 0);
    ESP_LOGI(TAG, "mDNS responder started: http://%s.local", hostname);
    m_mdnsStarted = true;
}

void WifiBackend::wifiEventHandler(void* arg, esp_event_base_t eventBase, int32_t eventId, void* eventData) {
    auto* self = static_cast<WifiBackend*>(arg);
    if (!self) return;

    if (eventBase == WIFI_EVENT) {
        switch (eventId) {
        case WIFI_EVENT_STA_START:
            esp_wifi_connect();
            break;
        case WIFI_EVENT_STA_DISCONNECTED:
            self->m_status.store(NetworkStatus::Disconnected);
            self->m_retryCount++;
            ESP_LOGW(TAG, "Wi-Fi disconnected. Retry #%d...", self->m_retryCount);
            if (self->m_eventQueue) {
                constexpr auto ev = SystemEventType::NetworkDisconnected;
                xQueueSend(self->m_eventQueue, &ev, 0);
            }
            esp_wifi_connect();
            break;
        default:
            break;
        }
    } else if (eventBase == IP_EVENT && eventId == IP_EVENT_STA_GOT_IP) {
        auto* event = static_cast<ip_event_got_ip_t*>(eventData);
        self->m_retryCount = 0;
        self->m_status.store(NetworkStatus::Connected);
        ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&event->ip_info.ip));

        if (self->m_eventQueue) {
            constexpr auto ev = SystemEventType::NetworkConnected;
            xQueueSend(self->m_eventQueue, &ev, 0);
        }
    }
}

} // namespace daliMQTT