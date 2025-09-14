#include "DaliSceneManagement.hxx"
#include "DaliAPI.hxx"
#include <esp_log.h>
#include <format>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

namespace daliMQTT
{
    static constexpr char TAG[] = "DaliSceneManagement";

    void DaliSceneManagement::init() {
        ESP_LOGI(TAG, "DALI Scene Manager initialized.");
    }

    esp_err_t DaliSceneManagement::activateScene(uint8_t sceneId) {
        if (sceneId >= 16) {
            return ESP_ERR_INVALID_ARG;
        }

        std::lock_guard lock(m_mutex);
        ESP_LOGI(TAG, "Activating DALI Scene %d", sceneId);
        auto& dali = DaliAPI::getInstance();
        return dali.sendCommand(DALI_ADDRESS_TYPE_BROADCAST, 0, DALI_COMMAND_GO_TO_SCENE_0 + sceneId);
    }

    esp_err_t DaliSceneManagement::saveScene(uint8_t sceneId, const SceneDeviceLevels& levels) {
        if (sceneId >= 16) {
            return ESP_ERR_INVALID_ARG;
        }
        std::lock_guard lock(m_mutex);
        ESP_LOGI(TAG, "Saving configuration for DALI Scene %d for %zu devices", sceneId, levels.size());
        auto& dali = DaliAPI::getInstance();

        for (const auto& [addr, level] : levels) {
            ESP_LOGD(TAG, "Setting device %d to level %d for scene %d", addr, level, sceneId);

            // 1. Устанавливаем DTR в нужное значение (уровень яркости)
            dali.sendCommand(DALI_ADDRESS_TYPE_SPECIAL_CMD, DALI_SPECIAL_COMMAND_DATA_TRANSFER_REGISTER, level);
            vTaskDelay(pdMS_TO_TICKS(5));

            // 2. Даем команду светильнику сохранить значение из DTR в ячейку памяти для сцены
            dali.sendCommand(DALI_ADDRESS_TYPE_SHORT, addr, DALI_COMMAND_STORE_DTR_AS_SCENE_0 + sceneId);
            vTaskDelay(pdMS_TO_TICKS(5));
        }

        ESP_LOGI(TAG, "Finished saving configuration for Scene %d", sceneId);
        return ESP_OK;
    }

} // namespace daliMQTT