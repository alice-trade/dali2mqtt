// Copyright (c) 2026 Alice-Trade Inc.
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef DALIMQTT_DALISERVICE_HXX
#define DALIMQTT_DALISERVICE_HXX
#include "dali/RmtDaliTransceiver.hxx"
#include "dali/DaliBusEngine.hxx"
#include "dali/DaliDeviceRegistry.hxx"
#include "system/ConfigStructure.hxx"

namespace daliMQTT {

class DaliService {
public:
    inline DaliService();
    ~DaliService() = default;

    DaliService(const DaliService&) = delete;
    DaliService& operator=(const DaliService&) = delete;

    esp_err_t inline init(const BusHardwareConfig& cfg);

    [[nodiscard]] inline BusHealth checkHealth() const;
    [[nodiscard]] inline DaliDeviceRegistry& Registry();
    [[nodiscard]] inline DaliBusEngine& Bus();
    [[nodiscard]] inline RmtDaliTransceiver& Driver();

private:
    RmtDaliTransceiver m_phy;
    DaliBusEngine m_bus;
    DaliDeviceRegistry m_registry;
};

} // namespace daliMQTT
#include "dali/DaliService.icc"

#endif // DALIMQTT_DALISERVICE_HXX
