// Copyright (c) 2026 Alice-Trade Inc.
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef PCH_HXX
#define PCH_HXX

// Standard C++ library headers
#include <algorithm>
#include <array>
#include <atomic>
#include <bitset>
#include <charconv>
#include <memory>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <functional>
#include <mutex>
#include <optional>
#include <etl/variant.h>
#include <etl/queue.h>
#include <etl/flat_set.h>
#include <etl/flat_map.h>
#include <etl/vector.h>
#include <etl/string.h>
#include <ranges>
#include <string>
#include <string_view>
#include <vector>
// C headers
#ifdef __cplusplus
extern "C" {
#endif

#include <esp_log.h>
#include <esp_err.h>
#include <esp_event.h>
#include <nvs_flash.h>
#include <driver/rmt_tx.h>
#include <driver/rmt_rx.h>
#include <nvs.h>
#include <esp_check.h>
#include <esp_timer.h>
#include <esp_random.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/ringbuf.h>
#include <freertos/queue.h>
#include <freertos/timers.h>
#include <driver/gpio.h>

#ifdef __cplusplus
}
#endif

// ArduinoJson
#include <ArduinoJson.h>

#endif //PCH_HXX