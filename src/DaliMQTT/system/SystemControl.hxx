#ifndef DALIMQTT_SYSTEMCONTROL_HXX
#define DALIMQTT_SYSTEMCONTROL_HXX

namespace daliMQTT
{
    class SystemControl {
        public:
            SystemControl() = delete;

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

#endif //DALIMQTT_SYSTEMCONTROL_HXX