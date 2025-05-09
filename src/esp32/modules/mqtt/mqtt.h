#ifndef MQTT_H
#define MQTT_H
#include "esp_err.h"

/**
 * @brief Инициализирует MQTT клиент.
 *
 * Использует URI из g_app_config.
 *
 * @return esp_err_t Результат операции (ESP_OK при успехе).
 */
esp_err_t mqtt_manager_init(void);

/**
 * @brief Запускает MQTT клиент и пытается подключиться к брокеру.
 *
 * Должен вызываться после успешного подключения к WiFi.
 * Подключение происходит асинхронно.
 *
 * @return esp_err_t Результат операции (ESP_OK при успехе запуска).
 */
esp_err_t mqtt_manager_start(void);

/**
 * @brief Останавливает и уничтожает MQTT клиент.
 *
 * @return esp_err_t Результат операции (ESP_OK при успехе).
 */
esp_err_t mqtt_manager_stop(void);

/**
 * @brief Публикует сообщение в указанный топик.
 *
 * @param topic Топик для публикации.
 * @param data Данные для публикации (строка).
 * @param retain Флаг retain (0 или 1).
 * @return int ID сообщения при успешной постановке в очередь, -1 при ошибке или если клиент не подключен.
 */
int mqtt_manager_publish(const char *topic, const char *data, int retain);

/**
 * @brief Публикует текущую конфигурацию устройства в MQTT.
 *
 * Отправляет JSON с текущими настройками (без пароля WiFi)
 * в топик MQTT_CONFIG_GET_RESPONSE_TOPIC.
 */
void mqtt_publish_config(void);
#endif //MQTT_H
