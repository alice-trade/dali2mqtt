#include <freertos/FreeRTOS.h>
#include <driver/gpio.h>
#include <esp_log.h>
#include <esp_timer.h>
#include "include/dali_debug.h"

static const char *TAG = "DALI_GPIO_DEBUG";

static esp_timer_handle_t gpio_poll_timer;
static gpio_num_t dali_rx_pin_debug;
static gpio_num_t dali_tx_pin_debug;
static int prev_rx_level = -1;
static int prev_tx_level = -1;

static void gpio_poll_callback(void *arg) {
    int current_rx_level = gpio_get_level(dali_rx_pin_debug);
    int current_tx_level = gpio_get_level(dali_tx_pin_debug);

    if (current_rx_level != prev_rx_level) {
        ESP_LOGI(TAG, "DALI_RX (GPIO %d) changed from %d to %d", dali_rx_pin_debug, prev_rx_level, current_rx_level);
        prev_rx_level = current_rx_level;
    }

    if (current_tx_level != prev_tx_level) {
        ESP_LOGI(TAG, "DALI_TX (GPIO %d) changed from %d to %d", dali_tx_pin_debug, prev_tx_level, current_tx_level);
        prev_tx_level = current_tx_level;
    }
}

esp_err_t dali_debug_init(gpio_num_t rx_pin, gpio_num_t tx_pin) {
    dali_rx_pin_debug = rx_pin;
    dali_tx_pin_debug = tx_pin;

    // Initialize previous levels
    prev_rx_level = gpio_get_level(dali_rx_pin_debug);
    prev_tx_level = gpio_get_level(dali_tx_pin_debug);

    const esp_timer_create_args_t timer_args = {
        .callback = &gpio_poll_callback,
        .name = "dali_gpio_poll"
    };

    esp_err_t err = esp_timer_create(&timer_args, &gpio_poll_timer);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create debug timer: %s", esp_err_to_name(err));
        return err;
    }

    // Poll every 100 microseconds, good for observing DALI's ~417us half-bit time
    err = esp_timer_start_periodic(gpio_poll_timer, 100);
     if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start debug timer: %s", esp_err_to_name(err));
        esp_timer_delete(gpio_poll_timer);
        gpio_poll_timer = NULL;
        return err;
    }
    ESP_LOGI(TAG, "DALI GPIO debug polling started on RX:%d, TX:%d at 10kHz", rx_pin, tx_pin);
    return ESP_OK;
}

esp_err_t dali_debug_stop(void) {
    if (gpio_poll_timer) {
        esp_timer_stop(gpio_poll_timer);
        esp_timer_delete(gpio_poll_timer);
        gpio_poll_timer = NULL;
        ESP_LOGI(TAG, "DALI GPIO debug polling stopped.");
    }
    return ESP_OK;
}
