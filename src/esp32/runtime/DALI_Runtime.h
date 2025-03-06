//
// Created by Danil on 25.02.2025.
//

#ifndef DALI_RUNTIME_H
#define DALI_RUNTIME_H

void dali_runtime_mqtt_command_handler_task(void *pvParameters);
void dali_runtime_status_polling_task(void *pvParameters);
bool dali_runtime_init();

#endif //DALI_RUNTIME_H