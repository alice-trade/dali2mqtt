#ifndef PCH_HXX
#define PCH_HXX

// Standard C++ library headers
#include <string>
#include <string_view>
#include <vector>
#include <map>
#include <unordered_map>
#include <mutex>
#include <optional>
#include <bitset>
#include <format>
#include <algorithm>
#include <ranges>
#include <atomic>
#include <functional>
#include <cstring>
#include <charconv>
#include <array>

// ESP-IDF C headers
#ifdef __cplusplus
extern "C" {
#endif

#include <esp_log.h>
#include <esp_err.h>
#include <esp_event.h>
#include <nvs_flash.h>
#include <nvs.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <driver/gpio.h>
#include <cJSON.h>

#ifdef __cplusplus
}
#endif

#endif //PCH_HXX