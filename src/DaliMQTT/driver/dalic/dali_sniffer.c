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

/**
 * @brief Decodes a raw RMT symbol buffer into a DALI frame.
 *
 * This function iterates through RMT symbols, which represent level durations on the bus,
 * and applies the DALI (Manchester) decoding logic to extract the data bits.
 * It can handle both 16-bit forward frames and 8-bit backward frames.
 *
 * @param rx_data Pointer to the RMT receive done event data.
 * @param out_frame Pointer to a dali_frame_t struct to store the result.
 * @return True if a valid frame was decoded, false otherwise.
 */
static bool decode_dali_frame(const rmt_rx_done_event_data_t* rx_data, dali_frame_t* out_frame) {
    if (rx_data->num_symbols == 0) {
        return false;
    }

    dali_receivePrevBit_t receive_prev_bit = DALI_RECEIVE_PREV_BIT_ONE; // Start bit is a '1'
    uint16_t frame_data_16bit = 0;
    uint8_t bit_count = 0; // Number of data bits decoded (excludes start bit)

    for (size_t i = 0; i < rx_data->num_symbols; i++) {
        if (bit_count >= 16) break; // Decoded a full forward frame
        dali_rmt_rx_decoder(&receive_prev_bit, &frame_data_16bit, &bit_count, rx_data->received_symbols[i].duration0, rx_data->received_symbols[i].level0);
        if (bit_count >= 16) break;
        dali_rmt_rx_decoder(&receive_prev_bit, &frame_data_16bit, &bit_count, rx_data->received_symbols[i].duration1, rx_data->received_symbols[i].level1);
    }

    if (bit_count == 8 || bit_count == 16) {
        out_frame->data = frame_data_16bit;
        out_frame->is_backward_frame = (bit_count == 8);
        return true;
    }

    return false; // Incomplete or invalid frame
}

static void dali_sniffer_task([[maybe_unused]] void* pvParameters) {
    rmt_rx_done_event_data_t rx_data;

    size_t buffer_size = sizeof(rmt_symbol_word_t) * 64;
    rmt_symbol_word_t* rmt_buffer = malloc(buffer_size);
    if (!rmt_buffer) {
        ESP_LOGE(TAG, "Failed to allocate memory for RMT buffer");
        is_sniffer_running = false;
        dali_sniffer_task_handle = nullptr;
        vTaskDelete(nullptr);
        return;
    }

    // Initial arming of the RMT receiver.
    // It will be re-armed inside the loop after processing each frame.
    xSemaphoreTake(bus_mutex, portMAX_DELAY);
    esp_err_t err = rmt_enable(dali_rxChannel);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "DALI sniffer task started and RMT receiver enabled.");
        err = rmt_receive(dali_rxChannel, rmt_buffer, buffer_size, &dali_rxChannelConfig);
    }
    xSemaphoreGive(bus_mutex);
    ESP_ERROR_CHECK(err);

    while (is_sniffer_running) {
        // Wait for RMT receive done callback to send data to the queue
        if (xQueueReceive(dali_rxChannelQueue, &rx_data, pdMS_TO_TICKS(500)) == pdPASS) {
            dali_frame_t frame;
            ESP_LOGW(TAG, "RMT ISR received %d symbols. Raw dump:", rx_data.num_symbols);
            for (size_t i = 0; i < rx_data.num_symbols; i++) {
                ESP_LOGW(TAG, "  Symbol %2d: {L0:%d, D0:%5d}, {L1:%d, D1:%5d}",
                    i,
                    rx_data.received_symbols[i].level0,
                    rx_data.received_symbols[i].duration0,
                    rx_data.received_symbols[i].level1,
                    rx_data.received_symbols[i].duration1
                );
            }
            if (decode_dali_frame(&rx_data, &frame) && dali_output_queue) {
                 if (frame.is_backward_frame) {
                    ESP_LOGI(TAG, "Backward frame sniffed: 0x%02X", frame.data & 0xFF);
                 } else {
                    ESP_LOGI(TAG, "Forward frame sniffed: 0x%04X", frame.data);
                 }
                xQueueSend(dali_output_queue, &frame, 0);
            }

            // Re-arm the RMT receiver to catch the next frame
            // A lock is needed because dali_transaction might temporarily disable the channel
            if (is_sniffer_running) {
                xSemaphoreTake(bus_mutex, portMAX_DELAY);
                esp_err_t recv_err = rmt_receive(dali_rxChannel, rmt_buffer, buffer_size, &dali_rxChannelConfig);
                xSemaphoreGive(bus_mutex);
                if (recv_err != ESP_OK) {
                    // This can happen if the channel was disabled by another task just after we checked is_sniffer_running
                    ESP_LOGD(TAG, "Failed to re-arm RMT receiver: %s", esp_err_to_name(recv_err));
                    vTaskDelay(pdMS_TO_TICKS(10)); // Avoid busy-looping
                }
            }
        }
    }

    free(rmt_buffer);
    ESP_LOGI(TAG, "DALI sniffer task stopped.");
    dali_sniffer_task_handle = nullptr;
    vTaskDelete(nullptr);
}


esp_err_t dali_sniffer_start(QueueHandle_t output_queue) {
    if (xSemaphoreTake(bus_mutex, portMAX_DELAY) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    if (is_sniffer_running) {
        ESP_LOGW(TAG, "Sniffer is already running.");
        xSemaphoreGive(bus_mutex);
        return ESP_ERR_INVALID_STATE;
    }
    if (!output_queue) {
        xSemaphoreGive(bus_mutex);
        return ESP_ERR_INVALID_ARG;
    }

    dali_output_queue = output_queue;
    if (xTaskCreate(dali_sniffer_task, "dali_sniffer", 4096, nullptr, 10, &dali_sniffer_task_handle) != pdPASS) {
        ESP_LOGE(TAG, "Failed to create sniffer task.");
        xSemaphoreGive(bus_mutex);
        return ESP_FAIL;
    }

    is_sniffer_running = true;
    xSemaphoreGive(bus_mutex);
    return ESP_OK;
}

esp_err_t dali_sniffer_stop(void) {
    if (!is_sniffer_running) {
        return ESP_OK;
    }

    is_sniffer_running = false;
    // Wait for the task to terminate. It will see the flag and exit its loop.
    while (dali_sniffer_task_handle != nullptr) {
        vTaskDelay(pdMS_TO_TICKS(20));
    }

    // Now that the task is gone, we can safely disable the RMT channel
    xSemaphoreTake(bus_mutex, portMAX_DELAY);
    const esp_err_t err = rmt_disable(dali_rxChannel);
    xSemaphoreGive(bus_mutex);

    ESP_LOGI(TAG, "DALI sniffer stopped and RMT receiver disabled.");
    return err;
}