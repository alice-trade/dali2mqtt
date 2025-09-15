#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <driver/rmt_tx.h>
#include <driver/rmt_rx.h>
#include <esp_log.h>
#include "dali_defs.h"
static const char *TAG = "DALISniffer";

static TaskHandle_t dali_sniffer_task_handle = nullptr;
static QueueHandle_t dali_output_queue = nullptr;
volatile bool is_sniffer_running = false;


static void dali_sniffer_task([[maybe_unused]] void* pvParameters) {
    rmt_rx_done_event_data_t rx_data;
    size_t received_symbols_size = sizeof(rmt_symbol_word_t) * 64;
    rmt_symbol_word_t *raw_symbols = malloc(received_symbols_size);
    if (!raw_symbols) {
        ESP_LOGE(TAG, "Failed to allocate memory for RMT symbols");
        is_sniffer_running = false;
        vTaskDelete(nullptr);
        return;
    }

    ESP_LOGI(TAG, "DALI sniffer task started.");

    ESP_ERROR_CHECK(rmt_enable(dali_rxChannel));

    while (is_sniffer_running) {
        esp_err_t status = rmt_receive(dali_rxChannel, raw_symbols, received_symbols_size, &dali_rxChannelConfig);

        if (status == ESP_OK) {
            dali_receivePrevBit_t receive_prev_bit = DALI_RECEIVE_PREV_BIT_ONE;
            uint16_t frame_data_16bit = 0;
            uint8_t bit_count = 0;

            uint8_t* frame_ptr = (uint8_t*)&frame_data_16bit;

            dali_rmt_rx_decoder(&receive_prev_bit, frame_ptr, &bit_count, rx_data.received_symbols[0].duration1, rx_data.received_symbols[0].level1);
            for (size_t i = 1; i < rx_data.num_symbols; i++) {
                if (bit_count >= 17) break;
                dali_rmt_rx_decoder(&receive_prev_bit, frame_ptr, &bit_count, rx_data.received_symbols[i].duration0, rx_data.received_symbols[i].level0);
                if (bit_count >= 17) break;
                dali_rmt_rx_decoder(&receive_prev_bit, frame_ptr, &bit_count, rx_data.received_symbols[i].duration1, rx_data.received_symbols[i].level1);
            }

            if (bit_count == 8 || bit_count == 16) {
                dali_frame_t frame = {
                    .data = frame_data_16bit,
                    .is_backward_frame = (bit_count == 8)
                };
                if (dali_output_queue) {
                    xQueueSend(dali_output_queue, &frame, 0);
                }
            }
        } else if (status == ESP_ERR_TIMEOUT) {
            continue;
        } else {
            ESP_LOGW(TAG, "RMT receive error: %s", esp_err_to_name(status));
        }
    }

    free(raw_symbols);
    ESP_LOGI(TAG, "DALI sniffer task stopped.");
    dali_sniffer_task_handle = nullptr;
    vTaskDelete(nullptr);
}


esp_err_t dali_sniffer_start(QueueHandle_t output_queue) {
    if (is_sniffer_running) {
        ESP_LOGW(TAG, "Sniffer is already running.");
        return ESP_ERR_INVALID_STATE;
    }
    if (!output_queue) {
        return ESP_ERR_INVALID_ARG;
    }

    dali_output_queue = output_queue;
    is_sniffer_running = true;

    if (xTaskCreate(dali_sniffer_task, "dali_sniffer", 4096, NULL, 10, &dali_sniffer_task_handle) != pdPASS) {
        ESP_LOGE(TAG, "Failed to create sniffer task.");
        is_sniffer_running = false;
        return ESP_FAIL;
    }

    return ESP_OK;
}

esp_err_t dali_sniffer_stop(void) {
    if (!is_sniffer_running) {
        return ESP_OK;
    }
    is_sniffer_running = false;

    vTaskDelay(pdMS_TO_TICKS(50));

    if (dali_sniffer_task_handle) {
        vTaskDelete(dali_sniffer_task_handle);
        dali_sniffer_task_handle = nullptr;
    }

    return rmt_disable(dali_rxChannel);
}