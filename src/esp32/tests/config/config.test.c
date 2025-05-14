#include <string.h>
#include "unity.h"
#include "esp_log.h"
#include "nvs_flash.h" // Для инициализации NVS в тестах
#include "nvs.h"       // Для прямого доступа к NVS (моки)

// Включаем заголовочные файлы тестируемого модуля и project_defs
#include "config.h"
#include "app_config.h" // Здесь определена app_config_t и дефолты

// Включаем sdkconfig, чтобы иметь доступ к CONFIG_MYPROJ_... дефолтам
#include "sdkconfig.h"


// Глобальная конфигурация, которую мы будем использовать в тестах
app_config_t g_app_config; // Тестовый экземпляр конфигурации


// --- Вспомогательные функции для мокирования NVS ---
// Эти функции будут перехватывать вызовы NVS для DALI параметров

// Состояние "мока" NVS
static uint32_t mock_nvs_poll_interval = 0;
static uint16_t mock_nvs_poll_groups_mask = 0;
static uint64_t mock_nvs_poll_devices_mask = 0;
static bool mock_nvs_poll_interval_present = false;
static bool mock_nvs_poll_groups_mask_present = false;
static bool mock_nvs_poll_devices_mask_present = false;

// Переопределение функций NVS (слабая линковка или директивы препроцессора)
// Это сложнее для функций ESP-IDF. Проще инициализировать NVS и писать/читать из него.
// Для простоты, мы будем инициализировать реальный NVS для тестов, но
// будем его очищать и заполнять нужными значениями перед каждым тестом.


// Тег для логов
static const char* TAG = "test_config_manager";

// --- Настройка и очистка тестов ---
void setUp(void) {
    // Вызывается перед каждым тестом
    ESP_LOGI(TAG, "setUp running...");
    // Инициализация NVS для тестов (если еще не сделана)
    // Это важно, так как config_manager работает с NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        TEST_ESP_OK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    TEST_ESP_OK(ret);

    // Очищаем g_app_config перед каждым тестом (или заполняем известными значениями)
    memset(&g_app_config, 0, sizeof(app_config_t));



    // Очистим реальный NVS для чистоты эксперимента между тестами
    nvs_handle_t nvs_handle;
    TEST_ESP_OK(nvs_open(CONFIG_DALI2MQTT_NVS_NAMESPACE, NVS_READWRITE, &nvs_handle));
    TEST_ESP_OK(nvs_erase_all(nvs_handle)); // Очищаем все в нашем namespace
    TEST_ESP_OK(nvs_commit(nvs_handle));
    nvs_close(nvs_handle);
    ESP_LOGI(TAG, "NVS erased for namespace %s", CONFIG_DALI2MQTT_NVS_NAMESPACE);
}

void tearDown(void) {
    // Вызывается после каждого теста
    ESP_LOGI(TAG, "tearDown running...");
    nvs_flash_deinit(); // Деинициализация NVS, если нужно
}

// --- Тестовые случаи ---

TEST_CASE("Config Manager init nvs empty test", "[config]") {
// Тест инициализации config_manager, когда NVS пуст
    ESP_LOGI(TAG, "Running test_config_manager_init_nvs_empty...");
    // Убедимся, что NVS пуст (сделано в setUp)

    TEST_ESP_OK(config_manager_init()); // Вызываем тестируемую функцию

    // Проверяем параметры WiFi/MQTT (должны быть из sdkconfig)
    TEST_ASSERT_EQUAL_STRING(CONFIG_DALI2MQTT_WIFI_DEFAULT_SSID, g_app_config.wifi_ssid);
    TEST_ASSERT_EQUAL_STRING(CONFIG_DALI2MQTT_WIFI_DEFAULT_PASS, g_app_config.wifi_pass);
    TEST_ASSERT_EQUAL_STRING(CONFIG_DALI2MQTT_MQTT_DEFAULT_URI, g_app_config.mqtt_uri);

    // Проверяем DALI параметры (должны быть дефолтные, т.к. NVS был пуст)
    uint32_t expected_poll_interval = CONFIG_DALI2MQTT_DALI_DEFAULT_POLL_INTERVAL_MS;
    TEST_ASSERT_EQUAL_UINT32(expected_poll_interval, g_app_config.poll_interval_ms);
    TEST_ASSERT_EQUAL_UINT16(0x0001, g_app_config.poll_groups_mask); // Дефолт из config_manager.c
    TEST_ASSERT_EQUAL_UINT64(0, g_app_config.poll_devices_mask);   // Дефолт из config_manager.c

    // Проверяем, что дефолтные DALI параметры были записаны в NVS
    nvs_handle_t nvs_handle;
    TEST_ESP_OK(nvs_open(CONFIG_DALI2MQTT_NVS_NAMESPACE, NVS_READONLY, &nvs_handle));
    uint32_t nvs_interval;
    TEST_ESP_OK(nvs_get_u32(nvs_handle, NVS_KEY_POLL_INTERVAL, &nvs_interval));
    nvs_close(nvs_handle);

    TEST_ASSERT_EQUAL_UINT32(expected_poll_interval, nvs_interval);
}
TEST_CASE("Config Manager init with nvs values", "[config]") {
// Тест инициализации, когда в NVS есть DALI параметры
    ESP_LOGI(TAG, "Running test_config_manager_init_with_nvs_values...");
    // Предварительно запишем значения DALI в NVS
    nvs_handle_t nvs_handle;
    TEST_ESP_OK(nvs_open(CONFIG_DALI2MQTT_NVS_NAMESPACE, NVS_READWRITE, &nvs_handle));
    uint32_t test_interval = 12345;
    TEST_ESP_OK(nvs_set_u32(nvs_handle, NVS_KEY_POLL_INTERVAL, test_interval));
    TEST_ESP_OK(nvs_commit(nvs_handle));
    nvs_close(nvs_handle);

    TEST_ESP_OK(config_manager_init());

    // WiFi/MQTT должны быть из sdkconfig
    TEST_ASSERT_EQUAL_STRING(CONFIG_DALI2MQTT_WIFI_DEFAULT_SSID, g_app_config.wifi_ssid);
    TEST_ASSERT_EQUAL_STRING(CONFIG_DALI2MQTT_MQTT_DEFAULT_URI, g_app_config.mqtt_uri);

    // DALI параметры должны быть из NVS
    TEST_ASSERT_EQUAL_UINT32(test_interval, g_app_config.poll_interval_ms);
}
TEST_CASE("Config manager save dali params test", "[config]") {
// Тест сохранения DALI параметров
    ESP_LOGI(TAG, "Running test_config_manager_save_dali_params...");
    // Заполняем g_app_config тестовыми значениями (WiFi/MQTT не важны для этого теста, т.к. не сохраняются)
    g_app_config.poll_interval_ms = 9876;

    // Мокаем функцию mqtt_publish_config, так как она будет вызвана
    // В реальном юнит-тесте ее нужно было бы мокать через CMock или FFF.
    // Здесь мы просто знаем, что она вызовется, и игнорируем ее эффект.
    // Для этого теста важно, что config_manager_save() отработал и записал в NVS.

    TEST_ESP_OK(config_manager_save());

    // Проверяем, что значения были записаны в NVS
    nvs_handle_t nvs_handle;
    TEST_ESP_OK(nvs_open(CONFIG_DALI2MQTT_NVS_NAMESPACE, NVS_READONLY, &nvs_handle));
    uint32_t nvs_interval;
    TEST_ESP_OK(nvs_get_u32(nvs_handle, NVS_KEY_POLL_INTERVAL, &nvs_interval));
    nvs_close(nvs_handle);

    TEST_ASSERT_EQUAL_UINT32(g_app_config.poll_interval_ms, nvs_interval);
}
