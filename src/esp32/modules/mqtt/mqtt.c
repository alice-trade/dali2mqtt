// main/mqtt_manager.c
#include "mqtt.h"
#include "definitions.h"
#include "esp_log.h"
#include "dalic/include/dali_commands.h"
#include "mqtt_client.h"
#include "string.h" // Для strncmp, strchr, atoi, strncpy, strcmp, strlen, strerror
#include "stdlib.h" // Для strtol, strtoull
#include "cJSON.h"  // Для парсинга/генерации JSON
#include "dali_interface.h" // Для вызова команд DALI и управления группами
#include "config.h" // Для изменения конфигурации
#include "freertos/task.h" // Для vTaskDelay

static const char *TAG = "MQTT_MGR";
static esp_mqtt_client_handle_t client = NULL;
static bool is_connected = false;

// --- Вспомогательные функции ---

// Парсит топик команды вида <prefix>/light/<type>/<addr>/set
static bool parse_command_topic(const char *topic, char *type_buf, size_t type_buf_size, uint8_t *addr) {
    char prefix_buffer[128];
    snprintf(prefix_buffer, sizeof(prefix_buffer), "%s/light/", CONFIG_DALI2MQTT_MQTT_BASE_TOPIC); // e.g., "dali_bridge/light/"
    const char *prefix = prefix_buffer;

    if (strncmp(topic, prefix, strlen(prefix)) != 0) {
        return false; // Не соответствует префиксу
    }

    const char *type_start = topic + strlen(prefix);
    const char *addr_start = strchr(type_start, '/');
    if (!addr_start) return false; // Нет слеша после типа

    const char *cmd_start = strchr(addr_start + 1, '/');
     if (!cmd_start) return false; // Нет слеша после адреса

    // Проверяем окончание "/set"
    if (strcmp(cmd_start, "/set") != 0) {
        return false;
    }

    // Извлекаем тип (group или device)
    size_t type_len = addr_start - type_start;
    if (type_len >= type_buf_size) return false; // Буфер type_buf слишком мал
    strncpy(type_buf, type_start, type_len);
    type_buf[type_len] = '\0';

    // Извлекаем адрес
    char *endptr;
    long val = strtol(addr_start + 1, &endptr, 10);
    if (endptr == addr_start + 1 || *endptr != '/' || val < 0 || val > 255) {
         return false; // Ошибка преобразования или недопустимый адрес
    }
    *addr = (uint8_t)val;

    // Проверяем, что тип - "group" или "device"
    if (strcmp(type_buf, "group") != 0 && strcmp(type_buf, "device") != 0) {
        return false;
    }

    // Проверяем диапазон адреса для конкретного типа
    if (strcmp(type_buf, "group") == 0 && *addr > 15) {
        ESP_LOGW(TAG, "Invalid group address in topic: %d", *addr);
        return false; // Неверный номер группы
    }
    if (strcmp(type_buf, "device") == 0 && *addr > 63) {
        ESP_LOGW(TAG, "Invalid device short address in topic: %d", *addr);
        return false; // Неверный короткий адрес
    }

    return true;
}

// Обработчик входящих MQTT сообщений (вызывается из event handler'а)
static void handle_incoming_message(const char *topic, int topic_len, const char *data, int data_len) {
    // Создаем null-терминированные строки для удобства
    char topic_str[150];
    char data_str[512];

    int Tlen = topic_len < sizeof(topic_str) - 1 ? topic_len : sizeof(topic_str) - 1;
    strncpy(topic_str, topic, Tlen);
    topic_str[Tlen] = '\0';

    int Dlen = data_len < sizeof(data_str) - 1 ? data_len : sizeof(data_str) - 1;
    strncpy(data_str, data, Dlen);
    data_str[Dlen] = '\0';

    ESP_LOGI(TAG, "Received message: Topic='%s', Data='%s'", topic_str, data_str);

    // 1. Обработка команд управления светом (JSON)
    char type[8]; // "group" или "device"
    uint8_t addr;
    if (parse_command_topic(topic_str, type, sizeof(type), &addr)) {
        ESP_LOGD(TAG, "Parsed light command for type=%s, addr=%d", type, addr);
        cJSON *root = cJSON_Parse(data_str);
        if (root == NULL) {
            ESP_LOGE(TAG, "Failed to parse command JSON: %s", cJSON_GetErrorPtr());
            return;
        }

        dali_addressType_t addr_type = (strcmp(type, "group") == 0) ? DALI_ADDRESS_TYPE_GROUP : DALI_ADDRESS_TYPE_SHORT;
        int dali_result = DALI_RESULT_NO_REPLY;
        bool command_sent = false;

        // Обработка состояния ON/OFF
        const cJSON *state_json = cJSON_GetObjectItemCaseSensitive(root, "state");
        if (cJSON_IsString(state_json) && (state_json->valuestring != NULL)) {
            if (strcmp(state_json->valuestring, "ON") == 0) {
                ESP_LOGI(TAG, "CMD: %s %d ON", type, addr);
                dali_interface_send_command(addr_type, addr, DALI_COMMAND_RECALL_MAX_LEVEL, false, &dali_result);
                command_sent = true;
            } else if (strcmp(state_json->valuestring, "OFF") == 0) {
                ESP_LOGI(TAG, "CMD: %s %d OFF", type, addr);
                 dali_interface_send_command(addr_type, addr, DALI_COMMAND_OFF, false, &dali_result);
                 command_sent = true;
            }
        }

        // Обработка яркости (0-255 для Home Assistant)
        const cJSON *brightness_json = cJSON_GetObjectItemCaseSensitive(root, "brightness");
        if (cJSON_IsNumber(brightness_json)) {
            int brightness = brightness_json->valueint;
            if (brightness >= 0 && brightness <= 255) {
                 uint8_t dali_level;
                 if (brightness == 0) {
                     ESP_LOGI(TAG, "CMD: %s %d OFF (via brightness 0)", type, addr);
                     dali_interface_send_command(addr_type, addr, DALI_COMMAND_OFF, false, &dali_result);
                 } else {
                     dali_level = (brightness > 254) ? 254 : (uint8_t)brightness;
                     ESP_LOGI(TAG, "CMD: %s %d SET LEVEL %d (MQTT brightness %d)", type, addr, dali_level, brightness);
                     dali_interface_send_dapc(addr_type, addr, dali_level, &dali_result);
                 }
                 command_sent = true;
            } else {
                 ESP_LOGW(TAG, "Invalid brightness value: %d", brightness);
            }
        }

        cJSON_Delete(root);

        if (command_sent) {
            ESP_LOGI(TAG, "Light command for %s %d processed. DALI result: %d. Querying status...", type, addr, dali_result);
            vTaskDelay(pdMS_TO_TICKS(CONFIG_DALI2MQTT_DALI_POLL_DELAY_MS * 2)); // Пауза перед запросом
            dali_interface_query_and_publish_status(addr_type, addr);
        }

    }
    // 2. Обработка запроса на получение конфигурации
    else if (strcmp(topic_str, CONFIG_DALI2MQTT_MQTT_BASE_TOPIC CONFIG_DALI2MQTT_MQTT_CONFIG_GET_SUBTOPIC) == 0) {
         ESP_LOGI(TAG, "CMD: Get current configuration");
         mqtt_publish_config();
    }
    // 3. Обработка установки конфигурации (JSON)
    else if (strcmp(topic_str, CONFIG_DALI2MQTT_MQTT_BASE_TOPIC CONFIG_DALI2MQTT_MQTT_CONFIG_GET_SUBTOPIC) == 0) {
         ESP_LOGI(TAG, "CMD: Set configuration");
         cJSON *root = cJSON_Parse(data_str);
         if (root == NULL) {
             ESP_LOGE(TAG, "Failed to parse config JSON: %s", cJSON_GetErrorPtr());
             return;
         }

         bool config_changed = false;

         const cJSON *poll_interval_json = cJSON_GetObjectItemCaseSensitive(root, "poll_interval_ms");
         if (cJSON_IsNumber(poll_interval_json)) {
              // Вызываем config_manager_set*, который вернет ESP_OK если значение изменилось и было сохранено
              if (config_manager_set_poll_interval(poll_interval_json->valueint) == ESP_OK) {
                  // Не устанавливаем config_changed здесь, т.к. save() вызывается внутри set_*,
                  // и если интервал не изменился, то save() не будет вызван.
                  // mqtt_publish_config() будет вызван из save() если что-то реально сохранилось.
              }
         }

         const cJSON *poll_groups_json = cJSON_GetObjectItemCaseSensitive(root, "poll_groups_mask");
          if (cJSON_IsNumber(poll_groups_json)) {
              config_manager_set_poll_groups_mask((uint16_t)poll_groups_json->valueint);
          } else if (cJSON_IsString(poll_groups_json) && poll_groups_json->valuestring != NULL) {
              char *endptr;
              long val = strtol(poll_groups_json->valuestring, &endptr, 0);
              if (*endptr == '\0') {
                   config_manager_set_poll_groups_mask((uint16_t)val);
              } else {
                   ESP_LOGW(TAG, "Invalid format for poll_groups_mask string: %s", poll_groups_json->valuestring);
              }
          }

         const cJSON *poll_devices_json = cJSON_GetObjectItemCaseSensitive(root, "poll_devices_mask");
         if (cJSON_IsString(poll_devices_json) && poll_devices_json->valuestring != NULL) {
             char *endptr;
             unsigned long long val = strtoull(poll_devices_json->valuestring, &endptr, 0);
             if (*endptr == '\0') {
                 config_manager_set_poll_devices_mask(val);
             } else {
                  ESP_LOGW(TAG, "Invalid format for poll_devices_mask string: %s", poll_devices_json->valuestring);
             }
         } else if (cJSON_IsNumber(poll_devices_json)) {
              ESP_LOGW(TAG, "Setting poll_devices_mask via number is discouraged. Use hex string.");
              config_manager_set_poll_devices_mask((uint64_t)poll_devices_json->valuedouble);
         }

         cJSON_Delete(root);
         ESP_LOGI(TAG, "Configuration processing finished.");
         // mqtt_publish_config() вызывается из config_manager_save(), если были изменения
    }
    // 4. Обработка команды управления членством в группе
    else if (strcmp(topic_str, CONFIG_DALI2MQTT_MQTT_BASE_TOPIC CONFIG_DALI2MQTT_MQTT_GROUP_SET_SUBTOPIC) == 0) {
        ESP_LOGI(TAG, "CMD: Manage Group Membership");
        cJSON *root = cJSON_Parse(data_str);
        if (root == NULL) {
            ESP_LOGE(TAG, "Failed to parse group management JSON: %s", cJSON_GetErrorPtr());
             char err_payload[100];
             snprintf(err_payload, sizeof(err_payload), "{\"status\": \"error\", \"message\": \"Invalid JSON payload\"}");
             mqtt_manager_publish(CONFIG_DALI2MQTT_MQTT_BASE_TOPIC CONFIG_DALI2MQTT_MQTT_GROUP_RES_SUBTOPIC, err_payload, 0);
            return;
        }

        const cJSON *action_json = cJSON_GetObjectItemCaseSensitive(root, "action");
        const cJSON *address_json = cJSON_GetObjectItemCaseSensitive(root, "device_short_address");
        const cJSON *group_json = cJSON_GetObjectItemCaseSensitive(root, "group_number");

        char response_payload[200];
        const char* action_str = "unknown";
        int device_addr_int = -1;
        int group_num_int = -1;
        const char* status_str = "error";
        const char* message_str = "Invalid parameters";

        if (cJSON_IsString(action_json) && (action_json->valuestring != NULL)) action_str = action_json->valuestring;
        if (cJSON_IsNumber(address_json)) device_addr_int = address_json->valueint;
        if (cJSON_IsNumber(group_json)) group_num_int = group_json->valueint;

        if ( (strcmp(action_str, "add") == 0 || strcmp(action_str, "remove") == 0) &&
             device_addr_int >= 0 && device_addr_int <= 63 &&
             group_num_int >= 0 && group_num_int <= 15)
        {
            uint8_t device_addr = (uint8_t)device_addr_int;
            uint8_t group_num = (uint8_t)group_num_int;
            bool add_action = (strcmp(action_str, "add") == 0);

            ESP_LOGI(TAG, "Processing group management: Action=%s, Device=%d, Group=%d", action_str, device_addr, group_num);
            esp_err_t dali_err = dali_interface_manage_group_membership(device_addr, group_num, add_action);

            if (dali_err == ESP_OK) {
                status_str = "success";
                message_str = "Command sent successfully";
            } else {
                status_str = "error";
                message_str = esp_err_to_name(dali_err);
            }
        } else {
             ESP_LOGW(TAG, "Invalid parameters for group management: action=%s, device=%d, group=%d", action_str, device_addr_int, group_num_int);
             message_str = "Invalid action, device_short_address, or group_number";
        }

        cJSON_Delete(root);

        snprintf(response_payload, sizeof(response_payload),
                 "{\"action\": \"%s\", \"device_short_address\": %d, \"group_number\": %d, \"status\": \"%s\", \"message\": \"%s\"}",
                 action_str, device_addr_int, group_num_int, status_str, message_str);

        mqtt_manager_publish(CONFIG_DALI2MQTT_MQTT_BASE_TOPIC CONFIG_DALI2MQTT_MQTT_GROUP_RES_SUBTOPIC, response_payload, 0);
        ESP_LOGI(TAG, "Published group management result: %s", response_payload);

    } else {
         ESP_LOGW(TAG, "Unhandled topic: %s", topic_str);
    }
}

// --- Обработчик событий MQTT клиента (исправленная сигнатура) ---
static void mqtt_event_handler_cb(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data) {
    ESP_LOGD(TAG, "Event dispatched from event loop base=%s, event_id=%ld", base, event_id);
    esp_mqtt_event_handle_t event = (esp_mqtt_event_handle_t)event_data;
    esp_mqtt_client_handle_t current_client = event->client;
    int msg_id;

    switch (event->event_id) {
        case MQTT_EVENT_BEFORE_CONNECT:
             ESP_LOGI(TAG, "MQTT_EVENT_BEFORE_CONNECT");
             break;
        case MQTT_EVENT_CONNECTED:
            ESP_LOGI(TAG, "MQTT_EVENT_CONNECTED");
            is_connected = true;

            // Публикуем статус "online"
            msg_id = mqtt_manager_publish(CONFIG_DALI2MQTT_MQTT_AVAILABILITY_TOPIC, CONFIG_DALI2MQTT_MQTT_PAYLOAD_ONLINE, 1);
            ESP_LOGI(TAG, "Sent publish successful, msg_id=%d, topic=%s", msg_id, CONFIG_DALI2MQTT_MQTT_AVAILABILITY_TOPIC);

            // Подписываемся на топики
            char command_topic_wildcard[100];
            snprintf(command_topic_wildcard, sizeof(command_topic_wildcard), "%s/light/+/+/set", CONFIG_DALI2MQTT_MQTT_BASE_TOPIC);
            msg_id = esp_mqtt_client_subscribe(current_client, command_topic_wildcard, 1);
            ESP_LOGI(TAG, "Subscribed to %s, msg_id=%d", command_topic_wildcard, msg_id);

            msg_id = esp_mqtt_client_subscribe(current_client, CONFIG_DALI2MQTT_MQTT_BASE_TOPIC CONFIG_DALI2MQTT_MQTT_CONFIG_SET_SUBTOPIC, 1);
            ESP_LOGI(TAG, "Subscribed to %s, msg_id=%d", CONFIG_DALI2MQTT_MQTT_BASE_TOPIC CONFIG_DALI2MQTT_MQTT_CONFIG_SET_SUBTOPIC, msg_id);

            msg_id = esp_mqtt_client_subscribe(current_client, CONFIG_DALI2MQTT_MQTT_BASE_TOPIC CONFIG_DALI2MQTT_MQTT_CONFIG_GET_SUBTOPIC, 1);
            ESP_LOGI(TAG, "Subscribed to %s, msg_id=%d", CONFIG_DALI2MQTT_MQTT_BASE_TOPIC CONFIG_DALI2MQTT_MQTT_CONFIG_GET_SUBTOPIC, msg_id);

            msg_id = esp_mqtt_client_subscribe(current_client, CONFIG_DALI2MQTT_MQTT_BASE_TOPIC CONFIG_DALI2MQTT_MQTT_GROUP_SET_SUBTOPIC, 1);
            ESP_LOGI(TAG, "Subscribed to %s, msg_id=%d", CONFIG_DALI2MQTT_MQTT_BASE_TOPIC CONFIG_DALI2MQTT_MQTT_GROUP_SET_SUBTOPIC, msg_id);

            // Публикуем HA Discovery
            ESP_LOGI(TAG, "Publishing HA Discovery config...");
            dali_interface_publish_ha_discovery();

            // Запрашиваем начальный статус DALI
            ESP_LOGI(TAG, "Requesting initial DALI status poll (force publish)...");
            vTaskDelay(pdMS_TO_TICKS(1000)); // Даем время на подписки/discovery
            dali_interface_poll_all(true); // true - форсировать публикацию

            break;
        case MQTT_EVENT_DISCONNECTED:
            ESP_LOGW(TAG, "MQTT_EVENT_DISCONNECTED");
            is_connected = false;
            break;
        case MQTT_EVENT_SUBSCRIBED:
            ESP_LOGI(TAG, "MQTT_EVENT_SUBSCRIBED, msg_id=%d", event->msg_id);
            break;
        case MQTT_EVENT_UNSUBSCRIBED:
            ESP_LOGI(TAG, "MQTT_EVENT_UNSUBSCRIBED, msg_id=%d", event->msg_id);
            break;
        case MQTT_EVENT_PUBLISHED:
            ESP_LOGD(TAG, "MQTT_EVENT_PUBLISHED, msg_id=%d", event->msg_id);
            break;
        case MQTT_EVENT_DATA:
            ESP_LOGD(TAG, "MQTT_EVENT_DATA Received");
             handle_incoming_message(event->topic, event->topic_len, event->data, event->data_len);
            break;
        case MQTT_EVENT_ERROR:
            ESP_LOGE(TAG, "MQTT_EVENT_ERROR");
            if (event->error_handle->error_type == MQTT_ERROR_TYPE_TCP_TRANSPORT) {
                 ESP_LOGE(TAG, "TCP Transport Error: Last errno=%d (%s), esp_tls_err=0x%x, tls_stack_err=0x%x",
                          event->error_handle->esp_transport_sock_errno,
                          strerror(event->error_handle->esp_transport_sock_errno),
                          event->error_handle->esp_tls_last_esp_err,
                          event->error_handle->esp_tls_stack_err);
            } else if (event->error_handle->error_type == MQTT_ERROR_TYPE_CONNECTION_REFUSED) {
                ESP_LOGE(TAG, "Connection refused error code: 0x%x", event->error_handle->connect_return_code);
            } else {
                 ESP_LOGE(TAG, "Unknown MQTT error type: 0x%x", event->error_handle->error_type);
            }
            break;
        default:
            ESP_LOGD(TAG, "Other MQTT event id:%d", event->event_id);
            break;
    }
}

// --- Публичные функции ---

esp_err_t mqtt_manager_init(void) {
     ESP_LOGI(TAG, "Initializing MQTT...");
     const esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri = g_app_config.mqtt_uri,
        .credentials.client_id = CONFIG_DALI2MQTT_MQTT_DEFAULT_CLIENT_ID,
        .network.reconnect_timeout_ms = 10000,
        .network.timeout_ms = 20000,
        .task.priority = 5,
        .task.stack_size = 6144,
        .session.last_will = {
            .topic = CONFIG_DALI2MQTT_MQTT_AVAILABILITY_TOPIC,
            .msg = CONFIG_DALI2MQTT_MQTT_PAYLOAD_OFFLINE,
            .msg_len = strlen(CONFIG_DALI2MQTT_MQTT_PAYLOAD_OFFLINE),
            .qos = 1,
            .retain = 1
        },
        .session.keepalive = 60
    };

    client = esp_mqtt_client_init(&mqtt_cfg);
    if (!client) {
        ESP_LOGE(TAG, "Failed to initialize MQTT client");
        return ESP_FAIL;
    }
    ESP_ERROR_CHECK(esp_mqtt_client_register_event(client, ESP_EVENT_ANY_ID, mqtt_event_handler_cb, NULL));
    return ESP_OK;
}

esp_err_t mqtt_manager_start(void) {
     if (!client) {
         ESP_LOGE(TAG, "MQTT client not initialized");
         return ESP_ERR_INVALID_STATE;
     }
     ESP_LOGI(TAG, "Starting MQTT client...");
     esp_err_t err = esp_mqtt_client_start(client);
     if (err != ESP_OK) {
         ESP_LOGE(TAG, "Failed to start MQTT client: %s", esp_err_to_name(err));
     }
     return err;
}

esp_err_t mqtt_manager_stop(void) {
    if (!client) {
         ESP_LOGW(TAG, "MQTT client not initialized or already stopped.");
         return ESP_OK;
     }
     ESP_LOGI(TAG, "Stopping MQTT client...");
     esp_err_t err = esp_mqtt_client_stop(client);
      if (err != ESP_OK) {
         ESP_LOGE(TAG, "Failed to stop MQTT client: %s", esp_err_to_name(err));
     }
     // Ошибка остановки не должна мешать уничтожению
     esp_err_t destroy_err = esp_mqtt_client_destroy(client);
      if (destroy_err != ESP_OK) {
         ESP_LOGE(TAG, "Failed to destroy MQTT client: %s", esp_err_to_name(destroy_err));
         if (err == ESP_OK) err = destroy_err; // Возвращаем ошибку уничтожения, если остановка была ОК
     }
     client = NULL;
     is_connected = false;
     return err;
}

int mqtt_manager_publish(const char *topic, const char *data, int retain) {
     if (!client || !is_connected) {
         ESP_LOGW(TAG, "MQTT client not ready to publish (client=%p, connected=%d) to topic: %s", client, is_connected, topic);
         return -1;
     }
     int msg_id = esp_mqtt_client_publish(client, topic, data, 0, 1, retain);
     if (msg_id == -1) {
        ESP_LOGE(TAG, "Failed to queue MQTT message for topic: %s", topic);
     } else {
        ESP_LOGD(TAG, "Queued MQTT publish: topic=%s, msg_id=%d, retain=%d", topic, msg_id, retain);
     }
     return msg_id;
}

void mqtt_publish_config(void) {
    if (!client || !is_connected) {
         ESP_LOGW(TAG, "Cannot publish config, MQTT not connected.");
         return;
     }
     cJSON *root = cJSON_CreateObject();
     if (!root) {
        ESP_LOGE(TAG, "Failed to create JSON object for config");
        return;
     }

     cJSON_AddStringToObject(root, "mqtt_uri", g_app_config.mqtt_uri);
     cJSON_AddNumberToObject(root, "poll_interval_ms", g_app_config.poll_interval_ms);

     char groups_mask_str[8];
     snprintf(groups_mask_str, sizeof(groups_mask_str), "0x%04X", g_app_config.poll_groups_mask);
     cJSON_AddStringToObject(root, "poll_groups_mask", groups_mask_str);

     char devices_mask_str[20];
     snprintf(devices_mask_str, sizeof(devices_mask_str), "0x%016llX", (unsigned long long)g_app_config.poll_devices_mask);
     cJSON_AddStringToObject(root, "poll_devices_mask", devices_mask_str);

     cJSON_AddStringToObject(root, "wifi_ssid", g_app_config.wifi_ssid);

     char *json_str = cJSON_PrintUnformatted(root);
     if (!json_str) {
         ESP_LOGE(TAG, "Failed to render config JSON to string");
         cJSON_Delete(root);
         return;
     }

     ESP_LOGI(TAG, "Publishing current config to %s: %s", CONFIG_DALI2MQTT_MQTT_BASE_TOPIC CONFIG_DALI2MQTT_MQTT_CONFIG_RESP_SUBTOPIC, json_str);
     mqtt_manager_publish(CONFIG_DALI2MQTT_MQTT_BASE_TOPIC CONFIG_DALI2MQTT_MQTT_CONFIG_RESP_SUBTOPIC, json_str, 0); // retain=0

     free(json_str);
     cJSON_Delete(root);
}