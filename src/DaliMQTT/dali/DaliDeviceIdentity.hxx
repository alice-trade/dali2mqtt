// Copyright (c) 2026 Alice-Trade Inc.
// SPDX-License-Identifier: GPL-2.0-or-later

#ifndef DALIMQTT_DALIDEVICEIDENTITY_HXX
#define DALIMQTT_DALIDEVICEIDENTITY_HXX

#include <dali/DaliInternalAddr.hxx>

namespace daliMQTT {
    using DaliLongAddress_t = uint32_t;

    struct DeviceIdentity {
        DaliLongAddress_t long_address{0};          // 24-bit DALI Long (random) Address
        DaliInternalAddr internal_address{};        // Short addr
        std::string gtin;                           // GTIN
        bool available{false};                      // Runtime Availability flag

        [[nodiscard]] bool is_assigned() const { return (internal_address).shortAddr() < 64; }
    };
}
#endif //DALIMQTT_DALIDEVICEIDENTITY_HXX