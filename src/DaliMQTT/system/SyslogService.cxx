// Copyright (c) 2026 Alice-Trade Inc.
// SPDX-License-Identifier: GPL-3.0-or-later

#include "system/SyslogService.hxx"
#include <cstdio>
#include <cstring>
#include <esp_timer.h>
#include <lwip/netdb.h>
#include <lwip/sockets.h>

namespace daliMQTT {

static constexpr char TAG[] = "Syslog";

static SyslogService* g_syslogServiceInstance = nullptr;
static vprintf_like_t g_originalVprintf = nullptr;

SyslogService::SyslogService() = default;

SyslogService::~SyslogService() {
    stop();
}

esp_err_t SyslogService::start(const char* serverAddr) {
    if (!serverAddr || strlen(serverAddr) == 0 || strlen(serverAddr) >= m_serverAddr.capacity())
        return ESP_ERR_INVALID_ARG;

    if (m_running.load()) {
        stop();
    }

    m_serverAddr = serverAddr;
    m_sockFd = -1;
    m_lastConnectAttemptUs = 0;

    m_ringBuf = xRingbufferCreate(RING_BUFFER_SIZE, RINGBUF_TYPE_NOSPLIT);
    if (!m_ringBuf)
        return ESP_ERR_NO_MEM;

    m_running.store(true);
    g_syslogServiceInstance = this;
    TaskHandle_t createdTask = nullptr;

    const BaseType_t res = xTaskCreate(syslogTaskRunner, "syslog_task", 4096, this, 4, &createdTask);
    if (res != pdPASS) {
        m_running.store(false);
        g_syslogServiceInstance = nullptr;
        vRingbufferDelete(m_ringBuf);
        m_ringBuf = nullptr;
        return ESP_FAIL;
    }
    m_taskHandle.store(createdTask);

    g_originalVprintf = esp_log_set_vprintf(&syslogVprintfHook);
    ESP_LOGI(TAG, "Remote Syslog logging initialized -> %s:514", m_serverAddr.c_str());
    return ESP_OK;
}

void SyslogService::stop() {
    if (!m_running.exchange(false)) {
        return;
    }

    if (g_originalVprintf) {
        esp_log_set_vprintf(g_originalVprintf);
        g_originalVprintf = nullptr;
    }
    g_syslogServiceInstance = nullptr;

    vTaskDelay(pdMS_TO_TICKS(30));

    if (m_taskHandle.load() != nullptr) {
        constexpr char dummy = '\0';
        xRingbufferSend(m_ringBuf, &dummy, 1, 0);

        for (int i = 0; i < 30 && m_taskHandle.load() != nullptr; ++i) {
            vTaskDelay(pdMS_TO_TICKS(10));
        }

        if (TaskHandle_t task = m_taskHandle.load()) {
            vTaskDelete(task);
            m_taskHandle.store(nullptr);
        }
    }

    if (m_ringBuf) {
        vRingbufferDelete(m_ringBuf);
        m_ringBuf = nullptr;
    }

    std::lock_guard<std::mutex> lock(m_socketMutex);
    if (m_sockFd >= 0) {
        close(m_sockFd);
        m_sockFd = -1;
    }
}

int SyslogService::syslogVprintfHook(const char* format, va_list args) {
    int ret = 0;
    if (g_originalVprintf) {
        va_list cpy;
        va_copy(cpy, args);
        ret = g_originalVprintf(format, cpy);
        va_end(cpy);
    }

    const auto* inst = g_syslogServiceInstance;
    if (!inst || !inst->m_running.load(std::memory_order_relaxed) || !inst->m_ringBuf)
        return ret;

    if (xPortInIsrContext())
        return ret;

    if (xTaskGetCurrentTaskHandle() == inst->m_taskHandle.load(std::memory_order_relaxed))
        return ret;

    char msgBuf[MAX_LOG_PAYLOAD];
    const int len = vsnprintf(msgBuf, sizeof(msgBuf), format, args);
    if (len > 0) {
        const size_t actualLen = (static_cast<size_t>(len) < sizeof(msgBuf)) ? len : (sizeof(msgBuf) - 1);
        xRingbufferSend(inst->m_ringBuf, msgBuf, actualLen, 0);
    }
    return ret;
}

void SyslogService::syslogTaskRunner(void* arg) {
    auto* self = static_cast<SyslogService*>(arg);
    self->syslogWorkerLoop();
    self->m_taskHandle.store(nullptr);
    vTaskDelete(nullptr);
}

bool SyslogService::ensureSocketConnected() {
    std::lock_guard<std::mutex> lock(m_socketMutex);
    if (m_sockFd >= 0) {
        return true;
    }

    const int64_t now = esp_timer_get_time();
    if (now - m_lastConnectAttemptUs < RECONNECT_INTERVAL_US) {
        return false;
    }
    m_lastConnectAttemptUs = now;

    addrinfo hints{};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    addrinfo* res = nullptr;

    if (getaddrinfo(m_serverAddr.c_str(), "514", &hints, &res) != 0 || res == nullptr) {
        return false;
    }

    m_sockFd = socket(res->ai_family, res->ai_socktype, 0);
    if (m_sockFd >= 0) {
        constexpr timeval tv{.tv_sec = 0, .tv_usec = 100'000};
        setsockopt(m_sockFd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

        if (connect(m_sockFd, res->ai_addr, res->ai_addrlen) != 0) {
            close(m_sockFd);
            m_sockFd = -1;
        }
    }
    freeaddrinfo(res);
    return m_sockFd >= 0;
}

void SyslogService::syslogWorkerLoop() {
    size_t itemSize = 0;

    while (m_running.load(std::memory_order_relaxed)) {
        const auto item = static_cast<char*>(xRingbufferReceive(m_ringBuf, &itemSize, pdMS_TO_TICKS(100)));
        if (!item) {
            continue;
        }

        if (itemSize > 0 && item[0] != '\0') {
            if (ensureSocketConnected()) {
                sendUdpPacket(item, itemSize);
            }
        }
        vRingbufferReturnItem(m_ringBuf, item);
    }
}

void SyslogService::sendUdpPacket(const char* msg, size_t len) {
    std::lock_guard<std::mutex> lock(m_socketMutex);
    if (m_sockFd < 0 || len == 0)
        return;

    while (len > 0 && (msg[len - 1] == '\n' || msg[len - 1] == '\r')) {
        len--;
    }
    if (len == 0)
        return;

    char packetBuf[MAX_LOG_PAYLOAD + 32];
    const int headerLen = snprintf(packetBuf, sizeof(packetBuf), "<14>dalimqtt: ");
    if (headerLen <= 0)
        return;

    const size_t maxPayload = sizeof(packetBuf) - headerLen - 1;
    const size_t copyLen = (len < maxPayload) ? len : maxPayload;

    memcpy(packetBuf + headerLen, msg, copyLen);

    if (send(m_sockFd, packetBuf, headerLen + copyLen, 0) < 0) {
        close(m_sockFd);
        m_sockFd = -1;
    }
}

} // namespace daliMQTT