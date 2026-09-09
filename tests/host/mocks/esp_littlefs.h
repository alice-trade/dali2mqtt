//  Copyright (c) 2026 Alice-Trade Inc.
//  SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "esp_err.h"
typedef struct {
    const char* base_path;
    const char* partition_label;
    bool format_if_mount_failed;
    bool dont_mount;
} esp_vfs_littlefs_conf_t;

#ifdef __cplusplus
extern "C" {
#endif
inline esp_err_t esp_vfs_littlefs_register(const esp_vfs_littlefs_conf_t* conf) { (void)conf; return ESP_OK; }
#ifdef __cplusplus
}
#endif