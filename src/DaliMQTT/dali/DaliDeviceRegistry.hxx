// Copyright (c) 2026 Alice-Trade Inc.
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef DALIMQTT_DALIDEVICEREGISTRY_HXX
#define DALIMQTT_DALIDEVICEREGISTRY_HXX

#include "dali/DaliBusEngine.hxx"
#include "dali/DaliDevice.hxx"
#include "dali/DaliDeviceEvent.hxx"
#include "dali/DaliGroupEvent.hxx"
#include "dali/DaliInputEvent.hxx"
#include <array>
#include <bitset>
#include <etl/flat_map.h>
#include <etl/flat_set.h>
#include <etl/queue.h>
#include <etl/vector.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <mutex>
#include <optional>

namespace daliMQTT {

struct __attribute__((packed)) AddressMapBlobItem {
    DaliLongAddress_t longAddress{0};
    uint16_t internalAddress{0};
    uint8_t deviceType{0xFF};
    char gtin[16]{0};
    bool isInput{false};
    bool supportsRgb{false};
    bool supportsTc{false};
    uint8_t minLevel{1};
    uint8_t maxLevel{254};
    uint8_t powerOnLevel{254};
    uint8_t systemFailureLevel{254};
    uint16_t groupMask{0};
};
static_assert(sizeof(AddressMapBlobItem) == 32, "NVS Blob struct must be 32 bytes aligned");

struct DaliGroupState {
    uint8_t currentLevel{0};
    uint8_t lastLevel{254};
    std::optional<uint16_t> colorTemp;
    std::optional<DaliRGB> rgb;
};

struct StaticMetadata {
    uint8_t minLevel{1};
    uint8_t maxLevel{254};
    uint8_t powerOnLevel{254};
    uint8_t systemFailureLevel{254};
    std::optional<uint8_t> deviceType;
    std::optional<ColorFeatures> color;
    etl::string<16> gtin{};
};

using SceneLevels = std::array<uint8_t, 64>;
using GroupMask = std::bitset<16>;
using GroupAssignments = etl::flat_map<DaliLongAddress_t, GroupMask, 64>;

class DaliDeviceRegistry {
  public:
    static constexpr size_t MAX_DEVICES_PER_BUS = 128;
    static constexpr size_t BUS_COUNT = 1;
    static constexpr size_t MAX_TOTAL_DEVICES = MAX_DEVICES_PER_BUS * BUS_COUNT;

    explicit DaliDeviceRegistry(DaliBusEngine& busEngine);
    ~DaliDeviceRegistry();

    DaliDeviceRegistry(const DaliDeviceRegistry&) = delete;
    DaliDeviceRegistry& operator=(const DaliDeviceRegistry&) = delete;

    esp_err_t init();
    void start();

    esp_err_t setBrightness(DaliLongAddress_t longAddr, uint8_t level);
    esp_err_t setPower(DaliLongAddress_t longAddr, bool on);
    esp_err_t setColorTemp(DaliLongAddress_t longAddr, uint16_t mireds);
    esp_err_t setRgb(DaliLongAddress_t longAddr, uint8_t r, uint8_t g, uint8_t b);
    esp_err_t setRgbwaf(DaliLongAddress_t longAddr, uint8_t r, uint8_t g, uint8_t b,
                        uint8_t w = 0xFF, uint8_t a = 0xFF, uint8_t f = 0xFF);
    
    esp_err_t setGroupBrightness(uint8_t busId, uint8_t groupId, uint8_t level);
    esp_err_t setGroupPower(uint8_t busId, uint8_t groupId, bool on);
    esp_err_t setDeviceGroupMembership(DaliLongAddress_t longAddr, uint8_t groupId, bool assigned);
    esp_err_t setAllGroupAssignments(const GroupAssignments& newAssignments);
    esp_err_t refreshGroupAssignmentsFromBus();
    [[nodiscard]] inline DaliGroupState getGroupState(uint8_t busId, uint8_t groupId) const;
    [[nodiscard]] inline GroupAssignments getGroupAssignments() const;

    void processSnifferFrame(const DaliRawFrame& frame);
    void processInputDeviceFrame(const DaliRawFrame& frame) const;

    esp_err_t activateScene(uint8_t busId, uint8_t sceneId) const;
    esp_err_t saveSceneLevels(uint8_t busId, uint8_t sceneId, const SceneLevels& levels) const;
    [[nodiscard]] SceneLevels querySceneLevels(uint8_t busId, uint8_t sceneId) const;

    void scanBus();
    void commissionNewDevices();
    void commission24BitDevices();

    [[nodiscard]] etl::vector<DaliDevice, MAX_TOTAL_DEVICES> getDevicesSnapshot() const;
    [[nodiscard]] inline std::optional<DaliInternalAddr> getInternalAddress(DaliLongAddress_t longAddr) const;
    [[nodiscard]] inline std::optional<DaliLongAddress_t> getLongAddress(DaliInternalAddr internalAddr,
                                                                         bool isInput = false) const;
    [[nodiscard]] inline std::optional<uint8_t> getLastLevel(DaliLongAddress_t longAddr) const;

    inline void setDeviceStateCallback(DeviceStateCallback cb, void* ctx) noexcept;
    inline void setGroupStateCallback(GroupStateCallback cb, void* ctx) noexcept;
    inline void setInputEventCallback(InputEventCallback cb, void* ctx) noexcept;
    inline void setDeviceAttributesCallback(DeviceAttributesCallback cb, void* ctx) noexcept;
    void requestSync(DaliInternalAddr addr, uint32_t delayMs = 0);
    void requestBroadcastSync(uint32_t baseDelayMs, uint32_t staggerStepMs);

  private:
    struct DeferredSyncRequest {
        DaliInternalAddr addr;
        int64_t executeAtTsMs{0};
    };
    struct ScanCommissionGuard {
        std::atomic<bool>& flag;
        DaliBusLock busLock;
        ScanCommissionGuard(std::atomic<bool>& f, const DaliBusEngine& bus)
            : flag(f), busLock(bus) {
            flag.store(true, std::memory_order_release);
        }
        ~ScanCommissionGuard() {
            flag.store(false, std::memory_order_release);
        }
    };
    StaticMetadata queryDeviceMetadataFromBus(uint8_t shortAddr) const;

    static void snifferCallbackEntry(const DaliRawFrame& frame, uint8_t busId, void* ctx);

    static void pollTaskRunner(void* arg);
    [[noreturn]] void pollLoop();

    esp_err_t removeDevice(DaliLongAddress_t longAddr);

    void pollSingleDevice(DaliInternalAddr addr);

    void notifyDeviceChange(const ControlGear& gear) const;
    void notifyGroupChange(uint8_t busId, uint8_t groupId, const DaliGroupState& state) const;
    void notifyInputEvent(const InputDeviceEvent& ev) const;

    bool loadAddressMapFromNvs();
    esp_err_t saveAddressMapToNvs();
    void handleDeferredNvsFlush(int64_t nowMs);

    DaliBusEngine& m_bus;

    mutable std::mutex m_registryMutex{};
    etl::vector<DaliDevice, MAX_TOTAL_DEVICES> m_devices{};
    std::array<DaliLongAddress_t, BUS_COUNT * 128> m_internalToLongMap{};
    GroupAssignments m_groupAssignments{};
    std::array<std::array<DaliGroupState, 16>, BUS_COUNT> m_groupStates{};

    mutable std::mutex m_queueMutex{};
    etl::queue<DaliInternalAddr, MAX_TOTAL_DEVICES> m_prioritySyncQueue{};
    etl::flat_set<DaliInternalAddr, MAX_TOTAL_DEVICES> m_prioritySyncSet{};
    etl::vector<DeferredSyncRequest, MAX_TOTAL_DEVICES> m_deferredSyncRequests{};

    etl::vector<DaliDevice, MAX_TOTAL_DEVICES> m_scanScratchpad{};
    std::array<AddressMapBlobItem, MAX_TOTAL_DEVICES> m_nvsBlobScratchpad{};
    mutable std::mutex m_snifferMutex{};
    etl::vector<DeviceStateChangeEvent, MAX_TOTAL_DEVICES> m_snifferEventsScratchpad{};

    DeviceStateCallback m_deviceStateCb{nullptr};
    void* m_deviceStateCtx{nullptr};
    std::atomic<bool> m_scanCommissionActive{false};

    GroupStateCallback m_groupStateCb{nullptr};
    void* m_groupStateCtx{nullptr};

    InputEventCallback m_inputEventCb{nullptr};
    void* m_inputEventCtx{nullptr};

    DeviceAttributesCallback m_attributesCb{nullptr};
    void* m_attributesCtx{nullptr};

    TaskHandle_t m_pollTaskHandle{nullptr};
    uint8_t m_roundRobinIndex{0};
    bool m_nvsDirty{false};
    int64_t m_lastNvsDirtyTsMs{0};
};

} // namespace daliMQTT

#include "dali/DaliDeviceRegistry.icc"

#endif // DALIMQTT_DALIDEVICEREGISTRY_HXX