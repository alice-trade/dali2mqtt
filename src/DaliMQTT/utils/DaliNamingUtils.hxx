// Copyright (c) 2026 Alice-Trade Inc.
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef DALIMQTT_DALINAMINGUTILS_HXX
#define DALIMQTT_DALINAMINGUTILS_HXX

#include "utils/NvsHandle.hxx"
#include <ArduinoJson.h>
#include <string>

namespace daliMQTT::utils {

inline std::string getDeviceCustomName(const char* longAddrStr) {
    NvsHandle nvs("dali_names", NVS_READONLY);
    if (!nvs) return "";

    size_t reqSize = 0;
    if (nvs_get_str(nvs.get(), "names_json", nullptr, &reqSize) != ESP_OK || reqSize <= 2) {
        return "";
    }

    auto buf = std::make_unique<char[]>(reqSize);
    if (nvs_get_str(nvs.get(), "names_json", buf.get(), &reqSize) != ESP_OK) {
        return "";
    }

    JsonDocument doc;
    if (deserializeJson(doc, buf.get()) == DeserializationError::Ok) {
        if (doc[longAddrStr].is<const char*>()) {
            return doc[longAddrStr].as<const char*>();
        }
    }
    return "";
}

} // namespace daliMQTT::utils

#endif // DALIMQTT_DALINAMINGUTILS_HXX