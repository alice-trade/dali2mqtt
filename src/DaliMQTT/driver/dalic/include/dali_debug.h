#ifndef DALI_DEBUG_H
#define DALI_DEBUG_H

#include <esp_err.h>
#include <driver/gpio.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initializes and starts the DALI GPIO debug poller.
 *
 * Creates a high-resolution timer to periodically poll the DALI RX and TX GPIOs.
 * Any change in the logic level of the pins is logged to the console.
 * This is useful for low-level debugging of the DALI bus.
 *
 * @param rx_pin GPIO number for the DALI RX pin.
 * @param tx_pin GPIO number for the DALI TX pin.
 * @return esp_err_t ESP_OK on success.
 */
esp_err_t dali_debug_init(gpio_num_t rx_pin, gpio_num_t tx_pin);

/**
 * @brief Stops and deinitializes the DALI GPIO debug poller.
 *
 * @return esp_err_t ESP_OK on success.
 */
esp_err_t dali_debug_stop(void);

#endif
