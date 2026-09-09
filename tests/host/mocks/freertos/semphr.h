//  Copyright (c) 2026 Alice-Trade Inc.
//  SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "freertos/FreeRTOS.h"

#ifdef __cplusplus
extern "C" {
#endif

SemaphoreHandle_t xSemaphoreCreateRecursiveMutex(void);
BaseType_t xSemaphoreTakeRecursive(SemaphoreHandle_t xMutex, TickType_t xBlockTime);
BaseType_t xSemaphoreGiveRecursive(SemaphoreHandle_t xMutex);
void vSemaphoreDelete(SemaphoreHandle_t xMutex);

#ifdef __cplusplus
}
#endif