// Copyright (c) 2026 Alice-Trade Inc.
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef DALIMQTT_APICONTEXT_HXX
#define DALIMQTT_APICONTEXT_HXX

#include "utils/Arena.hxx"
#include <mutex>

namespace daliMQTT {

class ConfigStore;
class NetworkPlatform;
class DaliDeviceRegistry;
class MqttClient;
class OtaService;

struct ApiContext {
    ConfigStore& config;
    NetworkPlatform& network;
    DaliDeviceRegistry& daliRegistry;
    MqttClient& mqttClient;
    OtaService& ota;

    mutable std::mutex arenaMutex{};
    mutable memory::StaticArena<8192> arena{};

    ApiContext(ConfigStore& cfg, NetworkPlatform& net, DaliDeviceRegistry& reg, MqttClient& mqtt, OtaService& o)
        : config(cfg), network(net), daliRegistry(reg), mqttClient(mqtt), ota(o) {}
};

} // namespace daliMQTT

#endif // DALIMQTT_APICONTEXT_HXX