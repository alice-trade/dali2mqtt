//  Copyright (c) 2026 Alice-Trade Inc.
//  SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "esp_err.h"
#define ESP_MAC_WIFI_STA 0
#ifdef __cplusplus
extern "C" {
#endif
esp_err_t esp_read_mac(uint8_t* mac, int type);
#ifdef __cplusplus
}
#endif