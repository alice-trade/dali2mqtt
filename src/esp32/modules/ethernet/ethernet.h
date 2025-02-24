//
// Created by danil on 23.02.2025.
//

#ifndef ETHERNET_H
#define ETHERNET_H

#include <esp_err.h>

#ifdef __cplusplus
extern "C" {
#endif
    /**
     * @brief Initializes the Ethernet interface.
     *        Configures the Ethernet MAC and PHY, starts the network interface,
     *        and obtains an IP address (using DHCP or static configuration).
     * @return esp_err_t ESP_OK on success, error code otherwise.
     */
    esp_err_t eth_init();

    /**
     * @brief Starts the Ethernet interface.
     *
     * @return esp_err_t  ESP_OK on success, error code otherwise.
     */
    esp_err_t eth_start();

    /**
     * @brief Stops the Ethernet interface.
     *
     * @return esp_err_t ESP_OK on success, error code otherwise.
     */
    esp_err_t eth_stop();

#ifdef __cplusplus
}
#endif
#endif //ETHERNET_H
