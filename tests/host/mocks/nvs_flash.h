//  Copyright (c) 2026 Alice-Trade Inc.
//  SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "esp_err.h"

#define ESP_ERR_NVS_NO_FREE_PAGES 0x1100
#define ESP_ERR_NVS_NEW_VERSION_FOUND 0x1101

#ifdef __cplusplus
extern "C" {
#endif
esp_err_t nvs_flash_init(void);
esp_err_t nvs_flash_erase(void);
#ifdef __cplusplus
}
#endif