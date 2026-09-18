// Copyright (c) 2026 Alice-Trade Inc.
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef DALIMQTT_WEBUICONTROLLER_HXX
#define DALIMQTT_WEBUICONTROLLER_HXX

#include "dali/DaliDeviceRegistry.hxx"
#include "mqtt/MqttClient.hxx"
#include "network/NetworkPlatform.hxx"
#include "system/ConfigStore.hxx"
#include "system/OtaService.hxx"
#include <esp_err.h>

#if defined(CONFIG_DALI2MQTT_ENABLE_WEBUI)
#include "webui/ApiContext.hxx"
#include "webui/WebUI.hxx"
#endif

namespace daliMQTT {

class WebUIController {
  public:
    WebUIController(ConfigStore& cfg, NetworkPlatform& net, DaliDeviceRegistry& reg, MqttClient& mqtt, OtaService& ota);
    ~WebUIController() = default;

    esp_err_t start();
    void stop();
    [[nodiscard]] static constexpr bool isEnabled() noexcept;
    [[nodiscard]] bool isRunning() const noexcept;

  private:
#if defined(CONFIG_DALI2MQTT_ENABLE_WEBUI)
    ApiContext m_apiCtx;
    WebUI m_webServer;
#endif
};

} // namespace daliMQTT

#include "system/WebUIController.icc"

#endif // DALIMQTT_WEBUICONTROLLER_HXX