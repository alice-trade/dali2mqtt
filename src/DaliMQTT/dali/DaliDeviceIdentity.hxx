// Copyright (c) 2026 Alice-Trade Inc.
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef DALIMQTT_DALIDEVICEIDENTITY_HXX
#define DALIMQTT_DALIDEVICEIDENTITY_HXX

#include "dali/DaliAddress.hxx"
#include <etl/string.h>

namespace daliMQTT {

struct DeviceIdentity {
    DaliLongAddress_t longAddress{InvalidLongAddr};
    DaliInternalAddr internalAddress{};
    etl::string<16> gtin{};
    bool available{false};

    [[nodiscard]] constexpr bool isAssigned() const noexcept { return internalAddress.isAssigned(); }
};

} // namespace daliMQTT

#endif // DALIMQTT_DALIDEVICEIDENTITY_HXX