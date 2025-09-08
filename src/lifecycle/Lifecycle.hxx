#ifndef DALIMQTT_LIFECYCLE_HXX
#define DALIMQTT_LIFECYCLE_HXX
#include <string>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace daliMQTT
{
    class Lifecycle {
        public:
            Lifecycle(const Lifecycle&) = delete;
            Lifecycle& operator=(const Lifecycle&) = delete;

            static Lifecycle& getInstance();

            // Запуск в режиме начальной настройки
            void startProvisioningMode();

            // Запуск в нормальном режиме работы
            void startNormalMode();

        private:
            Lifecycle() = default;

            void setupAndRunMqtt();
            void onMqttConnected();
            void onMqttData(const std::string& topic, const std::string& data);

            [[noreturn]] static void daliPollTask(void* pvParameters);
            TaskHandle_t dali_poll_task_handle{nullptr};
    };
} // daliMQTT

#endif //DALIMQTT_LIFECYCLE_HXX