#ifndef DALIMQTT_LIFECYCLE_HXX
#define DALIMQTT_LIFECYCLE_HXX
#include <string>

namespace daliMQTT
{
    class Lifecycle {
        public:
            Lifecycle(const Lifecycle&) = delete;
            Lifecycle& operator=(const Lifecycle&) = delete;

            static Lifecycle& getInstance() {
                static Lifecycle instance;
                return instance;
            }

            // Запуск в режиме начальной настройки
            void startProvisioningMode();

            // Запуск в нормальном режиме работы
            void startNormalMode();

        private:
            Lifecycle() = default;

            void setupAndRunMqtt();
            void onMqttConnected();
            void onMqttData(const std::string& topic, const std::string& data);
    };
} // daliMQTT

#endif //DALIMQTT_LIFECYCLE_HXX