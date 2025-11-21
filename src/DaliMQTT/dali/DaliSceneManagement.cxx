#include "DaliSceneManagement.hxx"
#include "DaliDeviceController.hxx"
#include "DaliAPI.hxx"

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
        auto& dali = DaliAPI::getInstance();
        return dali.sendCommand(DALI_ADDRESS_TYPE_BROADCAST, 0, DALI_COMMAND_GO_TO_SCENE_0 + sceneId);
    }

    esp_err_t DaliSceneManagement::saveScene(uint8_t sceneId, const SceneDeviceLevels& levels) const
    {
        if (sceneId >= 16) {
            return ESP_ERR_INVALID_ARG;
        }
        ESP_LOGI(TAG, "Saving configuration for DALI Scene %d for %zu devices", sceneId, levels.size());
        auto& dali = DaliAPI::getInstance();

        for (const auto& [addr, level] : levels) {
            ESP_LOGD(TAG, "Setting device %d to level %d for scene %d", addr, level, sceneId);

            esp_err_t res = dali.sendCommand(
                DALI_ADDRESS_TYPE_SPECIAL_CMD,
                level,
                DALI_SPECIAL_COMMAND_DATA_TRANSFER_REGISTER
            );
            if (res != ESP_OK) {
                ESP_LOGE(TAG, "Failed to set DTR for device %d", addr);
                continue;
            }
            vTaskDelay(pdMS_TO_TICKS(10));


            res = dali.sendCommand(
                DALI_ADDRESS_TYPE_SHORT,
                addr,
                DALI_COMMAND_STORE_DTR_AS_SCENE_0 + sceneId,
                true
            );
            if (res != ESP_OK) {
                ESP_LOGE(TAG, "Failed to store scene %d for device %d", sceneId, addr);
            }

            vTaskDelay(pdMS_TO_TICKS(10));
        }

        ESP_LOGI(TAG, "Finished saving configuration for Scene %d", sceneId);
        return ESP_OK;
    }
    SceneDeviceLevels DaliSceneManagement::getSceneLevels(uint8_t sceneId) const
    {
        SceneDeviceLevels results;
        if (sceneId >= 16) return results;

        auto& dali = DaliAPI::getInstance();
        auto devices = DaliDeviceController::getInstance().getDevices();

        ESP_LOGI(TAG, "Querying levels for Scene %d...", sceneId);

        for (const auto& device : devices | std::views::values) {
            if (!device.is_present) continue;

            auto level_opt = dali.sendQuery(
                DALI_ADDRESS_TYPE_SHORT,
                device.short_address,
                DALI_COMMAND_QUERY_SCENE_LEVEL_0 + sceneId
            );

            if (level_opt.has_value()) {
                results[device.short_address] = level_opt.value();
            } else {
                results[device.short_address] = 255;
            }
            vTaskDelay(pdMS_TO_TICKS(10));
        }
        return results;
    }

} // namespace daliMQTT