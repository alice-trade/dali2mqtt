// Copyright (c) 2026 Alice-Trade Inc.
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef DALIMQTT_SYSLOGSERVICE_HXX
#define DALIMQTT_SYSLOGSERVICE_HXX

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
    static int syslogVprintfHook(const char* format, va_list args);
    static void syslogTaskRunner(void* arg);
    [[noreturn]] void syslogWorkerLoop();
    void sendUdpPacket(const char* msg, size_t len);

    static constexpr size_t RING_BUFFER_SIZE = 2048;
    static constexpr size_t MAX_LOG_PAYLOAD = 256;

    etl::string<64> m_serverAddr{};
    int m_sockFd{-1};
    RingbufHandle_t m_ringBuf{nullptr};
    TaskHandle_t m_taskHandle{nullptr};
    vprintf_like_t m_originalVprintf{nullptr};
    std::mutex m_socketMutex{};
};

} // namespace daliMQTT

#endif // DALIMQTT_SYSLOGSERVICE_HXX