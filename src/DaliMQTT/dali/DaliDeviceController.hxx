#ifndef DALIMQTT_DALIDEVICECONTROLLER_HXX
#define DALIMQTT_DALIDEVICECONTROLLER_HXX

#include "DaliAPI.hxx"

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

        void init();
        /**
         * @brief Starts all DALI background tasks: event monitoring and periodic sync.
         */
        void start();

        std::bitset<64> performFullInitialization();
        std::bitset<64> performScan();

        [[nodiscard]] std::map<DaliLongAddress_t, DaliDevice> getDevices() const;
        [[nodiscard]] std::optional<uint8_t> getShortAddress(DaliLongAddress_t longAddress) const;
        [[nodiscard]] std::optional<DaliLongAddress_t> getLongAddress(uint8_t shortAddress) const;

        void updateDeviceState(DaliLongAddress_t longAddr, uint8_t level, std::optional<bool> lamp_failure = std::nullopt);
        void publishAttributes(DaliLongAddress_t longAddr);
        [[nodiscard]] std::optional<uint8_t> getLastLevel(DaliLongAddress_t longAddress) const;
        void requestDeviceSync(uint8_t shortAddress, uint32_t delay_ms = 0);

    private:
        DaliDeviceController() = default;

        void SnifferProcessFrame(const dali_frame_t& frame);
        void ProcessInputDeviceFrame(const dali_frame_t& frame) const;

        std::bitset<64> discoverAndMapDevices();
        bool validateAddressMap();
        void pollSingleDevice(uint8_t shortAddr);

        [[noreturn]] static void daliEventHandlerTask(void* pvParameters);
        [[noreturn]] static void daliSyncTask(void* pvParameters);

        static void publishState(DaliLongAddress_t long_addr, uint8_t level, bool lamp_failure);
        static void publishAvailability(DaliLongAddress_t long_addr, bool is_available);

        TaskHandle_t m_event_handler_task{nullptr};
        TaskHandle_t m_sync_task_handle{nullptr};

        std::map<DaliLongAddress_t, DaliDevice> m_devices;
        std::map<uint8_t, DaliLongAddress_t> m_short_to_long_map;
        mutable std::mutex m_devices_mutex;

        std::vector<DeferredRequest> m_deferred_requests;
        std::vector<uint8_t> m_priority_queue;
        std::set<uint8_t> m_priority_set;
        mutable std::mutex m_queue_mutex;
        uint8_t m_round_robin_index{0};
    };

} // daliMQTT

#endif //DALIMQTT_DALIDEVICECONTROLLER_HXX