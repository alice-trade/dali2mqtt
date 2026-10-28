// Copyright (c) 2026 Alice-Trade Inc.
// SPDX-License-Identifier: GPL-3.0-or-later

#include "dali/DaliBusEngine.hxx"
#include <esp_log.h>
#include <esp_random.h>
#include <esp_timer.h>

namespace daliMQTT {

static constexpr char TAG[] = "DaliBusEngine";

DaliBusEngine::DaliBusEngine(RmtDaliTransceiver& transceiver, const uint8_t busId)
    : m_transceiver(transceiver), m_busId(busId) {
    m_busMutex = xSemaphoreCreateRecursiveMutex();
    m_phyEventQueue = xQueueCreate(16, sizeof(DaliRawFrame));
    m_txQueue = xQueueCreate(QUEUE_SIZE, sizeof(TransactionRequest));
    m_respQueue = xQueueCreate(1, sizeof(TransactionResponse));
}

DaliBusEngine::~DaliBusEngine() {
    m_running.store(false);
    m_transceiver.setFrameCallback(nullptr, nullptr);

    if (m_workerTaskHandle)
        vTaskDelete(m_workerTaskHandle);
    if (m_busMutex)
        vQueueDelete(reinterpret_cast<QueueHandle_t>(m_busMutex));
    if (m_phyEventQueue)
        vQueueDelete(m_phyEventQueue);
    if (m_txQueue)
        vQueueDelete(m_txQueue);
    if (m_respQueue)
        vQueueDelete(m_respQueue);
}

esp_err_t DaliBusEngine::start() {
    if (m_running.load())
        return ESP_OK;

    m_transceiver.setFrameCallback(onPhyFrameReceived, this);

    m_workerTaskHandle = xTaskCreateStatic(busWorkerTaskRunner, "dali_bus_eng", BUS_ENG_STACK_SIZE, this, 8,
                                           m_workerStack, &m_workerTaskBuffer);
    if (!m_workerTaskHandle) {
        ESP_LOGE(TAG, "Failed to create bus worker task");
        return ESP_ERR_NO_MEM;
    }

    m_running.store(true);
    ESP_LOGI(TAG, "DALI Bus Engine #%d started", m_busId);
    return ESP_OK;
}

void DaliBusEngine::onPhyFrameReceived(const DaliRawFrame& frame, void* userCtx) {
    auto* self = static_cast<DaliBusEngine*>(userCtx);
    if (self->m_phyEventQueue) {
        xQueueSend(self->m_phyEventQueue, &frame, 0);
    }
}

void DaliBusEngine::busWorkerTaskRunner(void* arg) {
    static_cast<DaliBusEngine*>(arg)->busWorkerLoop();
}

[[noreturn]] void DaliBusEngine::busWorkerLoop() const {
    enum class EngineState { Idle, Transmitting, AwaitingReply, TwiceDelay, CollisionBackoff };
    auto state = EngineState::Idle;

    TransactionRequest activeTx{};
    int64_t stateEnterTimeUs = 0;
    int64_t currentBackoffUs = 35'000;
    uint8_t retryCount = 0;

    auto finish = [&](const esp_err_t result, const uint8_t response) {
        const TransactionResponse resp{.err = result, .response = response, .token = activeTx.token};
        if (m_respQueue) {
            xQueueOverwrite(m_respQueue, &resp);
        }
        state = EngineState::Idle;
        activeTx = {};
    };

    auto getDynamicWaitTicks = [&]() -> TickType_t {
        if (state == EngineState::Idle) {
            return pdMS_TO_TICKS(50);
        }

        int64_t targetTimeoutUs = 0;
        switch (state) {
        case EngineState::CollisionBackoff:
            targetTimeoutUs = currentBackoffUs;
            break;
        case EngineState::TwiceDelay:
            targetTimeoutUs = 40'000;
            break;
        case EngineState::AwaitingReply:
            targetTimeoutUs = 25'000;
            break;
        case EngineState::Transmitting:
            targetTimeoutUs = 70'000;
            break;
        default:
            return 0;
        }

        const int64_t elapsedUs = esp_timer_get_time() - stateEnterTimeUs;
        const int64_t remUs = targetTimeoutUs - elapsedUs;

        if (remUs <= 0) {
            return 0;
        }

        return pdMS_TO_TICKS((remUs + 999) / 1000);
    };

    while (true) {
        DaliRawFrame frame{};
        const TickType_t waitTicks = getDynamicWaitTicks();

        if (xQueueReceive(m_phyEventQueue, &frame, waitTicks) == pdTRUE &&
           frame.type != DaliFrameType::Wakeup) {

            if (m_rawSnifferCb && (frame.isValid() || frame.type == DaliFrameType::Collision)) {
                m_rawSnifferCb(frame, m_busId, m_rawSnifferCtx);
            }

            switch (state) {
            case EngineState::Idle:
                if (frame.isValid() && m_snifferCb) {
                    m_snifferCb(frame, m_busId, m_snifferCtx);
                }
                break;

            case EngineState::Transmitting:
                if (frame.type == DaliFrameType::TxEchoSuccess) {
                    const int64_t now = esp_timer_get_time();
                    if (activeTx.sendTwice) {
                        state = EngineState::TwiceDelay;
                        stateEnterTimeUs = now;
                    } else if (activeTx.isQuery) {
                        state = EngineState::AwaitingReply;
                        stateEnterTimeUs = now;
                    } else {
                        finish(ESP_OK, 0);
                    }
                } else if (frame.type == DaliFrameType::Collision || frame.type == DaliFrameType::NoiseCorrupted) {
                    if (++retryCount <= 2) {
                        currentBackoffUs = 15'000 + (esp_random() % 25'000);
                        ESP_LOGW(TAG, "Bus collision on TX, backing off (%d/2)...", retryCount);
                        state = EngineState::CollisionBackoff;
                        stateEnterTimeUs = esp_timer_get_time();
                    } else {
                        ESP_LOGE(TAG, "Bus collision retry limit exceeded");
                        finish(ESP_FAIL, 0);
                    }
                }
                break;

            case EngineState::AwaitingReply:
                if (frame.isBackward()) {
                    finish(ESP_OK, static_cast<uint8_t>(frame.data & 0xFF));
                } else if (activeTx.acceptAnyReplyAsYes &&
                           (frame.type == DaliFrameType::Collision || frame.type == DaliFrameType::NoiseCorrupted)) {
                    finish(ESP_OK, 0xFF);
                }
                break;

            case EngineState::TwiceDelay:
            case EngineState::CollisionBackoff:
                break;
            }
        }

        const int64_t nowUs = esp_timer_get_time();

        switch (state) {
        case EngineState::Transmitting: {
            constexpr int64_t MAX_TX_TIMEOUT_US = 70'000;
            if (nowUs - stateEnterTimeUs >= MAX_TX_TIMEOUT_US) {
                ESP_LOGE(TAG, "TX timeout: frame was not transmitted (no echo on bus)");
                finish(ESP_ERR_TIMEOUT, 0);
            }
            break;
        }
        case EngineState::CollisionBackoff:
            if (nowUs - stateEnterTimeUs >= currentBackoffUs) {
                m_transceiver.sendAsync(activeTx.data, activeTx.bits);
                state = EngineState::Transmitting;
                stateEnterTimeUs = nowUs;
            }
            break;

        case EngineState::TwiceDelay:
            if (nowUs - stateEnterTimeUs >= 40'000) {
                activeTx.sendTwice = false;
                m_transceiver.sendAsync(activeTx.data, activeTx.bits);
                state = EngineState::Transmitting;
                stateEnterTimeUs = nowUs;
            }
            break;

        case EngineState::AwaitingReply:
            if (nowUs - stateEnterTimeUs > 25'000) {
                finish(ESP_ERR_TIMEOUT, 0);
            }
            break;

        case EngineState::Idle:
            if (xQueueReceive(m_txQueue, &activeTx, 0) == pdTRUE) {
                retryCount = 0;
                m_transceiver.sendAsync(activeTx.data, activeTx.bits);
                state = EngineState::Transmitting;
                stateEnterTimeUs = nowUs;
            }
            break;
        }
    }
}

esp_err_t DaliBusEngine::executeTransaction(const TransactionRequest& request, uint8_t* outResponse) const {
    if (!isInitialized())
        return ESP_ERR_INVALID_STATE;

    if (xSemaphoreTakeRecursive(m_busMutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    TransactionResponse staleResp;
    while (xQueueReceive(m_respQueue, &staleResp, 0) == pdTRUE) {
        ESP_LOGD(TAG, "Drained stale response with token %u", staleResp.token);
    }

    uint16_t currentToken = m_nextToken.fetch_add(1, std::memory_order_relaxed);
    if (currentToken == 0) {
        currentToken = m_nextToken.fetch_add(1, std::memory_order_relaxed);
    }

    TransactionRequest req = request;
    req.token = currentToken;

    if (xQueueSend(m_txQueue, &req, pdMS_TO_TICKS(100)) != pdTRUE) {
        xSemaphoreGiveRecursive(m_busMutex);
        return ESP_ERR_NO_MEM;
    }

    constexpr DaliRawFrame kick{.type = DaliFrameType::Wakeup};
    xQueueSend(m_phyEventQueue, &kick, 0);

    constexpr TickType_t WAIT_TIMEOUT = pdMS_TO_TICKS(350);
    const TickType_t startTick = xTaskGetTickCount();

    TransactionResponse resp{};
    esp_err_t finalErr = ESP_ERR_TIMEOUT;

    while ((xTaskGetTickCount() - startTick) < WAIT_TIMEOUT) {
        TickType_t remaining = WAIT_TIMEOUT - (xTaskGetTickCount() - startTick);
        if (xQueueReceive(m_respQueue, &resp, remaining) == pdTRUE) {
            if (resp.token == currentToken) {
                finalErr = resp.err;
                if (resp.err == ESP_OK && outResponse) {
                    *outResponse = resp.response;
                }
                break;
            } else {
                ESP_LOGW(TAG, "Discarded stale DALI frame (token mismatch: got %u, expected %u)", resp.token,
                         currentToken);
            }
        } else {
            break;
        }
    }
    xSemaphoreGiveRecursive(m_busMutex);
    return finalErr;
}

RawTransactionResult DaliBusEngine::sendRawFrame(const uint32_t rawData, const uint8_t bits, const bool isQuery,
                                                 const bool sendTwice) const {
    uint8_t response = 0;
    const TransactionRequest req{
        .data = rawData, .bits = bits, .isQuery = isQuery, .sendTwice = sendTwice, .acceptAnyReplyAsYes = false};

    const esp_err_t err = executeTransaction(req, isQuery ? &response : nullptr);
    RawTransactionResult res{.err = err};
    if (err == ESP_OK && isQuery) {
        res.response = response;
    }
    return res;
}

uint32_t DaliBusEngine::findAddressBinarySearch(const bool is24BitInputDevice) const {
    uint32_t low = 0;
    uint32_t high = 0xFFFFFF;
    uint32_t searchAddr = 0xFFFFFF;

    auto sendSearchAddr = [&](const uint32_t addr) -> bool {
        if (is24BitInputDevice) {
            if (sendDeviceSpecial(0x05, static_cast<uint8_t>((addr >> 16) & 0xFF), false) != ESP_OK)
                return false;
            if (sendDeviceSpecial(0x06, static_cast<uint8_t>((addr >> 8) & 0xFF), false) != ESP_OK)
                return false;
            return (sendDeviceSpecial(0x07, static_cast<uint8_t>(addr & 0xFF), false) == ESP_OK);
        } else {
            if (sendGearSpecial(SpecialOpCode::SearchAddrH, static_cast<uint8_t>((addr >> 16) & 0xFF), false) != ESP_OK)
                return false;
            if (sendGearSpecial(SpecialOpCode::SearchAddrM, static_cast<uint8_t>((addr >> 8) & 0xFF), false) != ESP_OK)
                return false;
            return (sendGearSpecial(SpecialOpCode::SearchAddrL, static_cast<uint8_t>(addr & 0xFF), false) == ESP_OK);
        }
    };

    auto sendCompare = [&](const bool allowCollision) -> bool {
        uint8_t resp = 0;
        TransactionRequest req{};
        req.isQuery = true;
        req.sendTwice = false;
        req.acceptAnyReplyAsYes = allowCollision;

        if (is24BitInputDevice) {
            req.data = (0xC1 << 16) | (0x03 << 8);
            req.bits = 24;
        } else {
            req.data = makeFrame16(static_cast<uint8_t>(SpecialOpCode::Compare), 0);
            req.bits = 16;
        }
        return executeTransaction(req, &resp) == ESP_OK;
    };

    if (xSemaphoreTakeRecursive(m_busMutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        return InvalidLongAddr;
    }

    if (!sendSearchAddr(0xFFFFFF) || !sendCompare(true)) {
        xSemaphoreGiveRecursive(m_busMutex);
        return InvalidLongAddr;
    }

    while ((high - low) > 0) {
        searchAddr = low + (high - low) / 2;
        if (!sendSearchAddr(searchAddr)) {
            xSemaphoreGiveRecursive(m_busMutex);
            return InvalidLongAddr;
        }

        if (sendCompare(true)) {
            high = searchAddr;
        } else {
            low = searchAddr + 1;
        }
    }

    searchAddr = low;
    if (!sendSearchAddr(searchAddr)) {
        xSemaphoreGiveRecursive(m_busMutex);
        return InvalidLongAddr;
    }

    const bool verifiedClean = sendCompare(false);

    xSemaphoreGiveRecursive(m_busMutex);
    return verifiedClean ? searchAddr : ClashLongAddr;
}

} // namespace daliMQTT