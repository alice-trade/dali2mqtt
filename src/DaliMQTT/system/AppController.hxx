#ifndef DALIMQTT_APPCONTROLLER_HXX
#define DALIMQTT_APPCONTROLLER_HXX

namespace daliMQTT
{
    class AppController {
        public:
            AppController(const AppController&) = delete;
            AppController& operator=(const AppController&) = delete;

            static AppController& Instance() {
                static AppController instance;
                return instance;
            }

            void startProvisioningMode();
            void startNormalMode();

            void onConfigReloadRequest();
            void publishHAMqttDiscovery() const;


        private:
            AppController() = default;

            void initDaliSubsystem();
            void initNetworkSubsystem();

            // Callbacks
            void onNetworkConnected();
            void onNetworkDisconnected();
            void onMqttConnected();
            void onMqttDisconnected();

            std::atomic<bool> m_network_connected{false};
            std::atomic<bool> m_mqtt_connected{false};
    };
} // daliMQTT

#endif //DALIMQTT_APPCONTROLLER_HXX