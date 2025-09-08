#include "Lifecycle.hxx"
#include "esp_log.h"


namespace daliMQTT
{
    static inline auto TAG = "LifecycleBase";

    Lifecycle& Lifecycle::getInstance() {
        static Lifecycle instance;
        return instance;
    }
} // daliMQTT