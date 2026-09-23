// Copyright (c) 2026 Alice-Trade Inc.
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef DALIMQTT_CONFIGJSON_HXX
#define DALIMQTT_CONFIGJSON_HXX

#include "system/ConfigStructure.hxx"
#include <ArduinoJson.h>

namespace daliMQTT {

struct ConfigApplyResult {
    bool success{true};
    bool requiresReboot{false};
    bool hassDiscoveryToggled{false};
    const char* errorMessage{""};
};

class ConfigJson {
public:
    /**
     * @brief Serialization of configuration in JSON
     */
    static void serialize(const ConfigStructure& cfg, JsonDocument& doc, bool maskSecrets = true);

    /**
     * @brief Deserialization and applying JSON to a structure
     */
    static ConfigApplyResult apply(const JsonDocument& doc, ConfigStructure& target);
};

} // namespace daliMQTT

#endif // DALIMQTT_CONFIGJSON_HXX