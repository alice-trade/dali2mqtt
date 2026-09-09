// Copyright (c) 2026 Alice-Trade Inc.
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef DALIMQTT_WIFIBACKEND_HXX
#define DALIMQTT_WIFIBACKEND_HXX

#include "system/ConfigStructure.hxx"
#include "system/SystemEvent.hxx"
#include <atomic>
#include <esp_netif.h>
#include <esp_wifi.h>
#include <etl/string.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

namespace daliMQTT {

class WifiBackend {
public:
    WifiBackend() = default;
    ~WifiBackend();

    esp_err_t init();
    esp_err_t start(const ConfigStructure& cfg);
    esp_err_t startAccessPoint(const char* ssid, const char* password);
    void stop();

    void startMdns(const char* hostname, const char* clientId);
    inline void setEventQueue(QueueHandle_t queue) noexcept;

    etl::string<16> getIpAddress() const;
    inline NetworkStatus getStatus() const noexcept;

    static constexpr bool supportsAccessPoint() noexcept { return true; }
    static constexpr const char* interfaceName() noexcept { return "Wi-Fi"; }

private:
    static void wifiEventHandler(void* arg, esp_event_base_t base, int32_t id, void* data);

    std::atomic<NetworkStatus> m_status{NetworkStatus::Disconnected};
    esp_netif_t* m_staNetif{nullptr};
    esp_netif_t* m_apNetif{nullptr};
    uint8_t m_retryCount{0};
    bool m_initialized{false};
    bool m_mdnsStarted{false};
    QueueHandle_t m_eventQueue{nullptr};
};

} // namespace daliMQTT

#include "network/WifiBackend.icc"

#endif // DALIMQTT_WIFIBACKEND_HXX