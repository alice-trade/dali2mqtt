//  Copyright (c) 2026 Alice-Trade Inc.
//  SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif
inline esp_err_t esp_crt_bundle_attach(void* conf) { (void)conf; return ESP_OK; }
#ifdef __cplusplus
}
#endif