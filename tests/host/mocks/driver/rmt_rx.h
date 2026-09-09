//  Copyright (c) 2026 Alice-Trade Inc.
//  SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "driver/rmt_tx.h"

typedef struct {
    size_t num_symbols;
    rmt_symbol_word_t* received_symbols;
} rmt_rx_done_event_data_t;

typedef struct {
    uint32_t signal_range_min_ns;
    uint32_t signal_range_max_ns;
} rmt_receive_config_t;

typedef struct {
    gpio_num_t gpio_num;
    uint32_t clk_src;
    uint32_t resolution_hz;
    size_t mem_block_symbols;
    struct {
        uint32_t invert_in : 1;
        uint32_t with_dma  : 1;
    } flags;
} rmt_rx_channel_config_t;

typedef struct {
    bool (*on_recv_done)(rmt_channel_handle_t, const rmt_rx_done_event_data_t*, void*);
} rmt_rx_event_callbacks_t;

#ifdef __cplusplus
extern "C" {
#endif
esp_err_t rmt_new_rx_channel(const rmt_rx_channel_config_t* config, rmt_channel_handle_t* ret_chan);
esp_err_t rmt_rx_register_event_callbacks(rmt_channel_handle_t channel, const rmt_rx_event_callbacks_t* cbs, void* user_data);
esp_err_t rmt_receive(rmt_channel_handle_t channel, void* buffer, size_t buffer_size, const rmt_receive_config_t* config);
#ifdef __cplusplus
}
#endif