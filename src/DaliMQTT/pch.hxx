#ifndef PCH_HXX
#define PCH_HXX

// Standard C++ library headers
#include <algorithm>
#include <array>
#include <set>
#include <atomic>
#include <bitset>
#include <charconv>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <variant>
#include <functional>
#include <map>
#include <mutex>
#include <optional>
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
#include <cJSON.h>

#ifdef __cplusplus
}
#endif

#endif //PCH_HXX