// Copyright (c) 2026 Alice-Trade Inc.
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef DALIMQTT_DALIDEVICE_HXX
#define DALIMQTT_DALIDEVICE_HXX

#include "dali/DaliControlGear.hxx"
#include "dali/DaliInputDevice.hxx"
#include <etl/variant.h>

namespace daliMQTT {

using DaliDevice = etl::variant<ControlGear, InputDevice>;

inline const DeviceIdentity& getIdentity(const DaliDevice& dev) noexcept {
    if (const auto* gear = etl::get_if<ControlGear>(&dev))
        return *gear;
    return *etl::get_if<InputDevice>(&dev);
}

inline DeviceIdentity& getIdentity(DaliDevice& dev) noexcept {
    if (auto* gear = etl::get_if<ControlGear>(&dev))
        return *gear;
    return *etl::get_if<InputDevice>(&dev);
}

} // namespace daliMQTT

#endif // DALIMQTT_DALIDEVICE_HXX