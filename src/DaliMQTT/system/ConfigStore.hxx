// Copyright (c) 2026 Alice-Trade Inc.
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef DALIMQTT_CONFIGSTORE_HXX
#define DALIMQTT_CONFIGSTORE_HXX

#include "system/ConfigDefaults.icc"
#include "system/ConfigStructure.hxx"
#include <esp_err.h>
#include <memory>
#include <mutex>

namespace daliMQTT {

class ConfigStore {
  public:
    ConfigStore() = default;
    ~ConfigStore() = default;

    ConfigStore(const ConfigStore&) = delete;
    ConfigStore& operator=(const ConfigStore&) = delete;

    esp_err_t init();
    esp_err_t load();
    esp_err_t save(const ConfigStructure& newConfig);
    esp_err_t factoryReset();

    [[nodiscard]] inline std::shared_ptr<const ConfigStructure> get() const noexcept;
    [[nodiscard]] inline bool isConfigured() const noexcept;

  private:
    static esp_err_t mountLittleFs();
    static void generateDefaultClientId(ConfigStructure& cfg);

    mutable std::mutex m_mutex{};
    std::shared_ptr<const ConfigStructure> m_config{std::make_shared<ConfigStructure>(makeDefaultConfig())};
    bool m_initialized{false};
};

} // namespace daliMQTT

#include "system/ConfigStore.icc"

#endif // DALIMQTT_CONFIGSTORE_HXX