#include "wifi_manager.h"
#include "definitions.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "string.h"
#include <esp_netif.h>


static const char *TAG = "WIFI_MGR";

static EventGroupHandle_t s_wifi_event_group;
static int s_retry_num = 0;

// Обработчик событий WiFi и IP
static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                               int32_t event_id, void* event_data) {
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
        ESP_LOGI(TAG, "WIFI_EVENT_STA_START: Trying to connect...");
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        ESP_LOGW(TAG, "WIFI_EVENT_STA_DISCONNECTED");
        if (s_retry_num < CONFIG_DALI2MQTT_WIFI_MAX_RETRY) {
            esp_wifi_connect(); // Попытка переподключения
            s_retry_num++;
            ESP_LOGI(TAG, "Retry to connect to the AP (%d/%d)", s_retry_num, CONFIG_DALI2MQTT_WIFI_MAX_RETRY);
        } else {
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT); // Сигнализируем об ошибке
            ESP_LOGE(TAG, "Connect to the AP failed after %d retries", CONFIG_DALI2MQTT_WIFI_MAX_RETRY);
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "Got IP:" IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_num = 0; // Сбрасываем счетчик попыток при успешном подключении
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT); // Сигнализируем об успехе
    } else {
         ESP_LOGD(TAG, "Unhandled event: base=%s, id=%ld", event_base, event_id);
    }
}

esp_err_t wifi_manager_init(void) {
    ESP_LOGI(TAG, "Initializing WiFi...");
    s_wifi_event_group = xEventGroupCreate();
    if (!s_wifi_event_group) {
        ESP_LOGE(TAG, "Failed to create event group");
        return ESP_FAIL;
    }

    ESP_ERROR_CHECK(esp_netif_init()); // Инициализация TCP/IP стека
    ESP_ERROR_CHECK(esp_event_loop_create_default()); // Создание системного цикла событий
    esp_netif_create_default_wifi_sta(); // Создание интерфейса WiFi Station по умолчанию

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg)); // Инициализация WiFi драйвера

    // Регистрация обработчиков событий
    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        &instance_got_ip));

    return ESP_OK;
}

esp_err_t wifi_manager_connect(void) {
    // Используем конфигурацию из g_app_config
    ESP_LOGI(TAG, "Connecting to WiFi SSID: %s", g_app_config.wifi_ssid);

    wifi_config_t wifi_config = {
        .sta = {
            // .ssid и .password будут скопированы ниже
            .threshold.authmode = WIFI_AUTH_WPA2_PSK, // Или другой режим (WPA_WPA2_PSK)
            .pmf_cfg = {
                .capable = true,
                .required = false
            },
        },
    };
    // Копируем SSID и пароль из глобальной конфигурации, обеспечивая null-терминацию
    strncpy((char*)wifi_config.sta.ssid, g_app_config.wifi_ssid, sizeof(wifi_config.sta.ssid) - 1);
    wifi_config.sta.ssid[sizeof(wifi_config.sta.ssid) - 1] = '\0';
    strncpy((char*)wifi_config.sta.password, g_app_config.wifi_pass, sizeof(wifi_config.sta.password) - 1);
    wifi_config.sta.password[sizeof(wifi_config.sta.password) - 1] = '\0';


    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start()); // Запускаем WiFi в режиме Station

    ESP_LOGI(TAG, "wifi_init_sta finished. Waiting for connection...");

    // Ожидаем события подключения или ошибки из event handler
    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
            WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
            pdFALSE, // Не очищать биты при выходе
            pdFALSE, // Не ждать оба бита
            portMAX_DELAY); // Ждать бесконечно

    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "Connected to AP SSID:%s", g_app_config.wifi_ssid);
        return ESP_OK;
    } else if (bits & WIFI_FAIL_BIT) {
        ESP_LOGE(TAG, "Failed to connect to SSID:%s", g_app_config.wifi_ssid);
        return ESP_FAIL;
    } else {
        ESP_LOGE(TAG, "UNEXPECTED EVENT while waiting for connection");
        return ESP_FAIL;
    }
}

esp_err_t wifi_manager_disconnect(void) {
    ESP_LOGI(TAG, "Disconnecting WiFi...");
    esp_err_t err = esp_wifi_disconnect();
    if (err == ESP_ERR_WIFI_NOT_INIT) {
        ESP_LOGW(TAG, "WiFi not initialized.");
        return ESP_OK; // Уже не инициализирован
    } else if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to disconnect WiFi: %s", esp_err_to_name(err));
        // Продолжаем остановку
    }

    err = esp_wifi_stop();
     if (err == ESP_ERR_WIFI_NOT_INIT) {
        ESP_LOGW(TAG, "WiFi not initialized (stop).");
        return ESP_OK; // Уже не инициализирован
    } else if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to stop WiFi: %s", esp_err_to_name(err));
        return err; // Возвращаем ошибку остановки
    }

    // Можно также дерегистрировать обработчики и удалить event group, если нужно полное выключение
    // esp_event_handler_instance_unregister(...)
    // vEventGroupDelete(s_wifi_event_group);
    // esp_wifi_deinit();
    ESP_LOGI(TAG, "WiFi stopped.");
    return ESP_OK;
}