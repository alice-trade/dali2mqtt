// Copyright (c) 2026 Alice-Trade Inc.
// SPDX-License-Identifier: GPL-3.0-or-later

#include "system/SystemControls.hxx"
#include <esp_log.h>
#include <esp_ota_ops.h>

namespace daliMQTT {

static constexpr char TAG[] = "SystemControls";
static constexpr uint32_t BUTTON_LONG_PRESS_MS = 5000;

SystemControls::~SystemControls() {
    if (m_resetTimer) {
        xTimerDelete(m_resetTimer, 0);
    }
}

esp_err_t SystemControls::init(const gpio_num_t bootButtonPin) {
    m_buttonPin = bootButtonPin;

    m_resetTimer = xTimerCreate("btn_poll_tmr", pdMS_TO_TICKS(100), pdTRUE, this, onButtonHeldTimer);
    if (!m_resetTimer) {
        return ESP_ERR_NO_MEM;
    }

    gpio_config_t ioConf{};
    ioConf.intr_type = GPIO_INTR_NEGEDGE;
    ioConf.mode = GPIO_MODE_INPUT;
    ioConf.pin_bit_mask = (1ULL << m_buttonPin);
    ioConf.pull_up_en = GPIO_PULLUP_ENABLE;
    ioConf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    gpio_config(&ioConf);

    esp_err_t err = gpio_install_isr_service(0);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        return err;
    }
    gpio_isr_handler_add(m_buttonPin, gpioButtonIsr, this);

    ESP_LOGI(TAG, "Hardware Reset Button initialized on GPIO %d (Hold 5s to reset)", m_buttonPin);
    return ESP_OK;
}

void SystemControls::checkAndValidateOta() {
    const esp_partition_t* running = esp_ota_get_running_partition();
    esp_ota_img_states_t otaState;
    if (esp_ota_get_state_partition(running, &otaState) == ESP_OK) {
        if (otaState == ESP_OTA_IMG_PENDING_VERIFY) {
            ESP_LOGI(TAG, "OTA self-test passed on partition '%s'. Confirming firmware...", running->label);
            esp_ota_mark_app_valid_cancel_rollback();
        }
    }
}

void SystemControls::setResetCallback(ResetActionCallback cb, void* ctx) noexcept {
    m_resetCb = cb;
    m_resetCtx = ctx;
}

void SystemControls::onButtonHeldTimer(TimerHandle_t xTimer) {
    const auto* self = static_cast<SystemControls*>(pvTimerGetTimerID(xTimer));
    if (!self) return;

    if (gpio_get_level(self->m_buttonPin) == 0) {
        const uint32_t nowMs = static_cast<uint32_t>(esp_timer_get_time() / 1000);
        const uint32_t heldDurationMs = nowMs - self->m_lastPressTsMs;

        if (heldDurationMs >= BUTTON_LONG_PRESS_MS) {
            xTimerStop(self->m_resetTimer, 0);
            ESP_LOGW(TAG, "BOOT Button held for 5 seconds. Factory Reset triggered!");
            if (self->m_resetCb) {
                self->m_resetCb(self->m_resetCtx);
            }
        }
    } else {
        xTimerStop(self->m_resetTimer, 0);
    }
}

void IRAM_ATTR SystemControls::gpioButtonIsr(void* arg) {
    auto* self = static_cast<SystemControls*>(arg);
    const uint32_t nowMs = static_cast<uint32_t>(esp_timer_get_time() / 1000);

    if (nowMs - self->m_lastPressTsMs > 250) {
        self->m_lastPressTsMs = nowMs;
        BaseType_t highTaskWoken = pdFALSE;
        xTimerStartFromISR(self->m_resetTimer, &highTaskWoken);
#ifndef traceISR_EXIT_TO_SCHEDULER
#define traceISR_EXIT_TO_SCHEDULER()
#endif
        if (highTaskWoken) {
            portYIELD_FROM_ISR();
        }
    }
}

} // namespace daliMQTT