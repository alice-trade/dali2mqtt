// Copyright (c) 2026 Alice-Trade Inc.
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef DALIMQTT_DALIDEVICECONTROLLER_HXX
#define DALIMQTT_DALIDEVICECONTROLLER_HXX

#include "dali/DaliAdapter.hxx"

namespace daliMQTT
{
    class DaliDeviceController {
    public:
        DaliDeviceController(const DaliDeviceController&) = delete;
        DaliDeviceController& operator=(const DaliDeviceController&) = delete;

        static DaliDeviceController& Instance() {
            static DaliDeviceController instance;
            return instance;
        }

        void init();

        void start();

        void applyBusConfiguration();

        DaliAdapter* getAdapter(uint8_t bus_id) const;

        /**
         * @brief Performs full bus initialization (addressing).
         */
        std::bitset<64> performFullInitialization();

        /**
         * @brief Performs initialization for 24-bit input devices.
         */
        std::bitset<64> perform24BitDeviceInitialization();

        /**
         * @brief Scans the bus for existing devices without re-addressing.
         */
        std::bitset<64> performScan();

        [[nodiscard]] std::vector<DaliDevice> getDevices() const;

        [[nodiscard]] std::optional<DaliInternalAddr> getInternalAddress(DaliLongAddress_t longAddress) const;

        [[nodiscard]] std::optional<DaliLongAddress_t> getLongAddress(DaliInternalAddr internalAddr, bool is_input_device = false) const;


        /**
         * @brief Updates the state of a device in the cache and publishes to MQTT.
         */
         void updateDeviceState(const DaliLongAddress_t longAddr, const DaliPublishState& state) {
            std::lock_guard<std::mutex> lock(m_devices_mutex);
            procUpdateDeviceState(longAddr, state);
        }

        /**
         * @brief Publishes device attributes (extended info) to MQTT.
         */
        void publishAttributes(DaliLongAddress_t longAddr) const;

        [[nodiscard]] std::optional<uint8_t> getLastLevel(DaliLongAddress_t longAddress) const;

        /**
         * @brief Requests a sync (poll) for a specific device.
         */
        void requestDeviceSync(DaliInternalAddr internalAddress, uint32_t delay_ms = 0);

        /**
         * @brief Requests a broadcast sync for all devices with staggered delay.
         */
        void requestBroadcastSync(uint32_t base_delay_ms, uint32_t stagger_ms);

        void scanAllActiveBuses();

    private:
        DaliDeviceController() = default;
        std::array<std::unique_ptr<DaliAdapter>, Constants::MaxBuses> m_adapters{};
        QueueHandle_t m_central_event_queue{};

        void SnifferProcessFrame(const dali_frame_t& frame);
        void ProcessInputDeviceFrame(const dali_frame_t& frame) const;
        bool validateAddressMap();
        std::bitset<64> discoverAndMapDevices(uint8_t bus_id);
        void pollSingleDevice(DaliInternalAddr internalAddr);

        struct ColorPollResult {
            std::optional<uint16_t> tc;
            std::optional<DaliRGB> rgb;
        };

        std::optional<uint8_t> pollAvailabilityAndLevel(DaliInternalAddr internalAddr, DaliLongAddress_t longAddr);
        void checkDT8Features(DaliInternalAddr internalAddr, DaliLongAddress_t longAddr);
        ColorPollResult pollColorDataCyclic(DaliInternalAddr internalAddr, DaliLongAddress_t longAddr, uint8_t current_level);
        void performInitialGroupSync(DaliLongAddress_t longAddr, uint8_t level, const ColorPollResult& colorData);
        void initialStaticDataFetch(DaliInternalAddr internalAddr, DaliLongAddress_t longAddr);
        void procUpdateDeviceState(DaliLongAddress_t longAddr, const DaliPublishState& state);

        [[noreturn]] static void daliEventHandlerTask(void* pvParameters);
        [[noreturn]] static void daliSyncTask(void* pvParameters);

        void publishState(const DaliLongAddress_t long_addr, const ControlGear& device) const;
        static void publishAvailability(DaliLongAddress_t long_addr, bool is_available);
        [[nodiscard]] std::optional<DaliLongAddress_t> getInputDeviceLongAddress(DaliInternalAddr internalAddress) const;

        TaskHandle_t m_event_handler_task{nullptr};
        TaskHandle_t m_sync_task_handle{nullptr};

        std::vector<DaliDevice> m_devices{};
        std::array<DaliLongAddress_t, Constants::MaxBuses * 256> m_internal_to_long_map{};
        mutable std::mutex m_devices_mutex{};

        std::vector<DeferredRequest> m_deferred_requests{};
        std::vector<DaliInternalAddr> m_priority_queue{};
        std::set<DaliInternalAddr> m_priority_set{};
        mutable std::mutex m_queue_mutex{};
        uint8_t m_round_robin_index{0};
        bool m_nvs_dirty{false};
        int64_t m_last_nvs_change_ts{0};
    };

} // daliMQTT

#endif //DALIMQTT_DALIDEVICECONTROLLER_HXX