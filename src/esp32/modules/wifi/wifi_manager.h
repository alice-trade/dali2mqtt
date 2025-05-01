#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include "esp_err.h"

/**
 * @brief Инициализирует WiFi и стек TCP/IP.
 *
 * @return esp_err_t Результат операции (ESP_OK при успехе).
 */
esp_err_t wifi_manager_init(void);

/**
 * @brief Запускает процесс подключения к WiFi сети.
 *
 * Использует SSID и пароль из g_app_config.
 * Блокирует выполнение до подключения или ошибки.
 *
 * @return esp_err_t Результат операции (ESP_OK при успешном подключении).
 */
esp_err_t wifi_manager_connect(void);

/**
 * @brief Останавливает WiFi и освобождает ресурсы.
 *
 * @return esp_err_t Результат операции (ESP_OK при успехе).
 */
esp_err_t wifi_manager_disconnect(void);


#endif // WIFI_MANAGER_H