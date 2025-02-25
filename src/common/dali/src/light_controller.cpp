//
// Created by danil on 25.02.2025.
//

#include "light_controller.hpp"
#include "dali_protocol.hpp"
#include <esp_log.h>

static const char *TAG = "LightingController";

namespace LightingController {


    bool queryBrightness(uint8_t address, bool is_group, int* brightness) {
        int query_result = DaliProtocol::queryBrightness(address, is_group, brightness);
        if (query_result == 0) {
            return true;
        } else {
            ESP_LOGE(TAG, "Failed to query brightness for DALI address %d, error code: %d", address, query_result);
            return false;
        }
    }

} // namespace LightingController