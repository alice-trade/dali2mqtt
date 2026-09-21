// Copyright (c) 2026 Alice-Trade Inc.
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef DALIMQTT_RMTDALITRANSCEIVER_HXX
#define DALIMQTT_RMTDALITRANSCEIVER_HXX

#include "DaliTransceiverConcept.hxx"
#include "dali/DaliFrame.hxx"
#include <atomic>
#include <driver/gpio.h>
#include <driver/rmt_rx.h>
#include <driver/rmt_tx.h>
#include <esp_err.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>

namespace daliMQTT {

struct RmtTransceiverConfig {
    gpio_num_t rxPin{GPIO_NUM_NC};
    gpio_num_t txPin{GPIO_NUM_NC};
    bool invertRx{true};  ///< Inversion when using a standard optocoupler
    bool invertTx{false}; ///< Direct control of the TX key
};
enum class DaliBusPhysicalState : uint8_t {
    NormalIdle,     // 16V on the rail (Idle)
    StuckActive,    // Short circuit or voltage drop (< 9.5V) for more than 500 ms
};

using PhyFrameCallback = void (*)(const DaliRawFrame& frame, void* userCtx);

class RmtDaliTransceiver {
  public:
    RmtDaliTransceiver();
    ~RmtDaliTransceiver();

    RmtDaliTransceiver(const RmtDaliTransceiver&) = delete;
    RmtDaliTransceiver& operator=(const RmtDaliTransceiver&) = delete;

    esp_err_t init(const RmtTransceiverConfig& config);
    esp_err_t sendAsync(uint32_t data, uint8_t bits) const;

    [[nodiscard]] inline bool isInitialized() const noexcept;
    inline void setFrameCallback(PhyFrameCallback cb, void* ctx) noexcept;
    [[nodiscard]] DaliBusPhysicalState getPhysicalBusState() const noexcept;

  private:
    struct Timing;

    struct TxMessage {
        uint32_t data{0};
        uint8_t bits{0};
    };

    struct TxEchoState {
        bool active{false};
        uint32_t data{0};
        uint8_t bits{0};
        int64_t startTs{0};
    };

    esp_err_t setupTx();
    esp_err_t setupRx();

    static void taskRunner(void* arg);
    [[noreturn]] void driverLoop();

    static bool IRAM_ATTR rmtRxDoneCallback(rmt_channel_handle_t rxChan, const rmt_rx_done_event_data_t* edata,
                                            void* userCtx);

    inline size_t encodeManchester(uint32_t data, uint8_t bits) noexcept;
    inline void decodeSymbols(const rmt_symbol_word_t* symbols, size_t count) noexcept;
    inline void dispatchFrame(DaliFrameType type, uint32_t data, uint8_t bits) const noexcept;

    static constexpr rmt_symbol_word_t makeSymbol(uint32_t dur0, uint8_t lvl0, uint32_t dur1,
                                                         uint8_t lvl1) noexcept;

    static constexpr size_t RMT_SYMBOLS_CAPACITY = 64;

    static constexpr uint32_t EVT_RX_DONE_BUF0 = (1 << 0);
    static constexpr uint32_t EVT_RX_DONE_BUF1 = (1 << 1);
    static constexpr uint32_t EVT_TX_QUEUED    = (1 << 2);
    volatile uint32_t m_rxSymbolCount[2]{0, 0};

    RmtTransceiverConfig m_config{};
    rmt_channel_handle_t m_txChannel{nullptr};
    rmt_channel_handle_t m_rxChannel{nullptr};
    rmt_encoder_handle_t m_copyEncoder{nullptr};

    QueueHandle_t m_txQueue{nullptr};
    QueueHandle_t m_rxDoneQueue{nullptr};
    TaskHandle_t m_taskHandle{nullptr};

    PhyFrameCallback m_frameCb{nullptr};
    void* m_frameCbCtx{nullptr};

    TxEchoState m_txState{};
    int64_t m_lastBusActivityUs{0};
    std::atomic<bool> m_initialized{false};

    rmt_symbol_word_t m_rxBuffers[2][RMT_SYMBOLS_CAPACITY]{};
    volatile uint8_t m_rxBufIdx{0};
    rmt_symbol_word_t m_txBuffer[RMT_SYMBOLS_CAPACITY]{};

    std::atomic<bool> m_isBusStuckActive{false};
    int64_t m_lineActiveStartUs{0};
};

static_assert(DaliTransceiverConcept<RmtDaliTransceiver>,
              "RmtDaliTransceiver must strictly satisfy DaliTransceiverConcept");

} // namespace daliMQTT

#include "dali/RmtDaliTransceiver.icc"

#endif // DALIMQTT_RMTDALITRANSCEIVER_HXX