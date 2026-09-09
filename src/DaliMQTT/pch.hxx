// Copyright (c) 2026 Alice-Trade Inc.
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef PCH_HXX
#define PCH_HXX
#pragma GCC system_header

#include <algorithm>
#include <array>
#include <atomic>
#include <bitset>
#include <charconv>
#include <cstdint>
#include <cstring>
#include <memory>
#include <mutex>
#include <optional>
#include <string_view>

#include <ArduinoJson.h>
#include <etl/string.h>
#include <etl/vector.h>
#include <etl/flat_map.h>
#include <etl/flat_set.h>
#include <etl/queue.h>
#include <etl/variant.h>

extern "C" {
    #include <freertos/FreeRTOS.h>
    #include <freertos/task.h>
    #include <freertos/queue.h>
    #include <freertos/semphr.h>
    #include <freertos/timers.h>
    #include <esp_log.h>
    #include <esp_err.h>
    #include <esp_event.h>
    #include <esp_check.h>
    #include <esp_timer.h>
    #include <nvs.h>
    #include <nvs_flash.h>
}

#endif //PCH_HXX