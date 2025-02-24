//
// Created by danil on 23.02.2025.
//

#ifndef LIGHTNINGCONFIG_HPP
#define LIGHTNINGCONFIG_HPP

#include <nvs_flash.h>
#include <string>

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
     * @return int The DALI RX GPIO pin number.
     */
    int getDaliRxGpio();

    /**
     * @brief Gets the DALI TX GPIO pin number from configuration.
     *
     * @return int The DALI TX GPIO pin number.
     */
    int getDaliTxGpio();

    // Add other configuration getters as needed (e.g., Ethernet settings, etc.)

} // namespace ConfigManager

#endif //LIGHTNINGCONFIG_HPP
