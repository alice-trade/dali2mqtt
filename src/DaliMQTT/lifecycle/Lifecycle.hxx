#ifndef DALIMQTT_LIFECYCLE_HXX
#define DALIMQTT_LIFECYCLE_HXX

namespace daliMQTT
{
    class Lifecycle {
        public:
            Lifecycle(const Lifecycle&) = delete;
            Lifecycle& operator=(const Lifecycle&) = delete;
            Lifecycle() = delete;

            // Запуск в режиме начальной настройки
            static void startProvisioningMode();

            // Запуск в нормальном режиме работы
            static void startNormalMode();

        private:
            static void onMqttConnected();
    };
} // daliMQTT

#endif //DALIMQTT_LIFECYCLE_HXX