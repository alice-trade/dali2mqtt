//
// Created by danil on 23.02.2025.
//

#ifndef LIGHTNINGCONFIG_HPP
#define LIGHTNINGCONFIG_HPP

#include <string>
#include <esp_err.h>

namespace ConfigManager {

/**
 * @brief Initializes the configuration manager. Loads configuration from NVS or file.
 *
 * @return esp_err_t ESP_OK on success, error code otherwise.
 */
esp_err_t init();

/**
 * @brief Gets the MQTT broker URL from configuration.
 *
 * @return std::string The MQTT broker URL.
 */
std::string getMqttBrokerUrl();

/**
 * @brief Gets the MQTT client ID from configuration.
 *
 * @return std::string The MQTT client ID.
 */
std::string getMqttClientId();

/**
 * @brief Gets the DALI RX GPIO pin number from configuration.
 *
 * @return int The DALI RX GPIO pin number, -1 if not configured.
 */
int getDaliRxGpio();

/**
 * @brief Gets the DALI TX GPIO pin number from configuration.
 *
 * @return int The DALI TX GPIO pin number, -1 if not configured.
 */
int getDaliTxGpio();

/**
 * @brief Gets the status poll interval in seconds from configuration.
 *
 * @return int The status poll interval in seconds, -1 if not configured.
 */
int getStatusPollIntervalSec();

/**
 * @brief Gets the MQTT command topic base from configuration.
 *
 * @return std::string The MQTT command topic base.
 */
std::string getMqttCommandTopicBase();

/**
 * @brief Gets the MQTT status topic base from configuration.
 *
 * @return std::string The MQTT status topic base.
 */
std::string getMqttStatusTopicBase();


} // namespace ConfigManager

#endif //LIGHTNINGCONFIG_HPP
