#ifndef DALIMQTT_DALIDEVICECONTROLLER_HXX
#define DALIMQTT_DALIDEVICECONTROLLER_HXX

#include "dali/DaliAdapter.hxx"

namespace daliMQTT
{
    class DaliDeviceController {
    public:
        DaliDeviceController(const DaliDeviceController&) = delete;
        DaliDeviceController& operator=(const DaliDeviceController&) = delete;

        static DaliDeviceController& getInstance() {
            static DaliDeviceController instance;
            return instance;
        }

        /**
         * @brief Initializes the controller and loads the address map.
         */
        void init();

        /**
         * @brief Starts all DALI background tasks: event monitoring and periodic sync.
         */
        void start();

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

        /**
         * @brief Returns the map of known devices.
         */
        [[nodiscard]] std::map<DaliLongAddress_t, DaliDevice> getDevices() const;
        [[nodiscard]] std::optional<uint8_t> getShortAddress(DaliLongAddress_t longAddress) const;
        [[nodiscard]] std::optional<DaliLongAddress_t> getLongAddress(uint8_t shortAddress, bool is24bitSpace = false) const;

        /**
         * @brief Updates the state of a device in the cache and publishes to MQTT.
         */
        void updateDeviceState(DaliLongAddress_t longAddr, const DaliPublishState& state);

        /**
         * @brief Publishes device attributes (extended info) to MQTT.
         */
        void publishAttributes(DaliLongAddress_t longAddr) const;

        [[nodiscard]] std::optional<uint8_t> getLastLevel(DaliLongAddress_t longAddress) const;

        /**
         * @brief Requests a sync (poll) for a specific device.
         */
        void requestDeviceSync(uint8_t shortAddress, uint32_t delay_ms = 0);

        /**
         * @brief Requests a broadcast sync for all devices with staggered delay.
         */
        void requestBroadcastSync(uint32_t base_delay_ms, uint32_t stagger_ms);


    private:
        DaliDeviceController() = default;

        void SnifferProcessFrame(const dali_frame_t& frame);
        void ProcessInputDeviceFrame(const dali_frame_t& frame) const;

        std::bitset<64> discoverAndMapDevices();
        bool validateAddressMap();
        void pollSingleDevice(uint8_t shortAddr);

        [[noreturn]] static void daliEventHandlerTask(void* pvParameters);
        [[noreturn]] static void daliSyncTask(void* pvParameters);

        static void publishState(DaliLongAddress_t long_addr, const DaliDevice& device);
        static void publishAvailability(DaliLongAddress_t long_addr, bool is_available);
        [[nodiscard]] std::optional<DaliLongAddress_t> getInputDeviceLongAddress(uint8_t shortAddress) const;

        TaskHandle_t m_event_handler_task{nullptr};
        TaskHandle_t m_sync_task_handle{nullptr};

        std::map<DaliLongAddress_t, DaliDevice> m_devices{};
        std::map<uint8_t, DaliLongAddress_t> m_short_to_long_map{};
        mutable std::mutex m_devices_mutex{};

        std::vector<DeferredRequest> m_deferred_requests{};
        std::vector<uint8_t> m_priority_queue{};
        std::set<uint8_t> m_priority_set{};
        mutable std::mutex m_queue_mutex{};
        uint8_t m_round_robin_index{0};
        bool m_nvs_dirty{false};
        int64_t m_last_nvs_change_ts{0};
    };

} // daliMQTT

#endif //DALIMQTT_DALIDEVICECONTROLLER_HXX