//  Copyright (c) 2026 Alice-Trade Inc.
//  SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "freertos/FreeRTOS.h"

#ifdef __cplusplus
extern "C" {
#endif

TaskHandle_t xTaskGetCurrentTaskHandle(void);
void vTaskDelay(TickType_t ticks);
void vTaskDelete(TaskHandle_t xTask);

BaseType_t xTaskCreate(void (*pxTaskCode)(void *), const char * const pcName,
                       const uint32_t usStackDepth, void * const pvParameters,
                       UBaseType_t uxPriority, TaskHandle_t * const pxCreatedTask);

BaseType_t xTaskNotifyWaitIndexed(UBaseType_t uxIndexToWaitOn, uint32_t ulBitsToClearOnEntry,
                                  uint32_t ulBitsToClearOnExit, uint32_t *pulNotificationValue,
                                  TickType_t xTicksToWait);

BaseType_t xTaskNotifyIndexed(TaskHandle_t xTaskToNotify, UBaseType_t uxIndexToNotify,
                              uint32_t ulValue, eNotifyAction eAction);

BaseType_t xTaskNotify(TaskHandle_t xTaskToNotify, uint32_t ulValue, eNotifyAction eAction);
BaseType_t xTaskNotifyWait(uint32_t ulBitsToClearOnEntry, uint32_t ulBitsToClearOnExit,
                           uint32_t *pulNotificationValue, TickType_t xTicksToWait);

void xTaskNotifyStateClearIndexed(TaskHandle_t xTask, UBaseType_t uxIndexToClear);
BaseType_t xTaskNotifyFromISR(TaskHandle_t xTaskToNotify, uint32_t ulValue, eNotifyAction eAction, BaseType_t *pxHigherPriorityTaskWoken);
#ifdef __cplusplus
}
#endif