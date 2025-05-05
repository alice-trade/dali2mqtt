// main entry point daliMQTT for modules
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_event.h" // Для esp_event_loop_create_default

#include "definitions.h"
#include "wifi_manager.h"
#include "mqtt.h"
#include "dali_interface.h"
#include "config.h"

static const char *TAG = "MAIN";

// --- Глобальные переменные (определения) ---
app_config_t g_app_config;



void app_main(void)
{
    ESP_LOGI(TAG, "===================================");
    ESP_LOGI(TAG, "    Starting DALI-MQTT Bridge");
    ESP_LOGI(TAG, "===================================");

    // 1. Инициализация NVS
    ESP_LOGI(TAG, "[1/6] Initializing NVS...");
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
      ESP_LOGW(TAG, "NVS partition was truncated or needs formatting, erasing...");
      ESP_ERROR_CHECK(nvs_flash_erase());
      ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    ESP_LOGI(TAG, "NVS Initialized.");

    // 2. Загрузка конфигурации
    ESP_LOGI(TAG, "[2/6] Loading configuration...");
    ret = config_manager_init();
    if (ret != ESP_OK) {
         ESP_LOGW(TAG, "Failed to load/init config, using defaults where possible (%s)", esp_err_to_name(ret));
    } else {
        ESP_LOGI(TAG, "Configuration loaded.");
    }

    // 3. Инициализация и подключение WiFi
    ESP_LOGI(TAG, "[3/6] Initializing WiFi...");
    ESP_ERROR_CHECK(wifi_manager_init());
    ESP_LOGI(TAG, "[4/6] Connecting to WiFi (SSID: %s)...", g_app_config.wifi_ssid);
    ret = wifi_manager_connect();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to connect to WiFi. Halting execution.");
        while(1) { vTaskDelay(pdMS_TO_TICKS(5000)); }
    }
    ESP_LOGI(TAG, "WiFi Connected.");

    // 4. Инициализация DALI интерфейса (включая задачу и таймер)
    ESP_LOGI(TAG, "[5/6] Initializing DALI Interface (RX:%d, TX:%d)...", CONFIG_DALI2MQTT_DALI_RX_PIN, CONFIG_DALI2MQTT_DALI_TX_PIN);
    ret = dali_interface_init(); // Эта функция теперь создает задачу и таймер
    ESP_ERROR_CHECK(ret);
    ESP_LOGI(TAG, "DALI Interface Initialized.");


    // 5. Инициализация и запуск MQTT клиента
    ESP_LOGI(TAG, "[6/6] Initializing & Starting MQTT Client (URI: %s)...", g_app_config.mqtt_uri);
    ESP_ERROR_CHECK(mqtt_manager_init());
    ret = mqtt_manager_start();
    ESP_ERROR_CHECK(ret);
    ESP_LOGI(TAG, "MQTT Client Started (connecting...).");

    // Запуск опроса DALI происходит теперь из MQTT callback'а MQTT_EVENT_CONNECTED,
    // чтобы убедиться, что MQTT готов к приему статусов.
    // Вызов dali_interface_start_polling() перенесен туда неявно
    // через уведомление задачи опроса dali_poll_task.
    // Но сам таймер нужно запустить один раз. Сделаем это здесь,
    // хотя логичнее было бы после MQTT_EVENT_CONNECTED.
    // Или, как вариант, dali_interface_start_polling() можно вызывать
    // из MQTT callback'а CONNECTED.
    // Давайте вызовем start_polling здесь. Первый опрос произойдет
    // после первого срабатывания таймера.
    ESP_LOGI(TAG, "Starting DALI polling service...");
    ret = dali_interface_start_polling();
    ESP_ERROR_CHECK(ret); // Проверяем запуск таймера

    ESP_LOGI(TAG, "===================================");
    ESP_LOGI(TAG, " Initialization complete. System running.");
    ESP_LOGI(TAG, "===================================");

    while(1) {
        vTaskDelay(pdMS_TO_TICKS(60000));
        //ESP_LOGI(TAG, "Heap free: %lu bytes (Uptime: %lus)", (unsigned long)esp_get_free_heap_size(), (unsigned long)(esp_timer_get_time() / 1000000));
    }
}