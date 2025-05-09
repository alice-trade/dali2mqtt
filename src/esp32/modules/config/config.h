#ifndef CONFIG_H
#define CONFIG_H
#include "esp_err.h"
#include "definitions.h" // Включает app_config_t

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

/**
 * @brief Устанавливает новую маску групп для опроса и сохраняет ее.
 *
 * @param groups_mask Битовая маска групп (0-15).
 * @return esp_err_t Результат операции (ESP_OK при успехе).
 */
esp_err_t config_manager_set_poll_groups_mask(uint16_t groups_mask);

/**
 * @brief Устанавливает новую маску устройств для опроса и сохраняет ее.
 *
 * @param devices_mask Битовая маска устройств (0-63).
 * @return esp_err_t Результат операции (ESP_OK при успехе).
 */
esp_err_t config_manager_set_poll_devices_mask(uint64_t devices_mask);

#endif //CONFIG_H
