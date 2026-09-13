// Copyright (c) 2026 Alice-Trade Inc.
// SPDX-License-Identifier: GPL-3.0-or-later

#include "dali/RmtDaliTransceiver.hxx"
#include <algorithm>
#include <cstring>
#include <esp_check.h>
#include <esp_log.h>
#include <esp_rom_sys.h>
#include <esp_timer.h>
#include <soc/soc_caps.h>

namespace daliMQTT {

static constexpr char TAG[] = "RmtPhy";

RmtDaliTransceiver::RmtDaliTransceiver()
    : m_txQueue(xQueueCreate(8, sizeof(TxMessage))), m_rxDoneQueue(xQueueCreate(4, sizeof(uint8_t))) {}

RmtDaliTransceiver::~RmtDaliTransceiver() {
    m_initialized.store(false);
    if (m_rxChannel) {
        rmt_disable(m_rxChannel);
    }
    if (m_txChannel) {
        rmt_disable(m_txChannel);
    }
    if (m_taskHandle) {
        vTaskDelete(m_taskHandle);
        m_taskHandle = nullptr;
    }

    if (m_rxChannel) {
        rmt_del_channel(m_rxChannel);
        m_rxChannel = nullptr;
    }
    if (m_txChannel) {
        rmt_del_channel(m_txChannel);
        m_txChannel = nullptr;
    }
    if (m_copyEncoder) {
        rmt_del_encoder(m_copyEncoder);
        m_copyEncoder = nullptr;
    }

    if (m_txQueue) {
        vQueueDelete(m_txQueue);
        m_txQueue = nullptr;
    }
    if (m_rxDoneQueue) {
        vQueueDelete(m_rxDoneQueue);
        m_rxDoneQueue = nullptr;
    }
}

esp_err_t RmtDaliTransceiver::init(const RmtTransceiverConfig& config) {
    if (m_initialized.load())
        return ESP_OK;
    m_config = config;

    ESP_LOGI(TAG, "Initializing RMT DALI Transceiver (RX: %d, TX: %d)", config.rxPin, config.txPin);

    ESP_RETURN_ON_ERROR(setupTx(), TAG, "TX init failed");
    ESP_RETURN_ON_ERROR(setupRx(), TAG, "RX init failed");

    const BaseType_t ret = xTaskCreate(taskRunner, "rmt_dali_phy", 3584, this, 10, &m_taskHandle);
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create FreeRTOS task");
        return ESP_ERR_NO_MEM;
    }

    rmt_receive_config_t rxConf = {
        .signal_range_min_ns = Timing::RX_NOISE_FILTER_NS,
        .signal_range_max_ns = Timing::RX_IDLE_THRESH_NS,
    };
    ESP_ERROR_CHECK(rmt_receive(m_rxChannel, m_rxBuffers[0], sizeof(m_rxBuffers[0]), &rxConf));

    m_initialized.store(true);
    return ESP_OK;
}

esp_err_t RmtDaliTransceiver::setupTx() {
    rmt_tx_channel_config_t txCfg = {
        .gpio_num = m_config.txPin,
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = 1'000'000,
#if defined(SOC_RMT_MEM_WORDS_PER_CHANNEL)
        .mem_block_symbols = SOC_RMT_MEM_WORDS_PER_CHANNEL,
#else
        .mem_block_symbols = RMT_SYMBOLS_CAPACITY,
#endif
        .trans_queue_depth = 4,
        .flags =
            {
                .invert_out = m_config.invertTx,
                .with_dma = false,
            },
    };
    ESP_RETURN_ON_ERROR(rmt_new_tx_channel(&txCfg, &m_txChannel), TAG, "New TX failed");

    rmt_copy_encoder_config_t encCfg = {};
    ESP_RETURN_ON_ERROR(rmt_new_copy_encoder(&encCfg, &m_copyEncoder), TAG, "Copy encoder failed");
    ESP_RETURN_ON_ERROR(rmt_enable(m_txChannel), TAG, "TX enable failed");

    return ESP_OK;
}

esp_err_t RmtDaliTransceiver::setupRx() {
    rmt_rx_channel_config_t rxCfg = {.gpio_num = m_config.rxPin,
                                     .clk_src = RMT_CLK_SRC_DEFAULT,
                                     .resolution_hz = 1'000'000,
#if defined(SOC_RMT_MEM_WORDS_PER_CHANNEL)
                                     .mem_block_symbols = SOC_RMT_MEM_WORDS_PER_CHANNEL,
#else
                                     .mem_block_symbols = RMT_SYMBOLS_CAPACITY,
#endif
                                     .flags = {
                                         .invert_in = m_config.invertRx,
                                         .with_dma = false,
                                     }};
    ESP_RETURN_ON_ERROR(rmt_new_rx_channel(&rxCfg, &m_rxChannel), TAG, "New RX failed");

    rmt_rx_event_callbacks_t cbs = {.on_recv_done = rmtRxDoneCallback};
    ESP_RETURN_ON_ERROR(rmt_rx_register_event_callbacks(m_rxChannel, &cbs, this), TAG, "RX callback failed");
    ESP_RETURN_ON_ERROR(rmt_enable(m_rxChannel), TAG, "RX enable failed");

    return ESP_OK;
}

esp_err_t RmtDaliTransceiver::sendAsync(const uint32_t data, const uint8_t bits) const {
    if (!isInitialized())
        return ESP_ERR_INVALID_STATE;

    const TxMessage msg{.data = data, .bits = bits};
    if (xQueueSend(m_txQueue, &msg, 0) != pdTRUE) {
        ESP_LOGW(TAG, "TX Queue full, frame dropped: 0x%04lX", data);
        return ESP_ERR_NO_MEM;
    }

    if (m_taskHandle) {
        xTaskNotify(m_taskHandle, EVT_TX_QUEUED, eSetBits);
    }
    return ESP_OK;
}

bool IRAM_ATTR RmtDaliTransceiver::rmtRxDoneCallback(rmt_channel_handle_t rxChan, const rmt_rx_done_event_data_t* edata,
                                                     void* userCtx) {
    auto* self = static_cast<RmtDaliTransceiver*>(userCtx);
    const uint8_t finishedBufIdx = self->m_rxBufIdx;
    self->m_rxSymbolCount[finishedBufIdx] = edata->num_symbols;

    const uint8_t nextBufIdx = finishedBufIdx ^ 1;
    self->m_rxBufIdx = nextBufIdx;

    rmt_receive_config_t rxConf = {
        .signal_range_min_ns = Timing::RX_NOISE_FILTER_NS,
        .signal_range_max_ns = Timing::RX_IDLE_THRESH_NS,
    };
    rmt_receive(rxChan, self->m_rxBuffers[nextBufIdx], sizeof(self->m_rxBuffers[0]), &rxConf);

    BaseType_t highTaskWoken = pdFALSE;
    xQueueSendFromISR(self->m_rxDoneQueue, &finishedBufIdx, &highTaskWoken);
    xTaskNotifyFromISR(self->m_taskHandle, 1, eSetBits, &highTaskWoken);
    return highTaskWoken == pdTRUE;
}

void RmtDaliTransceiver::taskRunner(void* arg) {
    static_cast<RmtDaliTransceiver*>(arg)->driverLoop();
}

[[noreturn]] void RmtDaliTransceiver::driverLoop() {
    TxMessage txMsg{};
    rmt_symbol_word_t localRxBuffer[RMT_SYMBOLS_CAPACITY];

    auto processBuffer = [&](const uint8_t bufIdx) {
        const size_t rawCount = m_rxSymbolCount[bufIdx];
        const size_t copyCount = std::min<size_t>(rawCount, RMT_SYMBOLS_CAPACITY);
        memcpy(localRxBuffer, m_rxBuffers[bufIdx], copyCount * sizeof(rmt_symbol_word_t));
        decodeSymbols(localRxBuffer, copyCount);
    };

    while (true) {
        if (m_config.rxPin != GPIO_NUM_NC) {
            const int level = gpio_get_level(m_config.rxPin);
            const bool isBusActive = m_config.invertRx ? (level == 0) : (level == 1);
            const int64_t nowUs = esp_timer_get_time();

            if (isBusActive) {
                if (m_lineActiveStartUs == 0) {
                    m_lineActiveStartUs = nowUs;
                } else if ((nowUs - m_lineActiveStartUs) >= 500'000) { // 500ms по IEC 62386-101
                    m_isBusStuckActive.store(true, std::memory_order_relaxed);
                }
            } else {
                m_lineActiveStartUs = 0;
                m_isBusStuckActive.store(false, std::memory_order_relaxed);
            }
        }

        const int64_t now = esp_timer_get_time();
        if (m_txState.active && (now - m_txState.startTs > Timing::TX_TIMEOUT_US)) {
            m_txState.active = false;
        }

        TickType_t waitTicks = pdMS_TO_TICKS(50);
        if (!m_txState.active && uxQueueMessagesWaiting(m_txQueue) > 0) {
            const int64_t elapsedUs = now - m_lastBusActivityUs;
            if (elapsedUs < Timing::FWD_TO_FWD_DELAY_US) {
                const int64_t remainUs = Timing::FWD_TO_FWD_DELAY_US - elapsedUs;
                waitTicks = pdMS_TO_TICKS((remainUs / 1000) + 1);
            } else {
                waitTicks = 0;
            }
        }
        uint32_t eventBits = 0;
        xTaskNotifyWait(0, 0xFFFFFFFF, &eventBits, waitTicks);

        uint8_t readyBufIdx = 0;
        while (xQueueReceive(m_rxDoneQueue, &readyBufIdx, 0) == pdTRUE) {
            processBuffer(readyBufIdx);
        }

        if (!m_txState.active && uxQueueMessagesWaiting(m_txQueue) > 0) {
            const int64_t currentNowUs = esp_timer_get_time();
            if ((currentNowUs - m_lastBusActivityUs) >= Timing::FWD_TO_FWD_DELAY_US) {
                if (xQueueReceive(m_txQueue, &txMsg, 0) == pdTRUE) {
                    m_txState.active = true;
                    m_txState.data = txMsg.data;
                    m_txState.bits = txMsg.bits;
                    m_txState.startTs = currentNowUs;

                    const size_t symCount = encodeManchester(txMsg.data, txMsg.bits);
                    rmt_transmit_config_t txConf = {
                        .loop_count = 0,
                        .flags = {
                            .eot_level = Timing::LEVEL_IDLE
                        }
                    };
                    ESP_ERROR_CHECK(rmt_transmit(m_txChannel, m_copyEncoder, m_txBuffer,
                                                 symCount * sizeof(rmt_symbol_word_t), &txConf));
                }
            }
        }
    }
}

} // namespace daliMQTT