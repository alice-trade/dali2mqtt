//  Copyright (c) 2026 Alice-Trade Inc.
//  SPDX-License-Identifier: GPL-3.0-or-later

#ifndef DALIMQTT_DALIBUSENGINE_HXX
#define DALIMQTT_DALIBUSENGINE_HXX

#include "DaliBusConcept.hxx"
#include "dali/DaliAddress.hxx"
#include "dali/DaliFrame.hxx"
#include "dali/DaliOpCodes.hxx"
#include "dali/DaliSpecialOpCodes.hxx"
#include "dali/RmtDaliTransceiver.hxx"
#include <atomic>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include <optional>

namespace daliMQTT {

using BusSnifferCallback = void (*)(const DaliRawFrame& frame, uint8_t busId, void* userCtx);
enum class BusHealth : uint8_t {
    Ok,
    ShortCircuit,
    HardwareFault
};

class DaliBusEngine {
  public:
    DaliBusEngine(RmtDaliTransceiver& transceiver, uint8_t busId);
    ~DaliBusEngine();

    DaliBusEngine(const DaliBusEngine&) = delete;
    DaliBusEngine& operator=(const DaliBusEngine&) = delete;

    esp_err_t start();

 inline esp_err_t sendDAPC(DaliAddressType type, uint8_t addr, uint8_t level) const;
    inline esp_err_t sendGearCommand(DaliAddressType type, uint8_t addr, OpCode opcode, bool sendTwice = false) const;
    inline esp_err_t sendGearSpecial(SpecialOpCode opcode, uint8_t data, bool sendTwice = false) const;
    inline esp_err_t sendDeviceCommand(uint8_t shortAddr, uint8_t opcode, bool sendTwice = false) const;
    inline esp_err_t sendInstanceCommand(uint8_t shortAddr, uint8_t instance, uint8_t opcode, bool sendTwice = false) const;
    inline esp_err_t sendDeviceSpecial(uint8_t specialOp, uint8_t data, bool sendTwice = false) const;

    inline std::optional<uint8_t> queryGear(DaliAddressType type, uint8_t addr, OpCode opcode) const;
    inline std::optional<uint8_t> queryGearSpecial(SpecialOpCode opcode, uint8_t data) const;
    inline std::optional<uint8_t> queryDevice(uint8_t shortAddr, uint8_t opcode) const;
    inline std::optional<uint8_t> queryInstance(uint8_t shortAddr, uint8_t instance, uint8_t opcode) const;
    std::optional<uint8_t> queryRaw(uint32_t rawData, uint8_t bits = 16) const;
    bool readMemoryBlock(uint8_t shortAddr, uint8_t bank, uint8_t startOffset, uint8_t* outBuffer, uint8_t length) const;
    inline esp_err_t setDtr0(uint8_t value) const;
    inline esp_err_t setDtr1(uint8_t value) const;
    inline esp_err_t setDtr2(uint8_t value) const;
    inline std::optional<uint8_t> readMemoryLocation(uint8_t shortAddr, uint8_t bank, uint8_t offset) const;

    inline void lockBus() const { xSemaphoreTakeRecursive(m_busMutex, portMAX_DELAY); }
    inline void unlockBus() const { xSemaphoreGiveRecursive(m_busMutex); }

    uint32_t findAddressBinarySearch(bool is24BitInputDevice) const;

    [[nodiscard]] inline BusHealth checkHealth() const noexcept;
    [[nodiscard]] inline uint8_t getBusId() const noexcept { return m_busId; }
    [[nodiscard]] inline bool isInitialized() const noexcept { return m_running.load(std::memory_order_relaxed); }
    inline void setSnifferCallback(BusSnifferCallback cb, void* ctx) noexcept {
        m_snifferCb = cb;
        m_snifferCtx = ctx;
    }

  private:
    struct TransactionResponse {
        esp_err_t err{ESP_FAIL};
        uint8_t response{0};
    };

    struct TransactionRequest {
        uint32_t data{0};
        uint8_t bits{16};
        bool isQuery{false};
        bool sendTwice{false};
        bool acceptAnyReplyAsYes{false};
        bool isClash{false};
    };
    QueueHandle_t m_respQueue{nullptr};

    esp_err_t executeTransaction(const TransactionRequest& request, uint8_t* outResponse) const;

    static void onPhyFrameReceived(const DaliRawFrame& frame, void* userCtx);
    static void busWorkerTaskRunner(void* arg);
    [[noreturn]] void busWorkerLoop() const;

    static constexpr uint8_t makeAddressByte(DaliAddressType type, uint8_t addr, bool isCommand) noexcept;
    static constexpr uint32_t makeFrame16(uint8_t addressByte, uint8_t opcodeByte) noexcept;
    static constexpr uint32_t makeFrame24(uint8_t addrByte, uint8_t instByte, uint8_t opcodeByte) noexcept;

    static constexpr UBaseType_t NOTIFY_INDEX = 0;
    static constexpr size_t QUEUE_SIZE = 16;

    RmtDaliTransceiver& m_transceiver;
    uint8_t m_busId{0};

    QueueHandle_t m_phyEventQueue{nullptr};
    QueueHandle_t m_txQueue{nullptr};
    SemaphoreHandle_t m_busMutex{nullptr};
    TaskHandle_t m_workerTaskHandle{nullptr};

    BusSnifferCallback m_snifferCb{nullptr};
    void* m_snifferCtx{nullptr};

    std::atomic<bool> m_running{false};
};
static_assert(DaliBusConcept<DaliBusEngine>, "DaliBusEngine must strictly satisfy DaliBusConcept");

struct DaliBusLock {
    const DaliBusEngine& engine;
    explicit DaliBusLock(const DaliBusEngine& e) : engine(e) { engine.lockBus(); }
    ~DaliBusLock() { engine.unlockBus(); }
};

} // namespace daliMQTT

#include "dali/DaliBusEngine.icc"

#endif // DALIMQTT_DALIBUSENGINE_HXX
