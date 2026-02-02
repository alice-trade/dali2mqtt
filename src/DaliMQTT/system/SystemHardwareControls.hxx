#ifndef DALIMQTT_SYSTEMHARDWARECONTROLS_HXX
#define DALIMQTT_SYSTEMHARDWARECONTROLS_HXX

namespace daliMQTT
{
    class SystemHardwareControls {
        public:
            SystemHardwareControls() = delete;

            /**
             * @brief Check OTA Validation and set it
             */
            static void checkOtaValidation();

            /**
             * @brief Reconfiguration button monitor
             */
            static void startResetConfigurationButtonMonitor();
    };
}

#endif //DALIMQTT_SYSTEMHARDWARECONTROLS_HXX