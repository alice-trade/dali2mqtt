#ifndef DALI_INTERNAL_H
#define DALI_INTERNAL_H

#include <driver/rmt_tx.h>
#include <freertos/semphr.h>
#include <driver/rmt_rx.h>
#include "dali.h"

typedef enum {
    DALI_RECEIVE_PREV_BIT_ZERO,
    DALI_RECEIVE_PREV_BIT_ONE
} dali_receivePrevBit_t;

extern SemaphoreHandle_t bus_mutex;
extern rmt_channel_handle_t dali_rxChannel;
extern rmt_receive_config_t dali_rxChannelConfig;
extern QueueHandle_t dali_rxChannelQueue;
extern rmt_channel_handle_t dali_txChannel;
extern rmt_tx_channel_config_t dali_txChannelConfig;
extern rmt_encoder_handle_t dali_txChannelEncoder;

esp_err_t dali_rmt_rx_decoder(dali_receivePrevBit_t* receive_prev_bit, uint16_t* frame, uint8_t* bit_index, uint16_t duration, uint16_t level);

#endif // DALI_INTERNAL_H