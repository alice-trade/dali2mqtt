#ifndef DALIMQTT_DALIDEVICECONTROLLER_HXX
#define DALIMQTT_DALIDEVICECONTROLLER_HXX

// #include <dalic/include/dali.h>


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
        [[nodiscard]] std::bitset<64> getDiscoveredDevices() const;

    private:
        DaliDeviceController() = default;

        void loadDeviceMask();
        void saveDeviceMask() const;
        void processDaliFrame(const dali_frame_t& frame);

        [[noreturn]] static void daliEventHandlerTask(void* pvParameters);
        [[noreturn]] static void daliSyncTask(void* pvParameters);

        std::bitset<64> m_discovered_devices;
        TaskHandle_t m_event_handler_task{nullptr};
        TaskHandle_t m_sync_task_handle{nullptr};
        mutable std::mutex m_devices_mutex;
    };

} // daliMQTT

#endif //DALIMQTT_DALIDEVICECONTROLLER_HXX