// Copyright (c) 2026 Alice-Trade Inc.
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef DALIMQTT_CONFIGSTRUCTURE_HXX
#define DALIMQTT_CONFIGSTRUCTURE_HXX

#include <array>
#include <cstdint>
#include <etl/string.h>

namespace daliMQTT {

struct BusHardwareConfig {
    bool enabled{false};
    int8_t rxPin{-1};
    int8_t txPin{-1};
};

struct ConfigStructure {
    etl::string<32> wifiSsid{};
    etl::string<64> wifiPass{};

    etl::string<128> mqttUri{};
    etl::string<32> mqttUser{};
    etl::string<64> mqttPass{};
    etl::string<64> mqttBaseTopic{};
    etl::string<32> clientId{};
    etl::string<1536> mqttCaCert{};

    etl::string<32> httpDomain{};
    etl::string<32> httpUser{};
    etl::string<64> httpPass{};

    std::array<BusHardwareConfig, 1> buses{};

    etl::string<128> otaBaseUrl{};
    etl::string<64> syslogServer{};
    bool syslogEnabled{false};

    uint8_t otaCheckIntervalDays{};
    uint32_t telemetryIntervalSec{};
    uint32_t daliPollIntervalMs{300000};
    bool hassDiscoveryEnabled{false};
    bool configuredFlag{false};

    [[nodiscard]] bool isMqttConfigured() const noexcept {
        return !mqttUri.empty();
    }

    [[nodiscard]] bool isFullyConfigured() const noexcept {
        return configuredFlag && isMqttConfigured();
    }
};

} // namespace daliMQTT

#endif // DALIMQTT_CONFIGSTRUCTURE_HXX