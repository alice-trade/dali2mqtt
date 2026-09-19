// Copyright (c) 2026 Alice-Trade Inc.
// SPDX-License-Identifier: GPL-3.0-or-later

#include "system/SyslogService.hxx"
#include <cstdio>
#include <cstring>
#include <lwip/netdb.h>
#include <lwip/sockets.h>

namespace daliMQTT {

static constexpr char TAG[] = "Syslog";
static SyslogService* g_syslogServiceInstance = nullptr;

SyslogService::SyslogService() = default;

SyslogService::~SyslogService() {
    stop();
}

esp_err_t SyslogService::start(const char* serverAddr) {
    if (!serverAddr || strlen(serverAddr) == 0)
        return ESP_ERR_INVALID_ARG;
    m_serverAddr = serverAddr;

    g_syslogServiceInstance = this;
    m_ringBuf = xRingbufferCreate(RING_BUFFER_SIZE, RINGBUF_TYPE_NOSPLIT);
    if (!m_ringBuf)
        return ESP_ERR_NO_MEM;

    const BaseType_t res = xTaskCreate(syslogTaskRunner, "syslog_task", 4096, this, 4, &m_taskHandle);
    if (res != pdPASS) {
        vRingbufferDelete(m_ringBuf);
        m_ringBuf = nullptr;
        return ESP_FAIL;
    }

    m_originalVprintf = esp_log_set_vprintf(&syslogVprintfHook);
    ESP_LOGI(TAG, "Remote Syslog logging started -> %s:514", m_serverAddr.c_str());
    return ESP_OK;
}

void SyslogService::stop() {
    if (m_originalVprintf) {
        esp_log_set_vprintf(m_originalVprintf);
        m_originalVprintf = nullptr;
    }

    if (m_taskHandle) {
        vTaskDelete(m_taskHandle);
        m_taskHandle = nullptr;
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

    g_syslogServiceInstance = nullptr;
}

int SyslogService::syslogVprintfHook(const char* format, va_list args) {
    int ret = 0;
    if (g_syslogServiceInstance && g_syslogServiceInstance->m_originalVprintf) {
        va_list cpy;
        va_copy(cpy, args);
        ret = g_syslogServiceInstance->m_originalVprintf(format, cpy);
        va_end(cpy);
    }

    if (!g_syslogServiceInstance || !g_syslogServiceInstance->m_ringBuf)
        return ret;

    if (xTaskGetCurrentTaskHandle() == g_syslogServiceInstance->m_taskHandle)
        return ret;

    char msgBuf[MAX_LOG_PAYLOAD];
    const int len = vsnprintf(msgBuf, sizeof(msgBuf), format, args);
    if (len > 0) {
        const size_t actualLen = (static_cast<size_t>(len) < sizeof(msgBuf)) ? len : (sizeof(msgBuf) - 1);
        if (xPortInIsrContext()) {
            BaseType_t highTaskWoken = pdFALSE;
            xRingbufferSendFromISR(g_syslogServiceInstance->m_ringBuf, msgBuf, actualLen, &highTaskWoken);
#ifndef traceISR_EXIT_TO_SCHEDULER
#define traceISR_EXIT_TO_SCHEDULER()
#endif
            if (highTaskWoken)
                portYIELD_FROM_ISR();
        } else {
            xRingbufferSend(g_syslogServiceInstance->m_ringBuf, msgBuf, actualLen, 0);
        }
    }
    return ret;
}

void SyslogService::syslogTaskRunner(void* arg) {
    static_cast<SyslogService*>(arg)->syslogWorkerLoop();
}

[[noreturn]] void SyslogService::syslogWorkerLoop() {
    struct addrinfo hints{};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    struct addrinfo* res = nullptr;

    if (getaddrinfo(m_serverAddr.c_str(), "514", &hints, &res) == 0 && res != nullptr) {
        std::lock_guard<std::mutex> lock(m_socketMutex);
        m_sockFd = socket(res->ai_family, res->ai_socktype, 0);
        if (m_sockFd >= 0) {
            connect(m_sockFd, res->ai_addr, res->ai_addrlen);
        }
        freeaddrinfo(res);
    }

    size_t itemSize = 0;
    while (true) {
        char* item = static_cast<char*>(xRingbufferReceive(m_ringBuf, &itemSize, portMAX_DELAY));
        if (item != nullptr) {
            sendUdpPacket(item, itemSize);
            vRingbufferReturnItem(m_ringBuf, item);
        }
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
    send(m_sockFd, packetBuf, headerLen + copyLen, 0);
}

} // namespace daliMQTT