// Copyright (c) 2026 Alice-Trade Inc.
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef DALIMQTT_NETWORKPLATFORM_HXX
#define DALIMQTT_NETWORKPLATFORM_HXX

#include "system/ConfigStructure.hxx"
#include "system/SystemEvent.hxx"
#include <concepts>
#include <esp_err.h>
#include <etl/string.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

namespace daliMQTT {

template <typename T>
concept NetworkBackendConcept = requires(T b, const ConfigStructure& cfg, QueueHandle_t q, const char* str) {
    { b.init() } -> std::same_as<esp_err_t>;
    { b.start(cfg) } -> std::same_as<esp_err_t>;
    { b.startAccessPoint(str, str) } -> std::same_as<esp_err_t>;
    { b.stop() } -> std::same_as<void>;
    { b.startMdns(str, str) } -> std::same_as<void>;
    { b.getIpAddress() } -> std::same_as<etl::string<16>>;
    { b.getStatus() } -> std::same_as<NetworkStatus>;
    { b.setEventQueue(q) } -> std::same_as<void>;
    { T::supportsAccessPoint() } -> std::same_as<bool>;
    { T::interfaceName() } -> std::same_as<const char*>;
};

} // namespace daliMQTT

#if defined(CONFIG_DALI2MQTT_NET_ETHERNET)
    #include "network/EthernetBackend.hxx"
    namespace daliMQTT { using SelectedBackend = EthernetBackend; }
#else
    #include "network/WifiBackend.hxx"
    namespace daliMQTT { using SelectedBackend = WifiBackend; }
#endif

namespace daliMQTT {

class NetworkPlatform {
public:
    NetworkPlatform() = default;
    ~NetworkPlatform() = default;

    inline esp_err_t init();
    inline esp_err_t start(const ConfigStructure& cfg);
    inline esp_err_t startAccessPoint(const char* ssid, const char* pass);
    inline void stop();

    inline void startMdns(const char* hostname, const char* clientId);
    inline void setEventQueue(QueueHandle_t queue) noexcept;

    [[nodiscard]] inline etl::string<16> getIpAddress() const;
    [[nodiscard]] inline NetworkStatus getStatus() const noexcept;
    [[nodiscard]] inline bool isConnected() const noexcept;

    [[nodiscard]] static constexpr bool supportsAccessPoint() noexcept;
    [[nodiscard]] static constexpr const char* getInterfaceName() noexcept;

    [[nodiscard]] static constexpr bool isConfigured(const ConfigStructure& cfg);

private:
    SelectedBackend m_backend{};
};

static_assert(NetworkBackendConcept<SelectedBackend>, "Selected backend must satisfy NetworkBackendConcept");

} // namespace daliMQTT

#include "network/NetworkPlatform.icc"

#endif // DALIMQTT_NETWORKPLATFORM_HXX