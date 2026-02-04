// Copyright (c) 2026 Alice-Trade Inc.
// SPDX-License-Identifier: GPL-2.0-or-later

#include "dali/DaliSceneManagement.hxx"
#include "dali/DaliDeviceController.hxx"
#include "dali/DaliAdapter.hxx"

namespace daliMQTT
{
    static constexpr char TAG[] = "DaliSceneManagement";

    void DaliSceneManagement::init() {
        ESP_LOGI(TAG, "DALI Scene Manager initialized.");
    }

    esp_err_t DaliSceneManagement::activateScene(const uint8_t sceneId) const
    {
        if (sceneId >= 16) {
            return ESP_ERR_INVALID_ARG;
        }
        ESP_LOGI(TAG, "Activating DALI Scene %d", sceneId);
        auto& dali = DaliAdapter::Instance();
        return dali.sendCommand(DaliAddressType::Broadcast,
                0,
                static_cast<Commands::OpCode>(static_cast<uint8_t>(Commands::OpCode::GoToScene0) + sceneId)
            );
    }

    esp_err_t DaliSceneManagement::saveScene(uint8_t sceneId, const SceneDeviceLevels& levels) const
    {
        if (sceneId >= 16) {
            return ESP_ERR_INVALID_ARG;
        }
        ESP_LOGI(TAG, "Saving configuration for DALI Scene %d for %zu devices", sceneId, levels.size());
        auto& dali = DaliAdapter::Instance();

        for (const auto& [addr, level] : levels) {
            ESP_LOGD(TAG, "Setting device %d to level %d for scene %d", addr, level, sceneId);
            dali.sendCommand(Commands::SpecialOpCode::Dtr0, level);

            auto storeCmd = static_cast<Commands::OpCode>(0x40 + sceneId);
            dali.sendCommand(DaliAddressType::Short, addr, storeCmd, true);
        }

        ESP_LOGI(TAG, "Finished saving configuration for Scene %d", sceneId);
        return ESP_OK;
    }
    SceneDeviceLevels DaliSceneManagement::getSceneLevels(uint8_t sceneId) const
    {
        SceneDeviceLevels results;
        if (sceneId >= 16) return results;

        auto& dali = DaliAdapter::Instance();
        auto devices = DaliDeviceController::Instance().getDevices();

        ESP_LOGI(TAG, "Querying levels for Scene %d...", sceneId);

        for (const auto& device : devices | std::views::values) {
            const auto& id = getIdentity(device);
            if (!id.available || !std::holds_alternative<ControlGear>(device)) continue;

            auto queryCmd = static_cast<Commands::OpCode>(0xB0 + sceneId);
            auto res = dali.sendQuery(DaliAddressType::Short, id.short_address, queryCmd);
            results[id.short_address] = res.value_or(255);
        }
        return results;
    }

} // namespace daliMQTT