// Copyright (c) 2026 Alice-Trade Inc.
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef DALIMQTT_OTASERVICE_HXX
#define DALIMQTT_OTASERVICE_HXX

#include <atomic>
#include <esp_err.h>
#include <etl/string.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

namespace daliMQTT {
struct OtaVersionInfo {
    etl::string<32> latestVersion{};
    etl::string<160> releaseUrl{};
    etl::string<160> firmwareUrl{};
    bool updateAvailable{false};
    int64_t lastCheckTsSec{0};
};

enum class OtaStatus : uint8_t { Idle, CheckingVersion, InProgress, Verifying, Success, Failed };

struct OtaProgressEvent {
    OtaStatus status{OtaStatus::Idle};
    uint8_t percentage{0};
    const char* stepDescription{""};
};

using OtaProgressCallback = void (*)(const OtaProgressEvent& event, void* userCtx);
using OtaVersionCallback = void (*)(const OtaVersionInfo& info, void* userCtx);

class OtaService {
  public:
    OtaService();
    ~OtaService();

    OtaService(const OtaService&) = delete;
    OtaService& operator=(const OtaService&) = delete;

    esp_err_t startUpdate(const char* url, bool updateWebFs = true);

    [[nodiscard]] inline bool isUpdating() const noexcept;
    [[nodiscard]] inline OtaStatus getStatus() const noexcept;

    inline void setProgressCallback(OtaProgressCallback cb, void* ctx) noexcept;
    esp_err_t checkForUpdateAsync(const char* manifestUrl);

    inline void setVersionCallback(OtaVersionCallback cb, void* ctx) noexcept;
    [[nodiscard]] inline OtaVersionInfo getVersionInfo() const noexcept;

  private:
    static void otaTaskRunner(void* arg);
    [[noreturn]] void otaWorkerLoop();
    static void versionCheckTaskRunner(void* arg);
    void performVersionCheck(const char* url);

    esp_err_t performAppOta(const char* appUrl);
    esp_err_t performFsOta(const char* fsUrl);
    void notifyProgress(OtaStatus status, uint8_t percentage, const char* desc);

    std::atomic<bool> m_isUpdating{false};
    std::atomic<OtaStatus> m_status{OtaStatus::Idle};

    etl::string<256> m_targetUrl{};
    bool m_updateWebFs{true};
    mutable std::mutex m_infoMutex{};
    OtaVersionInfo m_versionInfo{};
    OtaVersionCallback m_versionCb{nullptr};
    void* m_versionCtx{nullptr};
    TaskHandle_t m_taskHandle{nullptr};
    OtaProgressCallback m_progressCb{nullptr};
    void* m_progressCtx{nullptr};
};

} // namespace daliMQTT

#include "system/OtaService.icc"

#endif // DALIMQTT_OTASERVICE_HXX