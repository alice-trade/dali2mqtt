//
// Created by Danil on 14.05.2025.
//

#ifndef INIT_STEP_HANDLER_H
#define INIT_STEP_HANDLER_H
#include "esp_err.h"
#include "esp_log.h"
/**
 * @brief Проверяет результат операции и логирует ошибку или успех.
 *
 * @param err Код ошибки esp_err_t.
 * @param step_name Описание шага/операции.
 * @return esp_err_t Возвращает переданный код ошибки.
 */
[[nodiscard]] static  esp_err_t app_check_init_step(esp_err_t err, const char *step_name) {
    if (err != ESP_OK) {
        ESP_LOGE("INIT_CHECK", "Failed to initialize %s: %s (0x%x)", step_name, esp_err_to_name(err), err);
    } else {
        ESP_LOGI("INIT_CHECK", "%s initialized successfully.", step_name);
    }
    return err;
}
#endif //INIT_STEP_HANDLER_H
