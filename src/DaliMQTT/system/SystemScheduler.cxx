// Copyright (c) 2026 Alice-Trade Inc.
// SPDX-License-Identifier: GPL-3.0-or-later

#include "system/SystemScheduler.hxx"

namespace daliMQTT {

void SystemScheduler::tick(const int64_t nowSec) {
    if (m_telemetryIntervalSec > 0 && (nowSec - m_lastTelemetrySec >= m_telemetryIntervalSec)) {
        m_lastTelemetrySec = nowSec;
        if (m_telemetryCb)
            m_telemetryCb(m_telemetryCtx);
    }

    if (m_busHealthIntervalSec > 0 && (nowSec - m_lastBusHealthSec >= m_busHealthIntervalSec)) {
        m_lastBusHealthSec = nowSec;
        if (m_busHealthCb)
            m_busHealthCb(m_busHealthCtx);
    }

    if (m_otaIntervalSec > 0 && (nowSec - m_lastOtaCheckSec >= m_otaIntervalSec)) {
        m_lastOtaCheckSec = nowSec;
        if (m_otaCheckCb)
            m_otaCheckCb(m_otaCheckCtx);
    }
}

} // namespace daliMQTT