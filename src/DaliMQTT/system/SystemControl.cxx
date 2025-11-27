#include "SystemControl.hxx"
#include "ConfigManager.hxx"
#include <esp_ota_ops.h>

namespace daliMQTT {

    static constexpr char TAG[] = "SystemControl";
    static constexpr gpio_num_t BOOT_BUTTON_GPIO = GPIO_NUM_0;
    static constexpr uint32_t BUTTON_LONG_PRESS_MS = 5000;

    static TimerHandle_t g_button_timer = nullptr;

    static void ResetButtonTimerCallback(TimerHandle_t xTimer) {
        if (gpio_get_level(BOOT_BUTTON_GPIO) == 0) {
            ESP_LOGW(TAG, "BOOT button held for %lu seconds. Performing Factory Reset!", BUTTON_LONG_PRESS_MS/1000);
            
            ConfigManager::getInstance().resetConfiguredFlag();
            
            ESP_LOGW(TAG, "Rebooting system...");
            vTaskDelay(pdMS_TO_TICKS(500));
            esp_restart();
        }
    }

    static void IRAM_ATTR gpioResetConfBtnIsrHandler(void* arg) {
        BaseType_t xHigherPriorityTaskWoken = pdFALSE;
        
        if (gpio_get_level(BOOT_BUTTON_GPIO) == 0) {
            if (g_button_timer) {
                xTimerStartFromISR(g_button_timer, &xHigherPriorityTaskWoken);
            }
        } else {
            if (g_button_timer) {
                xTimerStopFromISR(g_button_timer, &xHigherPriorityTaskWoken);
            }
        }

        if (xHigherPriorityTaskWoken) {
            #ifndef traceISR_EXIT_TO_SCHEDULER
            #define traceISR_EXIT_TO_SCHEDULER()
            #endif
            portYIELD_FROM_ISR();
        }
    }

    void SystemControl::checkOtaValidation() {
        const esp_partition_t *running = esp_ota_get_running_partition();
        esp_ota_img_states_t ota_state;
        if (esp_ota_get_state_partition(running, &ota_state) == ESP_OK) {
            if (ota_state == ESP_OTA_IMG_PENDING_VERIFY) {
                ESP_LOGI(TAG, "OTA Update detected. Marking firmware as valid.");
                esp_ota_mark_app_valid_cancel_rollback();
            }
        }
    }

    void SystemControl::startResetConfigurationButtonMonitor() {
        if (g_button_timer != nullptr) {
            ESP_LOGW(TAG, "Button monitor already started.");
            return;
        }

        g_button_timer = xTimerCreate(
            "reset_conf_btn_reset_tmr",
            pdMS_TO_TICKS(BUTTON_LONG_PRESS_MS), 
            pdFALSE,
            nullptr, 
            ResetButtonTimerCallback
        );

        if (!g_button_timer) {
            ESP_LOGE(TAG, "Failed to create FreeRTOS timer for button.");
            return;
        }

        gpio_config_t io_conf = {};
        io_conf.intr_type = GPIO_INTR_ANYEDGE;
        io_conf.mode = GPIO_MODE_INPUT;
        io_conf.pin_bit_mask = (1ULL << BOOT_BUTTON_GPIO);
        io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
        io_conf.pull_up_en = GPIO_PULLUP_ENABLE;
        gpio_config(&io_conf);

        gpio_install_isr_service(ESP_INTR_FLAG_IRAM);
        gpio_isr_handler_add(BOOT_BUTTON_GPIO, gpioResetConfBtnIsrHandler, nullptr);

        ESP_LOGI(TAG, "Boot button (GPIO %d) monitor started (Long press > %lums resets config).", BOOT_BUTTON_GPIO, BUTTON_LONG_PRESS_MS);
    }
}