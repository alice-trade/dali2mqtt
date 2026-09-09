//  Copyright (c) 2026 Alice-Trade Inc.
//  SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include <stdint.h>

typedef enum {
    GPIO_NUM_NC = -1,
    GPIO_NUM_0 = 0,
    GPIO_NUM_1 = 1,
    GPIO_NUM_2 = 2,
    GPIO_NUM_3 = 3,
    GPIO_NUM_4 = 4,
    GPIO_NUM_5 = 5,
    GPIO_NUM_16 = 16,
    GPIO_NUM_17 = 17,
    GPIO_NUM_MAX
} gpio_num_t;
#ifdef __cplusplus
extern "C" {
#endif
inline int gpio_get_level(gpio_num_t gpio_num) { (void)gpio_num; return 0; }
#ifdef __cplusplus
}
#endif