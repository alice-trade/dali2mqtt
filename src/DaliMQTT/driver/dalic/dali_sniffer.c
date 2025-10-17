#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <driver/rmt_tx.h>
#include <driver/rmt_rx.h>
#include <esp_log.h>
#include "dali_priv_defs.h"
static const char *TAG = "DALISniffer";

static TaskHandle_t dali_sniffer_task_handle = nullptr;
static QueueHandle_t dali_output_queue = nullptr;
volatile bool is_sniffer_running = false;


static void dali_sniffer_task([[maybe_unused]] void* pvParameters) {
    rmt_rx_done_event_data_t rx_data;
    
    // The RMT receiver needs a buffer to store symbols. The driver manages this internally,
    // but we need to provide one for the initial rmt_receive() call to "arm" the receiver.
    // The actual received symbols will be pointed to by rx_data.received_symbols.
    size_t buffer_size = sizeof(rmt_symbol_word_t) * 64;
    rmt_symbol_word_t* rmt_buffer = malloc(buffer_size);
    if (!rmt_buffer) {
        ESP_LOGE(TAG, "Failed to allocate memory for RMT buffer");
        is_sniffer_running = false;
        return;
    }

    ESP_LOGI(TAG, "DALI sniffer task started.");

    ESP_ERROR_CHECK(rmt_enable(dali_rxChannel));
    ESP_ERROR_CHECK(rmt_receive(dali_rxChannel, rmt_buffer, buffer_size, &dali_rxChannelConfig));

    while (is_sniffer_running) {
        if (xQueueReceive(dali_rxChannelQueue, &rx_data, pdMS_TO_TICKS(100)) == pdPASS) {
            dali_receivePrevBit_t receive_prev_bit = DALI_RECEIVE_PREV_BIT_ONE; // Start bit is always a '1'
            uint16_t frame_data_16bit = 0;
            uint8_t bit_count = 0;

            for (size_t i = 0; i < rx_data.num_symbols; i++) {
                if (bit_count >= 17) break;
                dali_rmt_rx_decoder(&receive_prev_bit, &frame_data_16bit, &bit_count, rx_data.received_symbols[i].duration0, rx_data.received_symbols[i].level0);
                if (bit_count >= 17) break;
                dali_rmt_rx_decoder(&receive_prev_bit, &frame_data_16bit, &bit_count, rx_data.received_symbols[i].duration1, rx_data.received_symbols[i].level1);
            }

            if (bit_count == 8 || bit_count == 16) {
                dali_frame_t frame = {
                    .data = frame_data_16bit,
                    .is_backward_frame = (bit_count == 8)
                };
                ESP_LOGD(TAG, "Decoded %s frame, bits: %d, data: 0x%04X",
                         frame.is_backward_frame ? "Backward" : "Forward", bit_count, frame.data);
                if (dali_output_queue) {
                    xQueueSend(dali_output_queue, &frame, 0);
                }
            }

            ESP_ERROR_CHECK(rmt_receive(dali_rxChannel, rmt_buffer, buffer_size, &dali_rxChannelConfig));
        }
    }

    free(rmt_buffer);
    ESP_LOGI(TAG, "DALI sniffer task stopped.");
    dali_sniffer_task_handle = nullptr;
    vTaskDelete(NULL);
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