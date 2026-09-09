// Copyright (c) 2026 Alice-Trade Inc.
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef DALIMQTT_ETHERNETBACKEND_HXX
#define DALIMQTT_ETHERNETBACKEND_HXX

#include "system/ConfigStructure.hxx"
#include "system/SystemEvent.hxx"
#include <atomic>
#include <esp_err.h>
#include <esp_eth.h>
#include <esp_netif.h>
#include <etl/string.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

namespace daliMQTT {

class EthernetBackend {
public:
    EthernetBackend() = default;
    ~EthernetBackend();

    esp_err_t init();
    esp_err_t start(const ConfigStructure& cfg);
    esp_err_t startAccessPoint(const char*, const char*) { return ESP_OK; }
    void stop();

    void startMdns(const char* hostname, const char* clientId);
    inline void setEventQueue(QueueHandle_t queue) noexcept;

    etl::string<16> getIpAddress() const;
    inline NetworkStatus getStatus() const noexcept;

    static constexpr bool supportsAccessPoint() noexcept { return false; }
    static constexpr const char* interfaceName() noexcept { return "Ethernet"; }

private:
    static void ethEventHandler(void* arg, esp_event_base_t base, int32_t id, void* data);
    esp_err_t setupW5500();

    std::atomic<NetworkStatus> m_status{NetworkStatus::Disconnected};
    esp_netif_t* m_ethNetif{nullptr};
    esp_eth_handle_t m_ethHandle{nullptr};
    bool m_initialized{false};
    bool m_mdnsStarted{false};
    QueueHandle_t m_eventQueue{nullptr};
};

} // namespace daliMQTT

#include "network/EthernetBackend.icc"

#endif // DALIMQTT_ETHERNETBACKEND_HXX