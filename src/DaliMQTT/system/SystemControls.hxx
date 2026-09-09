// Copyright (c) 2026 Alice-Trade Inc.
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef DALIMQTT_SYSTEMCONTROLS_HXX
#define DALIMQTT_SYSTEMCONTROLS_HXX

#include <driver/gpio.h>
#include <freertos/FreeRTOS.h>
#include <freertos/timers.h>

namespace daliMQTT {

using ResetActionCallback = void (*)(void* userCtx);

class SystemControls {
  public:
    SystemControls() = default;
    ~SystemControls();

    SystemControls(const SystemControls&) = delete;
    SystemControls& operator=(const SystemControls&) = delete;

    esp_err_t init(gpio_num_t bootButtonPin = GPIO_NUM_0);
    static void checkAndValidateOta();
    void setResetCallback(ResetActionCallback cb, void* ctx) noexcept;

  private:
    static void onButtonHeldTimer(TimerHandle_t xTimer);
    static void IRAM_ATTR gpioButtonIsr(void* arg);

    gpio_num_t m_buttonPin{GPIO_NUM_0};
    TimerHandle_t m_resetTimer{nullptr};

    ResetActionCallback m_resetCb{nullptr};
    void* m_resetCtx{nullptr};
};

} // namespace daliMQTT

#endif // DALIMQTT_SYSTEMCONTROLS_HXX