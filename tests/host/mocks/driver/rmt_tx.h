//  Copyright (c) 2026 Alice-Trade Inc.
//  SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include <stdint.h>
#include <stddef.h>
#include "driver/gpio.h"
#include "esp_err.h"

#define RMT_CLK_SRC_DEFAULT 0

typedef void* rmt_channel_handle_t;
typedef void* rmt_encoder_handle_t;

typedef struct {
    uint32_t duration0 : 15;
    uint32_t level0    : 1;
    uint32_t duration1 : 15;
    uint32_t level1    : 1;
} rmt_symbol_word_t;

typedef struct {
    int loop_count;
} rmt_transmit_config_t;

typedef struct {
    gpio_num_t gpio_num;
    uint32_t clk_src;
    uint32_t resolution_hz;
    size_t mem_block_symbols;
    size_t trans_queue_depth;
    struct {
        uint32_t invert_out : 1;
        uint32_t with_dma   : 1;
    } flags;
} rmt_tx_channel_config_t;

typedef struct {} rmt_copy_encoder_config_t;

#ifdef __cplusplus
extern "C" {
#endif
esp_err_t rmt_new_tx_channel(const rmt_tx_channel_config_t* config, rmt_channel_handle_t* ret_chan);
esp_err_t rmt_new_copy_encoder(const rmt_copy_encoder_config_t* config, rmt_encoder_handle_t* ret_encoder);
esp_err_t rmt_del_channel(rmt_channel_handle_t channel);
esp_err_t rmt_del_encoder(rmt_encoder_handle_t encoder);
esp_err_t rmt_enable(rmt_channel_handle_t channel);
esp_err_t rmt_disable(rmt_channel_handle_t channel);
esp_err_t rmt_transmit(rmt_channel_handle_t channel, rmt_encoder_handle_t encoder, const void* payload, size_t payload_bytes, const rmt_transmit_config_t* config);
esp_err_t rmt_tx_wait_all_done(rmt_channel_handle_t channel, int timeout_ms);
#ifdef __cplusplus
}
#endif