//  Copyright (c) 2026 Alice-Trade Inc.
//  SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "esp_err.h"

#define ESP_RETURN_ON_ERROR(x, tag, msg) do { esp_err_t __err = (x); if (__err != ESP_OK) return __err; } while(0)
#define ESP_ERROR_CHECK(x) do { (void)(x); } while(0)