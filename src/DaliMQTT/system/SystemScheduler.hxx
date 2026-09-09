// Copyright (c) 2026 Alice-Trade Inc.
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef DALIMQTT_SYSTEMSCHEDULER_HXX
#define DALIMQTT_SYSTEMSCHEDULER_HXX

#include <cstdint>
#include <esp_timer.h>

namespace daliMQTT {

using ScheduledActionCallback = void (*)(void* userCtx);

class SystemScheduler {
  public:
    struct Intervals {
        static constexpr uint32_t BUS_HEALTH_CHECK_SEC = 300;
    };

    SystemScheduler() = default;

    void tick(int64_t nowSec);

    inline void setTelemetryCallback(ScheduledActionCallback cb, void* ctx, uint32_t intervalSec) noexcept;
    inline void setBusHealthCallback(ScheduledActionCallback cb, void* ctx, uint32_t intervalSec) noexcept;
    inline void setOtaCheckCallback(ScheduledActionCallback cb, void* ctx, uint32_t intervalSec) noexcept;

  private:
    int64_t m_lastTelemetrySec{0};
    int64_t m_lastBusHealthSec{0};
    int64_t m_lastOtaCheckSec{0};

    uint32_t m_telemetryIntervalSec{0};
    uint32_t m_busHealthIntervalSec{0};
    uint32_t m_otaIntervalSec{0};

    ScheduledActionCallback m_telemetryCb{nullptr};
    void* m_telemetryCtx{nullptr};

    ScheduledActionCallback m_busHealthCb{nullptr};
    void* m_busHealthCtx{nullptr};

    ScheduledActionCallback m_otaCheckCb{nullptr};
    void* m_otaCheckCtx{nullptr};
};

} // namespace daliMQTT

#include "system/SystemScheduler.icc"

#endif // DALIMQTT_SYSTEMSCHEDULER_HXX