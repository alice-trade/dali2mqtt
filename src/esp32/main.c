// main entry point daliMQTT for modules
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_event.h" // Для esp_event_loop_create_default
#include "esp_system.h" // Для esp_restart

#ifndef DALI2MQTT_PROJECT_VERSION
#define DALI2MQTT_PROJECT_VERSION "Development"
#endif

#include "definitions.h"
#include "wifi_manager.h"
#include "mqtt.h"
#include "dali_interface.h"
#include "config.h"

static const char *TAG = "MAIN";

// --- Глобальные переменные (определения) ---
app_config_t g_app_config;


// Обертка для ESP_ERROR_CHECK для более информативного вывода и возможного действия
// C23 [[nodiscard]] можно добавить, если компилятор поддерживает, но для ESP-IDF это пока редкость
static esp_err_t check_init_step(esp_err_t err, const char *step_name) {
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize %s: %s (0x%x)", step_name, esp_err_to_name(err), err);
    } else {
        ESP_LOGI(TAG, "%s initialized successfully.", step_name);
    }
    return err;
}

void app_main(void) {
    ESP_LOGI(TAG, "===================================");
    ESP_LOGI(TAG, "    Starting DALI-MQTT Bridge");
    ESP_LOGI(TAG, "Firmware Version: %s", (DALI2MQTT_PROJECT_VERSION)); // Пример: если добавили версию в Kconfig
    ESP_LOGI(TAG, "===================================");

    esp_err_t ret;

    // 1. Инициализация NVS
    ESP_LOGI(TAG, "Initializing NVS...");
    ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS partition was truncated or needs formatting, erasing...");
        if (check_init_step(nvs_flash_erase(), "NVS Erase") != ESP_OK) {
            // Критическая ошибка, если не можем стереть NVS
            ESP_LOGE(TAG, "Halting due to NVS erase failure.");
            goto halt_system; // Или esp_restart() через некоторое время
        }
        ret = nvs_flash_init();
    }
    if (check_init_step(ret, "NVS") != ESP_OK) {
        ESP_LOGE(TAG, "Halting due to NVS init failure.");
        goto halt_system;
    }

    // 2. Загрузка/инициализация конфигурации
    ESP_LOGI(TAG, "Loading configuration...");
    if (check_init_step(config_manager_init(), "Configuration Manager") != ESP_OK) {
        ESP_LOGW(TAG, "Continuing with default/partial configuration.");
        // Не критично, если не загрузились DALI параметры из NVS, используем дефолты
    }

    // 3. Инициализация WiFi
    ESP_LOGI(TAG, "Initializing WiFi...");
    if (check_init_step(wifi_manager_init(), "WiFi Manager") != ESP_OK) {
        ESP_LOGE(TAG, "Halting due to WiFi Manager init failure.");
        goto halt_system;
    }

    ESP_LOGI(TAG, "Connecting to WiFi (SSID: %s)...", g_app_config.wifi_ssid);
    if (check_init_step(wifi_manager_connect(), "WiFi Connection") != ESP_OK) {
        ESP_LOGE(TAG, "Failed to connect to WiFi. System will attempt to reconnect or may require reset.");
        // Здесь можно запустить задачу, которая будет периодически пытаться переподключиться,
        // или просто позволить системе "зависнуть" в ожидании WiFi, если это допустимо.
        // Вместо while(1) можно, например, уйти в глубокий сон и проснуться через время.
        // Для простоты пока оставим возможность "зависания" или перезагрузки.
        ESP_LOGE(TAG, "Consider implementing a WiFi reconnect strategy or a configuration portal.");
        goto halt_system; // Или esp_restart();
    }

    // 4. Инициализация DALI интерфейса
    ESP_LOGI(TAG, "Initializing DALI Interface (RX:%d, TX:%d)...", CONFIG_DALI2MQTT_DALI_RX_PIN, CONFIG_DALI2MQTT_DALI_TX_PIN);
    if (check_init_step(dali_interface_init(), "DALI Interface") != ESP_OK) {
        ESP_LOGE(TAG, "Halting due to DALI Interface init failure.");
        goto halt_system;
    }

    // 5. Инициализация MQTT клиента
    ESP_LOGI(TAG, "Initializing MQTT Client (URI: %s)...", g_app_config.mqtt_uri);
    if (check_init_step(mqtt_manager_init(), "MQTT Manager") != ESP_OK) {
        ESP_LOGE(TAG, "Halting due to MQTT Manager init failure.");
        goto halt_system;
    }

    ESP_LOGI(TAG, "Starting MQTT client...");
    if (check_init_step(mqtt_manager_start(), "MQTT Client Start") != ESP_OK) {
        ESP_LOGE(TAG, "Halting due to MQTT Client start failure.");
        goto halt_system;
    }

    // 6. Запуск сервиса опроса DALI
    // Таймер запускается внутри dali_interface_start_polling
    ESP_LOGI(TAG, "Starting DALI polling service...");
    if (check_init_step(dali_interface_start_polling(), "DALI Polling Service") != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start DALI polling. Functionality will be limited.");
        // Не обязательно останавливать всё, но логирование статусов не будет работать
    }

    ESP_LOGI(TAG, "===================================");
    ESP_LOGI(TAG, " Initialization complete. System running.");
    ESP_LOGI(TAG, "===================================");


halt_system:
    ESP_LOGE(TAG, "System initialization failed. Halting or restarting...");
    // Здесь можно добавить логику перезагрузки через N секунд или переход в безопасный режим
    for (int i = 10; i > 0; i--) {
        ESP_LOGE(TAG, "Restarting in %d seconds...", i);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
    esp_restart();
}