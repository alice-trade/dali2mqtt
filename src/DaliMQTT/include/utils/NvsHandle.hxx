// Copyright (c) 2026 Alice-Trade Inc.
// SPDX-License-Identifier: GPL-2.0-or-later

#ifndef DALIMQTT_NVSHANDLE_HXX
#define DALIMQTT_NVSHANDLE_HXX
#include <esp_log.h>
#include <nvs.h>

static constexpr char  TAG_NVS[] = "NVS_Handler";

// RAII wrapper for NVS handle
class NvsHandle {
public:
    NvsHandle(const char* ns, nvs_open_mode_t mode) {
        esp_err_t err = nvs_open(ns, mode, &m_handle);
        if (err != ESP_OK) {
            ESP_LOGE(TAG_NVS, "Error (%s) opening NVS handle!", esp_err_to_name(err));
            m_handle = 0;
        }
    }

    ~NvsHandle() {
        if (m_handle) {
            nvs_close(m_handle);
        }
    }

    [[nodiscard]] nvs_handle_t get() const { return m_handle; }
    explicit operator bool() const { return m_handle != 0; }

    NvsHandle(const NvsHandle&) = delete;
    NvsHandle& operator=(const NvsHandle&) = delete;

private:
    nvs_handle_t m_handle{0};
};


#endif //DALIMQTT_NVSHANDLE_HXX