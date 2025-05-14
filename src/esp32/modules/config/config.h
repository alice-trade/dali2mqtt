#ifndef CONFIG_H
#define CONFIG_H
#include "esp_err.h"
#include "app_config.h" // Включает app_config_t

/**
 * @brief Инициализирует NVS и загружает конфигурацию.
 *
 * Вызывается после nvs_flash_init().
 * Если конфигурация не найдена, сохраняет значения по умолчанию.
 * Заполняет глобальную структуру g_app_config.
 *
 * @return esp_err_t Результат операции (ESP_OK при успехе).
 */
esp_err_t config_manager_init(void);

/**
 * @brief Сохраняет текущую конфигурацию из g_app_config в NVS.
 *
 * @return esp_err_t Результат операции (ESP_OK при успехе).
 */
esp_err_t config_manager_save(void);

/**
 * @brief Устанавливает новый интервал опроса и сохраняет его.
 *
 * Также обновляет период активного таймера опроса.
 *
 * @param interval_ms Новый интервал в миллисекундах.
 * @return esp_err_t Результат операции (ESP_OK при успехе).
 */
esp_err_t config_manager_set_poll_interval(uint32_t interval_ms);

#endif //CONFIG_H
