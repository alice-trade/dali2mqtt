//  Copyright (c) 2026 Alice-Trade Inc.
//  SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "freertos/FreeRTOS.h"

#ifdef __cplusplus
extern "C" {
#endif

QueueHandle_t xQueueCreate(uint32_t uxQueueLength, uint32_t uxItemSize);
BaseType_t xQueueSend(QueueHandle_t xQueue, const void* pvItemToQueue, TickType_t xTicksToWait);
BaseType_t xQueueReceive(QueueHandle_t xQueue, void* pvBuffer, TickType_t xTicksToWait);
BaseType_t xQueueOverwrite(QueueHandle_t xQueue, const void * pvItemToQueue);
BaseType_t xQueueReset(QueueHandle_t xQueue);
void vQueueDelete(QueueHandle_t xQueue);
UBaseType_t uxQueueMessagesWaiting(QueueHandle_t xQueue);

#ifdef __cplusplus
}
#endif