#ifndef DALIMQTT_DALIDEVICECONTROLLER_HXX
#define DALIMQTT_DALIDEVICECONTROLLER_HXX

#include <bitset>
#include <vector>
#include <mutex>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

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
        void startPolling();

        std::bitset<64> performFullInitialization();
        std::bitset<64> performScan();
        [[nodiscard]] std::bitset<64> getDiscoveredDevices() const;

    private:
        DaliDeviceController() = default;

        void loadDeviceMask();
        void saveDeviceMask();

        [[noreturn]] static void daliPollTask(void* pvParameters);

        std::bitset<64> m_discovered_devices;
        TaskHandle_t m_poll_task_handle{nullptr};
        mutable std::mutex m_devices_mutex;
    };

} // daliMQTT

#endif //DALIMQTT_DALIDEVICECONTROLLER_HXX