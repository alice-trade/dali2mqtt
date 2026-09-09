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
        vSemaphoreDelete(m_busMutex);
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

    const BaseType_t ret = xTaskCreate(busWorkerTaskRunner, "dali_bus_eng", 4096, this, 8, &m_workerTaskHandle);
    if (ret != pdPASS) {
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
    enum class EngineState { Idle, Transmitting, AwaitingReply, TwiceDelay };
    auto state = EngineState::Idle;

    TransactionRequest activeTx{};
    int64_t stateEnterTimeUs = 0;
    uint8_t retryCount = 0;

    auto finish = [&](const esp_err_t result, const uint8_t response) {
        const TransactionResponse resp{.err = result, .response = response};
        if (m_respQueue) {
            xQueueOverwrite(m_respQueue, &resp);
        }
        state = EngineState::Idle;
        activeTx = {};
    };

    while (true) {
        DaliRawFrame frame{};
        const TickType_t waitTicks = (state == EngineState::Idle) ? pdMS_TO_TICKS(50) : pdMS_TO_TICKS(1);

        if (xQueueReceive(m_phyEventQueue, &frame, waitTicks) == pdTRUE) {
            if (state == EngineState::Idle) {
                if (frame.isValid() && m_snifferCb) {
                    m_snifferCb(frame, m_busId, m_snifferCtx);
                }
            } else if (state == EngineState::Transmitting) {
                if (activeTx.isQuery && frame.isBackward()) {
                    finish(ESP_OK, static_cast<uint8_t>(frame.data & 0xFF));
                } else if (frame.type == DaliFrameType::Collision || frame.type == DaliFrameType::NoiseCorrupted) {
                    if (++retryCount <= 2) {
                        m_transceiver.sendAsync(activeTx.data, activeTx.bits);
                        stateEnterTimeUs = esp_timer_get_time();
                    } else {
                        finish(ESP_FAIL, 0);
                    }
                } else if (frame.type == DaliFrameType::TxEchoSuccess) {
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
                }
            } else if (state == EngineState::AwaitingReply) {
                if (frame.isBackward()) {
                    finish(ESP_OK, static_cast<uint8_t>(frame.data & 0xFF));
                } else if (activeTx.acceptAnyReplyAsYes &&
                           (frame.type == DaliFrameType::Collision || frame.type == DaliFrameType::NoiseCorrupted)) {
                    finish(ESP_OK, 0xFF);
                }
            }
        }

        const int64_t nowUs = esp_timer_get_time();

        switch (state) {
        case EngineState::Transmitting: {
            const int64_t txDurationUs = (1 + activeTx.bits + 4) * 834 + 25'000;
            if (nowUs - stateEnterTimeUs >= txDurationUs) {
                if (activeTx.sendTwice) {
                    state = EngineState::TwiceDelay;
                    stateEnterTimeUs = nowUs;
                } else if (activeTx.isQuery) {
                    state = EngineState::AwaitingReply;
                    stateEnterTimeUs = nowUs;
                } else {
                    finish(ESP_OK, 0);
                }
            }
            break;
        }

        case EngineState::TwiceDelay:
            if (nowUs - stateEnterTimeUs >= 15'000) {
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

    if (xSemaphoreTakeRecursive(m_busMutex, pdMS_TO_TICKS(500)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    xQueueReset(m_respQueue);

    if (xQueueSend(m_txQueue, &request, pdMS_TO_TICKS(100)) != pdTRUE) {
        xSemaphoreGiveRecursive(m_busMutex);
        return ESP_ERR_NO_MEM;
    }

    TransactionResponse resp{};
    const BaseType_t res = xQueueReceive(m_respQueue, &resp, pdMS_TO_TICKS(300));
    xSemaphoreGiveRecursive(m_busMutex);

    if (res != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    if (resp.err == ESP_OK && outResponse) {
        *outResponse = resp.response;
    }
    return resp.err;
}

std::optional<uint8_t> DaliBusEngine::queryRaw(const uint32_t rawData, const uint8_t bits) const {
    uint8_t response = 0;
    const TransactionRequest req{
        .data = rawData, .bits = bits, .isQuery = true, .sendTwice = false, .acceptAnyReplyAsYes = false};

    if (executeTransaction(req, &response) == ESP_OK) {
        return response;
    }
    return std::nullopt;
}

uint32_t DaliBusEngine::findAddressBinarySearch(const bool is24BitInputDevice) const {
    uint32_t low = 0;
    uint32_t high = 0xFFFFFF;
    uint32_t searchAddr = 0xFFFFFF;

    auto sendSearchAddr = [&](const uint32_t addr) {
        if (is24BitInputDevice) {
            sendSpecial24BitCommand(0x05, static_cast<uint8_t>((addr >> 16) & 0xFF), false);
            sendSpecial24BitCommand(0x06, static_cast<uint8_t>((addr >> 8) & 0xFF), false);
            sendSpecial24BitCommand(0x07, static_cast<uint8_t>(addr & 0xFF), false);
        } else {
            sendSpecialCommand(SpecialOpCode::SearchAddrH, static_cast<uint8_t>((addr >> 16) & 0xFF), false);
            sendSpecialCommand(SpecialOpCode::SearchAddrM, static_cast<uint8_t>((addr >> 8) & 0xFF), false);
            sendSpecialCommand(SpecialOpCode::SearchAddrL, static_cast<uint8_t>(addr & 0xFF), false);
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

    sendSearchAddr(0xFFFFFF);
    if (!sendCompare(true)) {
        xSemaphoreGiveRecursive(m_busMutex);
        return InvalidLongAddr;
    }

    while ((high - low) > 0) {
        searchAddr = low + (high - low) / 2;
        sendSearchAddr(searchAddr);

        if (sendCompare(true)) {
            high = searchAddr;
        } else {
            low = searchAddr + 1;
        }
    }

    searchAddr = low;
    sendSearchAddr(searchAddr);

    const bool verifiedClean = sendCompare(false);

    xSemaphoreGiveRecursive(m_busMutex);
    return verifiedClean ? searchAddr : ClashLongAddr;
}

} // namespace daliMQTT