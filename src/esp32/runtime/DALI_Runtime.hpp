//
// Created by Danil on 25.02.2025.
//

#ifndef DALI_RUNTIME_HPP
#define DALI_RUNTIME_HPP

namespace DALI {
    void mqttCommandHandlerTask(void *pvParameters);
    void statusPollingTask(void *pvParameters);
    bool init();
}
#endif //DALI_RUNTIME_HPP
