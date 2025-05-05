#include "freertos/FreeRTOS.h"

#include "dali_interface.h"
#include "definitions.h"
#include "dali_commands.h" // Включаем команды DALI
#include "freertos/task.h"
#include "freertos/timers.h" // Для TimerHandle_t
#include "esp_log.h"
#include "mqtt.h" // Для публикации статусов
#include "esp_mac.h"
#include "cJSON.h"
#include <stdio.h> // для snprintf
#include <freertos/semphr.h>

static const char *TAG = "DALI_IF";
static SemaphoreHandle_t dali_mutex = NULL; // Мьютекс для защиты доступа к шине DALI


// --- Статические переменные для задачи и таймера ---
static TimerHandle_t s_dali_poll_timer = NULL;
static TaskHandle_t s_dali_poll_task_handle = NULL;

// Структура для хранения последнего известного состояния (для уменьшения публикаций MQTT)
typedef struct {
    int level; // Последний известный уровень (-2 = не инициализирован, -1 = ошибка/нет ответа, 0-254, 255=MASK)
    // Можно добавить другие параметры статуса (ошибки и т.д.)
} dali_device_state_t;

// Массивы для хранения последних состояний
static dali_device_state_t last_device_state[DALI_MAX_DEVICES];
static dali_device_state_t last_group_state[DALI_MAX_GROUPS];

// --- Внутренние функции ---

// Инициализация состояний
static void initialize_states() {
    for (int i = 0; i < DALI_MAX_DEVICES; ++i) {
        last_device_state[i].level = -2; // Не инициализировано
    }
    for (int i = 0; i < DALI_MAX_GROUPS; ++i) {
        last_group_state[i].level = -2; // Не инициализировано
    }
     ESP_LOGI(TAG, "Internal DALI states initialized.");
}

// Функция для получения мьютекса
static bool lock_dali_bus() {
    if (dali_mutex == NULL) {
        ESP_LOGE(TAG, "DALI mutex not initialized!");
        return false;
    }
    // Увеличим таймаут мьютекса, чтобы учесть возможные задержки
    if (xSemaphoreTake(dali_mutex, pdMS_TO_TICKS(CONFIG_DALI2MQTT_DALI_TRANSACTION_TIMEOUT_MS + CONFIG_DALI2MQTT_DALI_INTER_FRAME_DELAY_MS + 50)) != pdTRUE) {
        ESP_LOGE(TAG, "Failed to take DALI mutex");
        return false;
    }
    return true;
}

void dali_poll_task(void *pvParameters) {
    ESP_LOGI(TAG, "DALI Poll Task started.");

    // Инициализация состояний при первом запуске задачи (на всякий случай)
    // initialize_states(); // Уже вызвано в dali_interface_init

    while (1) {
        // Ждем уведомления от таймера или от MQTT_EVENT_CONNECTED
        uint32_t notification_value = ulTaskNotifyTake(pdTRUE, portMAX_DELAY); // Очистить бит уведомления при выходе, ждать вечно

        if (notification_value > 0) { // Получили уведомление
            ESP_LOGI(TAG, "Polling DALI devices/groups...");
            dali_interface_poll_all(false); // false - публиковать только изменения
            ESP_LOGI(TAG, "Polling finished.");
        } else {
            // Этого не должно произойти с portMAX_DELAY, но на всякий случай
            ESP_LOGW(TAG, "Poll task woke up without notification?");
            vTaskDelay(pdMS_TO_TICKS(1000)); // Небольшая пауза перед повторным ожиданием
        }
    }
}

// Функция для освобождения мьютекса
static void unlock_dali_bus() {
    if (dali_mutex != NULL) {
        xSemaphoreGive(dali_mutex);
    }
}
// --- Callback таймера (остается здесь) ---
static void dali_poll_timer_callback(TimerHandle_t xTimer) { // Сделаем static
    // Теперь используем статический хэндл s_dali_poll_task_handle
    if (s_dali_poll_task_handle != NULL) {
        xTaskNotifyGive(s_dali_poll_task_handle);
    } else {
        // Этого не должно произойти, если таймер запущен после создания задачи
        ESP_LOGE(TAG,"Poll timer callback: Task handle is NULL!");
    }
}
// --- Публичные функции ---

esp_err_t dali_interface_init(void) {
    ESP_LOGI(TAG, "Initializing DALI Interface...");

    // 1. Создаем мьютекс
    if (dali_mutex == NULL) { // Проверка на случай повторной инициализации
        dali_mutex = xSemaphoreCreateMutex();
        if (dali_mutex == NULL) {
            ESP_LOGE(TAG, "Failed to create DALI mutex");
            return ESP_FAIL;
        }
    }

    // 2. Инициализируем состояния
    initialize_states();

    // 3. Инициализация низкоуровневого драйвера
    esp_err_t err = dali_init(CONFIG_DALI2MQTT_DALI_RX_PIN, CONFIG_DALI2MQTT_DALI_TX_PIN);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Low-level DALI driver initialization failed: %s", esp_err_to_name(err));
        // Не удаляем мьютекс здесь, он может быть уже использован
        return err;
    }
    ESP_LOGI(TAG, "Low-level DALI driver initialized.");

    // 4. Создание задачи опроса (если еще не создана)
    if (s_dali_poll_task_handle == NULL) {
        BaseType_t task_ret = xTaskCreate(dali_poll_task,
                                          "dali_poll_task",
                                          CONFIG_DALI2MQTT_DALI_POLL_TASK_STACK_SIZE,
                                          NULL,
                                          CONFIG_DALI2MQTT_DALI_POLL_TASK_PRIORITY,
                                          &s_dali_poll_task_handle);
        if (task_ret != pdPASS) {
            ESP_LOGE(TAG, "Failed to create DALI poll task!");
            s_dali_poll_task_handle = NULL; // Сбросить хэндл
            return ESP_FAIL;
        }
        ESP_LOGI(TAG, "DALI Poll Task created.");
    }

    // 5. Создание таймера опроса (если еще не создан)
    // Период берем из глобальной конфигурации g_app_config
    if (s_dali_poll_timer == NULL) {
        s_dali_poll_timer = xTimerCreate("daliPollTimer",
                                         pdMS_TO_TICKS(g_app_config.poll_interval_ms > 0 ? g_app_config.poll_interval_ms : CONFIG_DALI2MQTT_DALI_DEFAULT_POLL_INTERVAL_MS), // Защита от нулевого интервала
                                         pdTRUE, // Автоматический перезапуск
                                         (void *)0, // ID таймера (не используется)
                                         dali_poll_timer_callback); // Наш статический callback

        if (s_dali_poll_timer == NULL) {
            ESP_LOGE(TAG, "Failed to create DALI poll timer!");
            // Удаляем задачу, если таймер не создался? Или оставляем? Пока оставим.
            return ESP_FAIL;
        }
         ESP_LOGI(TAG, "DALI Poll Timer created. Interval: %lums", (unsigned long)g_app_config.poll_interval_ms);
    }

    ESP_LOGI(TAG, "DALI Interface Initialized successfully.");
    return ESP_OK;
}

esp_err_t dali_interface_start_polling(void) {
    if (s_dali_poll_timer == NULL || s_dali_poll_task_handle == NULL) {
        ESP_LOGE(TAG, "Cannot start polling: Timer or Task not initialized.");
        return ESP_ERR_INVALID_STATE;
    }
    if (xTimerIsTimerActive(s_dali_poll_timer)) {
        ESP_LOGW(TAG, "Polling timer already active.");
        return ESP_OK; // Уже запущен
    }

    // Обновляем период таймера на случай, если он изменился в конфиге до старта
    TickType_t new_period = pdMS_TO_TICKS(g_app_config.poll_interval_ms > 0 ? g_app_config.poll_interval_ms : CONFIG_DALI2MQTT_DALI_DEFAULT_POLL_INTERVAL_MS);
    if (xTimerChangePeriod(s_dali_poll_timer, new_period, pdMS_TO_TICKS(100)) != pdPASS) {
         ESP_LOGE(TAG, "Failed to set poll timer period before starting.");
         // Продолжаем попытку запуска со старым периодом
    }

    ESP_LOGI(TAG, "Starting DALI polling timer (Interval: %lu ms)...", (unsigned long)pdTICKS_TO_MS(new_period));
    if (xTimerStart(s_dali_poll_timer, pdMS_TO_TICKS(100)) != pdPASS) {
        ESP_LOGE(TAG, "Failed to start DALI poll timer!");
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "DALI polling timer started.");
    return ESP_OK;
}

esp_err_t dali_interface_stop_polling(void) {
     ESP_LOGI(TAG, "Stopping DALI polling...");
     esp_err_t err = ESP_OK;

     // Останавливаем и удаляем таймер
     if (s_dali_poll_timer != NULL) {
         if (xTimerIsTimerActive(s_dali_poll_timer)) {
             if (xTimerStop(s_dali_poll_timer, pdMS_TO_TICKS(100)) != pdPASS) {
                 ESP_LOGE(TAG, "Failed to stop DALI poll timer.");
                 err = ESP_FAIL; // Запоминаем ошибку, но продолжаем
             }
         }
         if (xTimerDelete(s_dali_poll_timer, pdMS_TO_TICKS(100)) != pdPASS) {
              ESP_LOGE(TAG, "Failed to delete DALI poll timer.");
              err = ESP_FAIL;
         } else {
             s_dali_poll_timer = NULL; // Сбрасываем хэндл
             ESP_LOGI(TAG, "DALI poll timer deleted.");
         }
     } else {
          ESP_LOGW(TAG, "DALI poll timer was already NULL.");
     }

     // Удаляем задачу опроса
     if (s_dali_poll_task_handle != NULL) {
         vTaskDelete(s_dali_poll_task_handle);
         s_dali_poll_task_handle = NULL; // Сбрасываем хэндл
         ESP_LOGI(TAG, "DALI poll task deleted.");
     } else {
          ESP_LOGW(TAG, "DALI poll task handle was already NULL.");
     }

     ESP_LOGI(TAG, "DALI polling stopped.");
     return err;
}


// Основная функция для отправки транзакции DALI
esp_err_t dali_interface_send_raw_command(dali_addressType_t address_type, uint8_t address,
                                          bool is_cmd, uint8_t command_or_data,
                                          bool send_twice, int *result)
{
    if (!lock_dali_bus()) return ESP_ERR_TIMEOUT;

    ESP_LOGD(TAG, "Sending DALI raw: Type=%d Addr=%d isCmd=%d CmdData=0x%02X Twice=%d",
             address_type, address, is_cmd, command_or_data, send_twice);

    int dali_res = DALI_RESULT_NO_REPLY; // Значение по умолчанию для результата
    esp_err_t err = dali_transaction(address_type, address, is_cmd, command_or_data, send_twice, CONFIG_DALI2MQTT_DALI_TRANSACTION_TIMEOUT_MS, &dali_res);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "DALI transaction failed: %s", esp_err_to_name(err));
    } else {
        ESP_LOGD(TAG, "DALI transaction OK. Result: %d", dali_res);
    }

    if (result != NULL) {
        *result = dali_res;
    }

    // Небольшая пауза между командами обязательна по стандарту DALI
    // Используем значение из project_defs.h
    // vTaskDelay(pdMS_TO_TICKS(DALI_INTER_FRAME_DELAY_MS));
    // dali_wait_between_frames(); // Используем inline функцию из dali.h, если она там есть и делает то же самое
    vTaskDelay(pdMS_TO_TICKS(CONFIG_DALI2MQTT_DALI_INTER_FRAME_DELAY_MS)); // Явная задержка

    unlock_dali_bus();
    return err;
}

// Обертка для стандартных команд (is_cmd = true)
esp_err_t dali_interface_send_command(dali_addressType_t address_type, uint8_t address,
                                      uint8_t command, bool send_twice, int *result)
{
    return dali_interface_send_raw_command(address_type, address, true, command, send_twice, result);
}

// Обертка для DAPC (is_cmd = false)
esp_err_t dali_interface_send_dapc(dali_addressType_t address_type, uint8_t address,
                                   uint8_t level, int *result)
{
    // Уровень 0 для DAPC не определен стандартом, используем команду OFF
    if (level == 0) {
        ESP_LOGD(TAG,"DAPC level 0 requested, sending OFF command instead.");
        return dali_interface_send_command(address_type, address, DALI_COMMAND_OFF, false, result);
    }
    // Уровни 1-254
    return dali_interface_send_raw_command(address_type, address, false, level, false, result);
}


esp_err_t dali_interface_query_actual_level(dali_addressType_t address_type, uint8_t address, int *level) {
     if (level == NULL) return ESP_ERR_INVALID_ARG;
     if (address_type == DALI_ADDRESS_TYPE_BROADCAST || address_type == DALI_ADDRESS_TYPE_SPECIAL_CMD) {
         ESP_LOGE(TAG,"Querying level for broadcast or special command is not supported.");
         return ESP_ERR_INVALID_ARG;
     }

    *level = -1; // Значение по умолчанию - ошибка/нет ответа

    ESP_LOGD(TAG, "Querying DALI level: Type=%d Addr=%d", address_type, address);

    int dali_result = DALI_RESULT_NO_REPLY;
    // Отправляем команду запроса уровня
    esp_err_t err = dali_interface_send_command(address_type, address, DALI_COMMAND_QUERY_ACTUAL_LEVEL, false, &dali_result);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "DALI query level transaction failed for %s %d: %s",
                 (address_type == DALI_ADDRESS_TYPE_GROUP) ? "group" : "device", address, esp_err_to_name(err));
        *level = -1; // Ошибка транзакции
    } else {
        if (dali_result == DALI_RESULT_NO_REPLY) {
            ESP_LOGW(TAG, "No reply to QUERY_ACTUAL_LEVEL for %s %d",
                     (address_type == DALI_ADDRESS_TYPE_GROUP) ? "group" : "device", address);
            *level = -1; // Нет ответа
        } else if (dali_result >= 0 && dali_result <= 254) { // 0-254 - валидные уровни
             ESP_LOGD(TAG, "DALI query level OK for %s %d. Level: %d",
                      (address_type == DALI_ADDRESS_TYPE_GROUP) ? "group" : "device", address, dali_result);
            *level = dali_result;
        } else if (dali_result == 255) { // 0xFF - маска или неопределенное состояние
            ESP_LOGD(TAG, "DALI query level OK for %s %d. Level: MASK/Undefined (255)",
                     (address_type == DALI_ADDRESS_TYPE_GROUP) ? "group" : "device", address);
            *level = 255; // Используем 255 для MASK
        } else {
            ESP_LOGE(TAG, "Invalid reply to QUERY_ACTUAL_LEVEL for %s %d: %d",
                     (address_type == DALI_ADDRESS_TYPE_GROUP) ? "group" : "device", address, dali_result);
             *level = -1; // Некорректный ответ
        }
    }

    // Задержка уже была в dali_interface_send_command
    return err; // Возвращаем ошибку RMT транзакции, если была
}


void dali_interface_query_and_publish_status(dali_addressType_t address_type, uint8_t address) {
    int current_level = -1;
    esp_err_t err = dali_interface_query_actual_level(address_type, address, &current_level);

    // Определяем тип и индекс для хранения и топика MQTT
    const char* type_str = NULL;
    dali_device_state_t* last_state_ptr = NULL;

    if (address_type == DALI_ADDRESS_TYPE_GROUP && address < DALI_MAX_GROUPS) {
        type_str = "group";
        last_state_ptr = &last_group_state[address];
    } else if (address_type == DALI_ADDRESS_TYPE_SHORT && address < DALI_MAX_DEVICES) {
        type_str = "device";
        last_state_ptr = &last_device_state[address];
    } else {
        ESP_LOGE(TAG, "Invalid address type/number for status query: Type=%d, Addr=%d", address_type, address);
        return;
    }

    // Проверяем, изменился ли статус или это первый запрос после инициализации (-2)
    // Публикуем также в случае ошибки чтения DALI, если предыдущее состояние было валидным (чтобы сообщить об ошибке)
    bool changed = (last_state_ptr->level == -2) || (last_state_ptr->level != current_level);
    bool publish_error = (err != ESP_OK || current_level < 0) && (last_state_ptr->level >= 0); // Публикуем ошибку, если раньше было ОК

    if (changed || publish_error) {
        ESP_LOGI(TAG, "Status %s for %s %d: Level %d (Last: %d, Err: %d)",
                 changed ? "changed" : "error", type_str, address, current_level, last_state_ptr->level, err);

        // Обновляем последнее известное состояние (даже если ошибка, чтобы не публиковать ее повторно)
        last_state_ptr->level = current_level;

        // Формируем топик и JSON payload для MQTT
        char topic[128];
        snprintf(topic, sizeof(topic), "%s/light/%s/%d/state", CONFIG_DALI2MQTT_MQTT_BASE_TOPIC, type_str, address);

        cJSON *root = cJSON_CreateObject();
        if (!root) {
             ESP_LOGE(TAG, "Failed to create JSON object for %s %d", type_str, address);
             return;
        }

        // Добавляем стандартные поля HA
        if (current_level == 0) { // Уровень 0 = OFF
            cJSON_AddStringToObject(root, "state", "OFF");
            cJSON_AddNumberToObject(root, "brightness", 0);
        } else if (current_level > 0 && current_level <= 254) { // Уровень > 0 = ON
            cJSON_AddStringToObject(root, "state", "ON");
            // Отправляем в MQTT яркость [1..255] (простое отображение DALI 1..254 -> MQTT 1..254)
            cJSON_AddNumberToObject(root, "brightness", current_level);
        } else if (current_level == 255) { // MASK/Undefined
            // Отображаем MASK как ON с максимальной яркостью
            cJSON_AddStringToObject(root, "state", "ON");
            cJSON_AddNumberToObject(root, "brightness", 255);
        } else { // Ошибка (-1) или нет ответа
            // Если предыдущее состояние было OK, публикуем текущее как OFF (или можно добавить поле error)
            // Если предыдущее тоже было ошибкой, не публикуем ничего, чтобы не спамить
             if (publish_error) {
                 ESP_LOGW(TAG, "Publishing error state for %s %d", type_str, address);
                 cJSON_AddStringToObject(root, "state", "OFF"); // Показываем как выключенное при ошибке
                 cJSON_AddNumberToObject(root, "brightness", 0);
                 cJSON_AddStringToObject(root, "dali_status", "error"); // Доп. поле
             } else {
                  ESP_LOGD(TAG, "Skipping publish for error state for %s %d (already in error)", type_str, address);
                  cJSON_Delete(root);
                  return; // Не публикуем повторную ошибку
             }
        }

        char *json_payload = cJSON_PrintUnformatted(root);
        if (json_payload) {
            ESP_LOGD(TAG, "Publishing to %s: %s", topic, json_payload);
            mqtt_manager_publish(topic, json_payload, 1); // Retain = true для состояния
            free(json_payload);
        } else {
            ESP_LOGE(TAG, "Failed to render JSON for %s %d", type_str, address);
        }
        cJSON_Delete(root);

    } else {
         ESP_LOGD(TAG, "Status not changed for %s %d (Level %d)", type_str, address, current_level);
    }
}

// Callback для таймера опроса



void dali_interface_poll_all(bool force_publish) {
    ESP_LOGD(TAG, "Polling all configured DALI entities (force_publish=%d)...", force_publish);
    if (force_publish) {
        ESP_LOGI(TAG,"Force publishing requested, resetting known states.");
        initialize_states();
    }
    // Опрос групп
    for (int i = 0; i < DALI_MAX_GROUPS; ++i) {
        if ((g_app_config.poll_groups_mask >> i) & 1) {
            dali_interface_query_and_publish_status(DALI_ADDRESS_TYPE_GROUP, i);
        }
    }
    // Опрос устройств
    for (int i = 0; i < DALI_MAX_DEVICES; ++i) {
        if ((g_app_config.poll_devices_mask >> i) & 1) {
            dali_interface_query_and_publish_status(DALI_ADDRESS_TYPE_SHORT, i);
        }
    }
}

void dali_interface_publish_ha_discovery(void) {
    ESP_LOGI(TAG, "Publishing Home Assistant MQTT Discovery config...");
    char topic_buffer[128];
    char payload_buffer[600]; // Увеличим буфер для device info
    char unique_id_buffer[64];
    char name_buffer[64];
    char device_info_buffer[256];

    // --- Информация об устройстве (мосте) ---
    uint8_t mac[6];
    esp_efuse_mac_get_default(mac);
    // Создаем уникальный идентификатор на основе MAC
    char bridge_id[30];
     snprintf(bridge_id, sizeof(bridge_id), "dali_bridge_%02x%02x%02x", mac[3], mac[4], mac[5]); // Используем последние 3 байта MAC

    snprintf(device_info_buffer, sizeof(device_info_buffer),
             "{\"identifiers\": [\"%s\"], \"name\": \"DALI MQTT Bridge\", \"manufacturer\": \"ESP-IDF\", \"model\": \"ESP32 DALI Bridge v1.0\"}",
             bridge_id);

    // --- Обнаружение Групп ---
    for (int i = 0; i < DALI_MAX_GROUPS; ++i) {
        if ((g_app_config.poll_groups_mask >> i) & 1) {
            snprintf(unique_id_buffer, sizeof(unique_id_buffer), "%s_group_%d", bridge_id, i); // Уникальный ID для группы
            snprintf(name_buffer, sizeof(name_buffer), "DALI Group %d", i);
            snprintf(topic_buffer, sizeof(topic_buffer), "homeassistant/light/%s/config", unique_id_buffer);

            // Формируем JSON конфигурации для HA
            snprintf(payload_buffer, sizeof(payload_buffer),
                     "{"
                     "\"~\": \"%s/light/group/%d\"," // Базовый топик для этого светильника
                     "\"name\": \"%s\","
                     "\"unique_id\": \"%s\","
                     "\"cmd_t\": \"~/set\","      // Топик команд: <base>/set
                     "\"stat_t\": \"~/state\","   // Топик состояния: <base>/state
                     "\"schema\": \"json\","      // Используем JSON для команд и состояния
                     "\"brightness\": true,"      // Поддержка яркости
                     "\"brightness_scale\": 255," // Шкала яркости 0-255
                     "\"qos\": 1,"                // QoS для команд и состояния
                     "\"retain\": true,"           // Сохранять состояние
                     "\"avty_t\": \"%s\","       // Топик доступности (общий для моста)
                     "\"avty_mode\": \"latest\"," // Использовать последнее сообщение о доступности
                     "\"pl_avail\": \"%s\","
                     "\"pl_not_avail\": \"%s\","
                     "\"device\": %s"             // Информация об устройстве (мосте)
                     "}",
                     CONFIG_DALI2MQTT_MQTT_BASE_TOPIC, i,              // ~
                     name_buffer,                     // name
                     unique_id_buffer,                // unique_id
                     CONFIG_DALI2MQTT_MQTT_AVAILABILITY_TOPIC,         // avty_t
                     CONFIG_DALI2MQTT_MQTT_PAYLOAD_ONLINE,             // pl_avail
                     CONFIG_DALI2MQTT_MQTT_PAYLOAD_OFFLINE,            // pl_not_avail
                     device_info_buffer               // device
                     );

            mqtt_manager_publish(topic_buffer, payload_buffer, 1); // Retain = true для конфигурации HA
            vTaskDelay(pdMS_TO_TICKS(50)); // Небольшая пауза между публикациями
        }
    }

    // --- Обнаружение Устройств ---
     for (int i = 0; i < DALI_MAX_DEVICES; ++i) {
        if ((g_app_config.poll_devices_mask >> i) & 1) {
            snprintf(unique_id_buffer, sizeof(unique_id_buffer), "%s_device_%d", bridge_id, i); // Уникальный ID для устройства
            snprintf(name_buffer, sizeof(name_buffer), "DALI Device %d", i);
            snprintf(topic_buffer, sizeof(topic_buffer), "homeassistant/light/%s/config", unique_id_buffer);

            // Формируем JSON конфигурации для HA (аналогично группам)
            snprintf(payload_buffer, sizeof(payload_buffer),
                     "{"
                     "\"~\": \"%s/light/device/%d\","
                     "\"name\": \"%s\","
                     "\"unique_id\": \"%s\","
                     "\"cmd_t\": \"~/set\","
                     "\"stat_t\": \"~/state\","
                     "\"schema\": \"json\","
                     "\"brightness\": true,"
                     "\"brightness_scale\": 255,"
                      "\"qos\": 1,"
                     "\"retain\": true,"
                     "\"avty_t\": \"%s\","
                     "\"avty_mode\": \"latest\","
                     "\"pl_avail\": \"%s\","
                     "\"pl_not_avail\": \"%s\","
                     "\"device\": %s"
                     "}",
                     CONFIG_DALI2MQTT_MQTT_BASE_TOPIC, i,              // ~
                     name_buffer,                     // name
                     unique_id_buffer,                // unique_id
                     CONFIG_DALI2MQTT_MQTT_AVAILABILITY_TOPIC,         // avty_t
                     CONFIG_DALI2MQTT_MQTT_PAYLOAD_ONLINE,             // pl_avail
                     CONFIG_DALI2MQTT_MQTT_PAYLOAD_OFFLINE,            // pl_not_avail
                     device_info_buffer               // device
                     );

            mqtt_manager_publish(topic_buffer, payload_buffer, 1); // Retain = true
             vTaskDelay(pdMS_TO_TICKS(50)); // Небольшая пауза
        }
    }
    ESP_LOGI(TAG, "Home Assistant MQTT Discovery config published.");
}


esp_err_t dali_interface_manage_group_membership(uint8_t device_short_address,
                                                 uint8_t group_number,
                                                 bool add_to_group)
{
    // Проверка валидности адресов
    if (device_short_address > 63) {
        ESP_LOGE(TAG, "Invalid device short address: %d", device_short_address);
        return ESP_ERR_INVALID_ARG;
    }
    if (group_number > 15) {
        ESP_LOGE(TAG, "Invalid group number: %d", group_number);
        return ESP_ERR_INVALID_ARG;
    }

    // Определяем DALI команду
    uint8_t dali_command;
    if (add_to_group) {
        dali_command = DALI_COMMAND_ADD_TO_GROUP_0 + group_number; // Команды 0x60 - 0x6F
        ESP_LOGI(TAG, "Adding device %d to group %d (Command 0x%02X)", device_short_address, group_number, dali_command);
    } else {
        dali_command = DALI_COMMAND_REMOVE_FROM_GROUP_0 + group_number; // Команды 0x70 - 0x7F
        ESP_LOGI(TAG, "Removing device %d from group %d (Command 0x%02X)", device_short_address, group_number, dali_command);
    }

    int dali_result = DALI_RESULT_NO_REPLY; // Ожидаем, что ответа не будет
    // Отправляем команду на КОНКРЕТНОЕ УСТРОЙСТВО
    esp_err_t err = dali_interface_send_command(DALI_ADDRESS_TYPE_SHORT, // Адресуемся к устройству
                                                device_short_address,    // Его короткий адрес
                                                dali_command,            // Команда добавления/удаления
                                                false,                   // Не требует двойной отправки
                                                &dali_result);           // Результат (скорее всего -1)

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to send group management command to device %d: %s", device_short_address, esp_err_to_name(err));
    } else {
        // Небольшая задержка после команды конфигурации может быть полезна
        // Задержка уже есть в send_command
        // vTaskDelay(pdMS_TO_TICKS(DALI_INTER_FRAME_DELAY_MS));
        ESP_LOGI(TAG, "Group management command sent successfully to device %d. DALI Result: %d", device_short_address, dali_result);
    }

    // Команда применена самим балластом. Нам не нужно хранить состояние групп на ESP32.
    // Опционально: можно запросить QUERY_GROUPS_0_7 / QUERY_GROUPS_8_15 у устройства, чтобы проверить.
    // Пример:
    // vTaskDelay(pdMS_TO_TICKS(100)); // Дать время балласту сохранить
    // uint8_t query_cmd = (group_number < 8) ? DALI_COMMAND_QUERY_GROUPS_0_7 : DALI_COMMAND_QUERY_GROUPS_8_15;
    // int query_result = -1;
    // dali_interface_send_command(DALI_ADDRESS_TYPE_SHORT, device_short_address, query_cmd, false, &query_result);
    // ESP_LOGI(TAG, "Verification query result for groups of device %d: %d", device_short_address, query_result);
    // // Дальше можно анализировать биты в query_result

    return err; // Возвращаем результат RMT транзакции
}