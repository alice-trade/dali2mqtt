#ifndef DALIMQTT_WIFI_HXX
#define DALIMQTT_WIFI_HXX

#include "esp_wifi.h"
#include <string>
#include <functional>
#include <atomic>

namespace daliMQTT
{
    class Wifi {
    public:
        enum class Status {
            DISCONNECTED,
            CONNECTING,
            CONNECTED,
            AP_MODE
        };

        Wifi(const Wifi&) = delete;
        Wifi& operator=(const Wifi&) = delete;

        static Wifi& getInstance();

        // Инициализация сетевого стека
        esp_err_t init();

        // Подключение к точке доступа
        esp_err_t connectToAP(const std::string& ssid, const std::string& password);

        // Запуск в режиме точки доступа
        esp_err_t startAP(const std::string& ssid, const std::string& password);

        // Отключение
        void disconnect();

        [[nodiscard]] Status getStatus() const { return status; }
        [[nodiscard]] std::string getIpAddress() const;

        // Callbacks
        std::function<void(void)> onConnected;
        std::function<void(void)> onDisconnected;

    private:
        Wifi() = default;

        static void wifiEventHandler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data);

        std::atomic<Status> status{Status::DISCONNECTED};
        bool initialized{false};
    };
} // daliMQTT

#endif //DALIMQTT_WIFI_HXX