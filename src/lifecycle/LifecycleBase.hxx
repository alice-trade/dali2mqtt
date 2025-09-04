#ifndef DALIMQTT_LIFECYCLEBASE_HXX
#define DALIMQTT_LIFECYCLEBASE_HXX
#include <string>

namespace daliMQTT
{
    class LifecycleBase {
    public:
        LifecycleBase(const LifecycleBase&) = delete;
        LifecycleBase& operator=(const LifecycleBase&) = delete;

        static LifecycleBase& getInstance();

        // Запуск в режиме начальной настройки
        void startProvisioningMode();

        // Запуск в нормальном режиме работы
        void startNormalMode();

    private:
        LifecycleBase() = default;

        void setupAndRunMqtt();
        void onMqttConnected();
        void onMqttData(const std::string& topic, const std::string& data);

        static void daliPollTask(void* pvParameters);
    };
} // daliMQTT

#endif //DALIMQTT_LIFECYCLEBASE_HXX