### Общая стратегия
План разделен на три основных этапа:
1.  **Интеграция "как есть"**: Замена `dalic` на `Lib_DALI` с минимальными изменениями, используя ее оригинальную реализацию на таймерах. Это позволит быстро проверить работоспособность основной логики и отделить этап интеграции от этапа рефакторинга.
2.  **Реализация сниффера**: Добавление функционала пассивного прослушивания шины на базе уже интегрированной библиотеки.
3.  **Переход на RMT**: Переписывание низкоуровневой части новой библиотеки для использования аппаратного RMT-модуля ESP32, что повысит надежность и снизит нагрузку на CPU.

---

### Фаза 1: Интеграция `Lib_DALI` в существующий проект

**Цель**: Заменить `dalic` на `Lib_DALI`, заставив ее работать в текущей архитектуре с минимальными изменениями в высокоуровневой логике (`DaliAPI`, `DaliDeviceController` и т.д.).

**Шаг 1.1: Подготовка структуры проекта**

1.  **Создайте новую директорию для драйвера**: В `src/DaliMQTT/driver/` создайте новую папку, например, `dali_ng` (next generation).
2.  **Скопируйте файлы**: Поместите `DALI_Lib.cpp` и `DALI_Lib.h` в эту новую директорию. Также скопируйте `dali_commands.h` из старого драйвера `dalic/include` в `dali_ng`, так как `Lib_DALI` его не предоставляет, а ваш код его использует.
3.  **Создайте CMake-файл для нового драйвера**: В `src/DaliMQTT/driver/dali_ng/` создайте файл `CMakeLists.txt`:
4. 
**Шаг 1.3: Адаптация `DaliAPI` (ключевой шаг)**

Теперь нужно переписать `DaliAPI.cxx` и `DaliAPI.hxx`, чтобы они работали с C++ классом `Dali` из `Lib_DALI`, а не с C-функциями из `dalic`.

1.  **`DaliAPI.hxx`**:
    *   Замените `#include "dalic/include/dali.h"` на `#include "DALI_Lib.h"`.
    *   Добавьте приватное поле для экземпляра новой библиотеки и таймера:
    ```c++
    #include "DALI_Lib.h"
    #include "esp_timer.h" // Нужно для таймера
    // ...
    class DaliAPI {
    // ...
    private:
        DaliAPI() = default;

        Dali m_dali_impl; // Экземпляр новой библиотеки
        esp_timer_handle_t m_dali_timer{nullptr}; // Таймер для bit-banging

        std::mutex bus_mutex;
        std::atomic<bool> m_initialized{false};
        QueueHandle_t m_dali_event_queue{nullptr};
    };
    ```

2.  **`DaliAPI.cxx`**: Это потребует значительных изменений.

    *   **Создайте callback-функции для GPIO**: `Lib_DALI` использует функции для управления пинами.
    ```c++
    // В начало файла DaliAPI.cxx
    #include "DaliAPI.hxx"
    #include "sdkconfig.h"
    #include <driver/gpio.h>

    static uint8_t bus_is_high() {
        return gpio_get_level((gpio_num_t)CONFIG_DALI2MQTT_DALI_RX_PIN);
    }
    static void bus_set_low() {
        gpio_set_level((gpio_num_t)CONFIG_DALI2MQTT_DALI_TX_PIN, 0);
    }
    static void bus_set_high() {
        gpio_set_level((gpio_num_t)CONFIG_DALI2MQTT_DALI_TX_PIN, 1);
    }

    // Callback для таймера
    static void IRAM_ATTR dali_timer_callback(void* arg) {
        Dali* dali_instance = static_cast<Dali*>(arg);
        dali_instance->timer();
    }
    ```

    *   **Перепишите `DaliAPI::init`**:
    ```c++
    esp_err_t DaliAPI::init(gpio_num_t rx_pin, gpio_num_t tx_pin) {
        if (m_initialized) return ESP_OK;
        
        // Настройка GPIO
        gpio_config_t tx_conf = {
            .pin_bit_mask = (1ULL << tx_pin),
            .mode = GPIO_MODE_OUTPUT_OD, // DALI bus is open-drain
            .pull_up_en = GPIO_PULLUP_DISABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE
        };
        gpio_config(&tx_conf);
        gpio_set_level(tx_pin, 1); // Release bus

        gpio_config_t rx_conf = {
            .pin_bit_mask = (1ULL << rx_pin),
            .mode = GPIO_MODE_INPUT,
            .pull_up_en = GPIO_PULLUP_ENABLE, // DALI bus is high when idle
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE
        };
        gpio_config(&rx_conf);

        m_dali_impl.begin(bus_is_high, bus_set_high, bus_set_low);

        // Настройка таймера для вызова dali.timer() с частотой ~9600 Гц (104 мкс)
        const esp_timer_create_args_t timer_args = {
            .callback = &dali_timer_callback,
            .arg = &m_dali_impl,
            .name = "dali_timer"
        };
        ESP_ERROR_CHECK(esp_timer_create(&timer_args, &m_dali_timer));
        ESP_ERROR_CHECK(esp_timer_start_periodic(m_dali_timer, 104)); // 104 us period

        if (!m_dali_event_queue) {
            // ... (остальная логика создания очереди)
        }
        
        m_initialized = true;
        ESP_LOGI(TAG, "DaliAPI initialized with Lib_DALI (timer mode)");
        return ESP_OK;
    }
    ```

    *   **Перепишите `sendCommand` и `sendQuery`**:
        *   `Lib_DALI` не имеет разделения на `is_cmd` и `send_twice`. Команды отправляются через `cmd()`.
        *   Адресация в `cmd()` немного отличается. Аргумент `adr` - это `YAAAAAA`.
        *   Вам нужно будет создать маппинг. Например, `DALI_ADDRESS_TYPE_SHORT` -> `arg = address`, `DALI_ADDRESS_TYPE_GROUP` -> `arg = 0b01000000 | address`, `DALI_ADDRESS_TYPE_BROADCAST` -> `arg = 0b01111111`.
        *   Для `sendQuery` используйте `cmd()`, который возвращает `int16_t`. Если результат >= 0, это ответ.
    ```c++
    // Примерная реализация
    esp_err_t DaliAPI::sendCommand(dali_addressType_t addr_type, uint8_t addr, uint8_t command, bool send_twice) {
        std::lock_guard lock(bus_mutex);
        uint8_t dali_addr;
        uint16_t dali_cmd = command;

        // Определяем адрес для Lib_DALI
        if (addr_type == DALI_ADDRESS_TYPE_SHORT) dali_addr = addr;
        else if (addr_type == DALI_ADDRESS_TYPE_GROUP) dali_addr = 0x40 | addr;
        else if (addr_type == DALI_ADDRESS_TYPE_BROADCAST) dali_addr = 0x7F;
        else { // Special command
            dali_cmd = command | 0x0100; // Lib_DALI использует флаги в старших битах
            dali_addr = 0; // Для спец. команд arg - это данные
        }

        if (send_twice) dali_cmd |= 0x0200; // Флаг повтора
        
        m_dali_impl.cmd(dali_cmd, dali_addr);
        return ESP_OK;
    }

    std::optional<uint8_t> DaliAPI::sendQuery(dali_addressType_t addr_type, uint8_t addr, uint8_t command) {
        std::lock_guard lock(bus_mutex);
        // Логика аналогична sendCommand
        // ...
        int16_t result = m_dali_impl.cmd(command, dali_addr);
        if (result >= 0) {
            return static_cast<uint8_t>(result);
        }
        return std::nullopt;
    }
    ```
    *   **`scanBus` и `initializeBus`**: Перепишите их, используя новые функции `m_dali_impl.commission()`, `m_dali_impl.set_searchaddr()`, `m_dali_impl.compare()` и т.д. Логика в `Lib_DALI` уже есть, ее просто нужно вызвать из вашего API.

После этого этапа у вас должен быть работающий проект, который отправляет команды DALI, но еще не умеет их слушать.

---

### Фаза 2: Реализация сниффера

**Цель**: Реализовать пассивное прослушивание шины и отправку полученных команд в очередь `m_dali_event_queue`.

**Шаг 2.1: Создание задачи-сниффера**

1.  В `DaliAPI::startSniffer` создайте задачу FreeRTOS, которая будет опрашивать состояние `Lib_DALI`.
2.  В `DaliAPI::stopSniffer` удаляйте эту задачу.

**Шаг 2.2: Логика задачи-сниффера**

Задача будет работать в бесконечном цикле и делать следующее:

```c++
// В DaliAPI.cxx
static void dali_sniffer_task(void* arg) {
    DaliAPI* self = static_cast<DaliAPI*>(arg);
    uint8_t decoded_data[4]; // Буфер для декодированных данных

    while(true) {
        // Опрашиваем состояние приемника из Lib_DALI
        uint8_t bit_len = self->m_dali_impl.rx(decoded_data);

        if (bit_len > 2) { // Если успешно принят кадр
            dali_frame_t frame = {};
            if (bit_len == 8) {
                frame.is_backward_frame = true;
                frame.data = decoded_data[0];
            } else if (bit_len == 16) {
                frame.is_backward_frame = false;
                frame.data = (decoded_data[0] << 8) | decoded_data[1];
            } else {
                // Нестандартная длина, игнорируем
                continue;
            }

            // Отправляем в очередь
            if (self->getEventQueue()) {
                xQueueSend(self->getEventQueue(), &frame, 0);
            }
        }
        vTaskDelay(pdMS_TO_TICKS(10)); // Небольшая задержка, чтобы не загружать CPU
    }
}

esp_err_t DaliAPI::startSniffer() {
    if (!m_initialized || !m_dali_event_queue) return ESP_ERR_INVALID_STATE;
    ESP_LOGI(TAG, "Starting DALI sniffer (timer mode)...");
    
    // m_sniffer_task_handle - новое поле TaskHandle_t в DaliAPI.hxx
    xTaskCreate(dali_sniffer_task, "dali_sniffer", 4096, this, 5, &m_sniffer_task_handle);
    return ESP_OK;
}
```

Теперь ваш `DaliDeviceController` должен начать получать события от других мастеров на шине.

---

### Фаза 3: Переход на RMT

**Цель**: Заменить таймер и bit-banging на аппаратный RMT, сохранив при этом остальную логику `Lib_DALI`. Это самый сложный, но и самый важный этап.

**Стратегия**: Мы не будем переписывать `Lib_DALI` с нуля. Вместо этого мы создадим новую реализацию, которая будет использовать RMT, но будет вызываться из тех же методов `DaliAPI`. Лучший способ — инкапсулировать RMT-логику внутри `DaliAPI` и больше не использовать `m_dali_impl` и таймер.

**Шаг 3.1: Отключение старой логики**

В `DaliAPI::init` закомментируйте создание таймера и вызов `m_dali_impl.begin()`.

**Шаг 3.2: Интеграция RMT-кода**

1.  **Скопируйте код из `dalic`**: Возьмите RMT-энкодер (`dali_rmt_tx_encoder_cb`), декодер (`dali_rmt_rx_decoder`) и callback (`dali_rmt_rx_done_cb`) из `dalic/dali.c`. Они уже написаны и отлажены. Поместите их в `DaliAPI.cxx` как `static` функции.
2.  **Инициализация RMT в `DaliAPI::init`**: Вместо старой логики инициализируйте RMT TX и RX каналы, как это сделано в `dalic/dali.c`.
    ```c++
    // В DaliAPI.hxx добавьте поля для RMT
    rmt_channel_handle_t m_rx_channel{nullptr};
    rmt_channel_handle_t m_tx_channel{nullptr};
    rmt_encoder_handle_t m_tx_encoder{nullptr};

    // В DaliAPI::init()
    esp_err_t DaliAPI::init(...) {
        // ...
        // Вместо кода с таймером и m_dali_impl.begin():
        // ... код инициализации RMT каналов из dalic/dali.c ...
        // Убедитесь, что callback `dali_rmt_rx_done_cb` получает `m_dali_event_queue` в качестве user_data
        
        m_initialized = true;
        ESP_LOGI(TAG, "DaliAPI initialized with RMT");
        return ESP_OK;
    }
    ```

**Шаг 3.3: Переписывание `sendCommand`/`sendQuery` под RMT**

Теперь эти функции будут напрямую работать с RMT, как это делала `dali_transaction` в `dalic`.

```c++
// Примерная реализация DaliAPI::sendQuery
std::optional<uint8_t> DaliAPI::sendQuery(dali_addressType_t addr_type, uint8_t addr, uint8_t command) {
    std::lock_guard lock(bus_mutex);
    // ... остановить сниффер ...
    
    uint8_t tx_buffer[2];
    // ... сформировать tx_buffer[0] (адрес) и tx_buffer[1] (команда) ...
    
    rmt_transmit_config_t transmit_config = { .loop_count = 0 };
    ESP_ERROR_CHECK(rmt_transmit(m_tx_channel, m_tx_encoder, tx_buffer, sizeof(tx_buffer), &transmit_config));
    ESP_ERROR_CHECK(rmt_tx_wait_all_done(m_tx_channel, pdMS_TO_TICKS(100)));

    // Ожидание ответа
    rmt_symbol_word_t raw_symbols[32]; // Буфер для ответа (8 бит)
    rmt_receive_config_t rx_config = { .signal_range_min_ns = ..., .signal_range_max_ns = ... };
    
    // Очищаем очередь перед приемом
    xQueueReset(m_dali_event_queue);
    
    rmt_enable(m_rx_channel);
    rmt_receive(m_rx_channel, raw_symbols, sizeof(raw_symbols), &rx_config);
    
    rmt_rx_done_event_data_t rx_data;
    if (xQueueReceive(m_dali_event_queue, &rx_data, pdMS_TO_TICKS(CONFIG_DALI2MQTT_DALI_TRANSACTION_TIMEOUT_MS)) == pdPASS) {
        // ... декодировать rx_data.received_symbols в 8-битный ответ, используя `dali_rmt_rx_decoder` ...
        // ... вернуть результат ...
    }
    
    rmt_disable(m_rx_channel);
    // ... перезапустить сниффер ...
    return std::nullopt;
}
```

**Шаг 3.4: Переписывание сниффера под RMT**

Это ключевое улучшение. Вместо опроса в задаче мы будем получать события от RMT.

1.  **Callback**: callback `dali_rmt_rx_done_cb` уже отправляет данные в очередь. Нам нужно, чтобы он отправлял их в `m_dali_event_queue`.
2.  **Задача-декодер**: Задача, которую запускает `startSniffer`, теперь будет не опрашивать, а ждать данные в этой очереди (`m_dali_event_queue`).
3.  **Логика задачи**:
    *   Ждет `rmt_rx_done_event_data_t` из очереди.
    *   Вызывает `dali_rmt_rx_decoder` для разбора `received_symbols`.
    *   **Важно**: декодер должен уметь определять длину фрейма (8 или 16 бит). Код из `dalic/dali_sniffer.c` уже это делает.
    *   Формирует `dali_frame_t` и отправляет ее в ту же очередь, на которую подписан `DaliDeviceController` (или в новую, если хотите разделить).
    *   Сразу же перезапускает прием RMT: `rmt_receive(...)`.

```c++
// DaliAPI.cxx
static bool IRAM_ATTR dali_rx_done_callback_for_sniffer(rmt_channel_handle_t channel, const rmt_rx_done_event_data_t *edata, void *user_data) {
    BaseType_t high_task_wakeup = pdFALSE;
    QueueHandle_t sniffer_raw_queue = (QueueHandle_t)user_data;
    xQueueSendFromISR(sniffer_raw_queue, edata, &high_task_wakeup);
    return high_task_wakeup == pdTRUE;
}

// Задача для RMT-сниффера
static void rmt_sniffer_task(void* arg) {
    DaliAPI* self = static_cast<DaliAPI*>(arg);
    // ...
    rmt_symbol_word_t* rmt_buffer = ...;
    
    // Запускаем постоянный прием
    rmt_receive(self->m_rx_channel, rmt_buffer, ...);

    while(true) {
        rmt_rx_done_event_data_t rx_data;
        if (xQueueReceive(self->m_sniffer_raw_queue, &rx_data, portMAX_DELAY) == pdPASS) {
            // Декодируем фрейм (логика из dalic/dali_sniffer.c)
            // ...
            if (bit_count == 8 || bit_count == 16) {
                // Отправляем в очередь для DaliDeviceController
                xQueueSend(self->getEventQueue(), &decoded_frame, 0);
            }

            // Перезапускаем прием
            rmt_receive(self->m_rx_channel, rmt_buffer, ...);
        }
    }
}
```

### Заключение

Этот план позволяет выполнить миграцию итеративно, проверяя работоспособность на каждом этапе.
-   **Фаза 1** даст вам работающий DALI-мастер на базе новой библиотеки.
-   **Фаза 2** добавит ключевую для вас функцию сниффера.
-   **Фаза 3** сделает решение надежным и эффективным, используя аппаратные возможности ESP32.

Ключ к успеху — это хорошее C++ API-обертка (`DaliAPI`), которое у вас уже есть. Оно изолирует остальную часть приложения от деталей низкоуровневой реализации, позволяя вам менять "движок" под капотом, не переписывая всю бизнес-логику.