// Copyright (c) 2026 Alice-Trade Inc.
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef DALIMQTT_MQTTMESSAGE_HXX
#define DALIMQTT_MQTTMESSAGE_HXX

#include <cstdint>
#include <etl/string.h>

namespace daliMQTT {

struct MqttIncomingMessage {
    etl::string<96> topic{};
    etl::string<384> payload{};
};

} // namespace daliMQTT

#endif // DALIMQTT_MQTTMESSAGE_HXX