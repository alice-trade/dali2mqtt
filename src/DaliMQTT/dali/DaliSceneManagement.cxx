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

    esp_err_t DaliSceneManagement::activateScene(uint8_t bus_id, const uint8_t sceneId) const
    {
        if (sceneId >= 16) return ESP_ERR_INVALID_ARG;
        ESP_LOGI(TAG, "Activating DALI Scene %d on bus %d", sceneId, bus_id);

        auto* adapter = DaliDeviceController::Instance().getAdapter(bus_id);
        if(!adapter) return ESP_FAIL;

        return adapter->sendCommand(DaliAddressType::Broadcast, 0,
                static_cast<Commands::OpCode>(static_cast<uint8_t>(Commands::OpCode::GoToScene0) + sceneId)
            );
    }

    esp_err_t DaliSceneManagement::saveScene(uint8_t bus_id, uint8_t sceneId, const SceneDeviceLevels& levels) const {
        if (sceneId >= 16) return ESP_ERR_INVALID_ARG;
        ESP_LOGI(TAG, "Saving configuration for DALI Scene %d on bus %d", sceneId, bus_id);

        auto* adapter = DaliDeviceController::Instance().getAdapter(bus_id);
        if(!adapter) return ESP_FAIL;

        for (uint8_t addr = 0; addr < 64; ++addr) {
            uint8_t level = levels[addr];
            if (level != 255) {
                ESP_LOGD(TAG, "Setting device %d to level %d for scene %d", addr, level, sceneId);
                adapter->sendCommand(Commands::SpecialOpCode::Dtr0, level);
                auto storeCmd = static_cast<Commands::OpCode>(0x40 + sceneId);
                adapter->sendCommand(DaliAddressType::Short, addr, storeCmd, true);
                vTaskDelay(pdMS_TO_TICKS(15));
            }
        }
        ESP_LOGI(TAG, "Finished saving configuration for Scene %d", sceneId);
        return ESP_OK;
    }

    SceneDeviceLevels DaliSceneManagement::getSceneLevels(uint8_t bus_id, uint8_t sceneId) const {
        SceneDeviceLevels results;
        results.fill(255);
        if (sceneId >= 16) return results;

        auto* adapter = DaliDeviceController::Instance().getAdapter(bus_id);
        if(!adapter) return results;

        auto devices = DaliDeviceController::Instance().getDevices();
        ESP_LOGI(TAG, "Querying levels for Scene %d on bus %d...", sceneId, bus_id);

        for (const auto& device : devices) {
            const auto& id = getIdentity(device);
            if (!id.available || !std::holds_alternative<ControlGear>(device)) continue;
            if ((id.internal_address).bus() != bus_id) continue;

            const auto queryCmd = static_cast<Commands::OpCode>(0xB0 + sceneId);
            auto res = adapter->sendQuery(DaliAddressType::Short, (id.internal_address).shortAddr(), queryCmd);
            results[(id.internal_address).shortAddr()] = res.value_or(255);
            vTaskDelay(pdMS_TO_TICKS(15));
        }

        return results;
    }

} // namespace daliMQTT
