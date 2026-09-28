// Copyright (c) 2026 Alice-Trade Inc.
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef DALIMQTT_SYSLOGSERVICE_HXX
#define DALIMQTT_SYSLOGSERVICE_HXX

#include <atomic>
#include <esp_err.h>
#include <esp_log.h>
#include <etl/string.h>
#include <freertos/FreeRTOS.h>
#include <freertos/ringbuf.h>
#include <freertos/task.h>
#include <mutex>

namespace daliMQTT {

class SyslogService {
public:
    SyslogService();
    ~SyslogService();

    SyslogService(const SyslogService&) = delete;
    SyslogService& operator=(const SyslogService&) = delete;

    esp_err_t start(const char* serverAddr);
    void stop();

private:
    __attribute__((format(printf, 1, 0))) static int syslogVprintfHook(const char* format, va_list args);
    static void syslogTaskRunner(void* arg);
    void syslogWorkerLoop();

    bool ensureSocketConnected();
    void sendUdpPacket(const char* msg, size_t len);

    static constexpr size_t RING_BUFFER_SIZE = 2048;
    static constexpr size_t MAX_LOG_PAYLOAD = 256;
    static constexpr int64_t RECONNECT_INTERVAL_US = 5'000'000;

    etl::string<64> m_serverAddr{};
    int m_sockFd{-1};
    int64_t m_lastConnectAttemptUs{0};

    RingbufHandle_t m_ringBuf{nullptr};
    std::atomic<TaskHandle_t> m_taskHandle{nullptr};
    std::atomic<bool> m_running{false};

    std::mutex m_socketMutex{};
};

} // namespace daliMQTT

#endif // DALIMQTT_SYSLOGSERVICE_HXX