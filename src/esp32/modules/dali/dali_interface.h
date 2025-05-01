#ifndef DALI_INTERFACE_H
#define DALI_INTERFACE_H

#include "esp_err.h"
#include "dali.h" // Включаем заголовок низкоуровневого драйвера

/**
 * @brief Инициализирует DALI интерфейс, создает задачу и таймер опроса.
 *
 * Создает мьютекс для защиты доступа к шине DALI.
 * Задача и таймер создаются, но таймер не запускается автоматически.
 *
 * @return esp_err_t Результат операции (ESP_OK при успехе).
 */
esp_err_t dali_interface_init(void);

/**
 * @brief Запускает периодический опрос DALI.
 *
 * Запускает таймер, который будет инициировать опрос.
 * Должен вызываться после dali_interface_init() и после подключения к MQTT.
 *
 * @return esp_err_t Результат запуска таймера (ESP_OK при успехе).
 */
esp_err_t dali_interface_start_polling(void);

/**
 * @brief Останавливает периодический опрос DALI.
 *
 * Останавливает и удаляет таймер и задачу опроса.
 *
 * @return esp_err_t Результат операции (ESP_OK при успехе).
 */
esp_err_t dali_interface_stop_polling(void);


// --- Остальные функции без изменений ---

/**
 * @brief Отправляет DALI команду.
 * Потокобезопасная обертка над dali_transaction.
 * ... (остальное описание)
 */
esp_err_t dali_interface_send_raw_command(dali_addressType_t address_type, uint8_t address,
                                          bool is_cmd, uint8_t command_or_data,
                                          bool send_twice, int *result);
/**
 * @brief Отправляет стандартную DALI команду (is_cmd = true).
 * ... (остальное описание)
 */
esp_err_t dali_interface_send_command(dali_addressType_t address_type, uint8_t address,
                                      uint8_t command, bool send_twice, int *result);
/**
 * @brief Отправляет команду Direct Arc Power (DAPC) для установки уровня яркости (is_cmd = false).
 * ... (остальное описание)
 */
esp_err_t dali_interface_send_dapc(dali_addressType_t address_type, uint8_t address,
                                   uint8_t level, int *result);
/**
 * @brief Запрашивает текущий уровень яркости устройства/группы.
 * ... (остальное описание)
 */
esp_err_t dali_interface_query_actual_level(dali_addressType_t address_type, uint8_t address, int *level);
/**
 * @brief Запрашивает статус устройства/группы и публикует его в MQTT, если он изменился.
 * ... (остальное описание)
 */
void dali_interface_query_and_publish_status(dali_addressType_t address_type, uint8_t address);
/**
 * @brief Запускает опрос всех настроенных устройств/групп и публикацию их статусов.
 * Вызывается задачей опроса.
 * ... (остальное описание)
 */
void dali_interface_poll_all(bool force_publish);
/**
 * @brief Публикует конфигурацию для Home Assistant MQTT Discovery.
 * ... (остальное описание)
 */
void dali_interface_publish_ha_discovery(void);
/**
 * @brief Управляет членством устройства DALI в группе.
 * ... (остальное описание)
 */
esp_err_t dali_interface_manage_group_membership(uint8_t device_short_address,
                                                 uint8_t group_number,
                                                 bool add_to_group);

#endif // DALI_INTERFACE_H