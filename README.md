# dali-to-MQTT bridge for ESP32

Этот проект предоставляет прошивку для микроконтроллеров ESP32, которая выступает в роли моста (шлюза) между шиной управления освещением DALI и MQTT брокером. Он позволяет отслеживать состояние и управлять светильниками DALI (как индивидуально, так и группами) через протокол MQTT, облегчая интеграцию с платформами умного дома, такими как Home Assistant.

## Возможности

*   **Управление DALI:** Включение/выключение и установка яркости для отдельных устройств (адреса 0-63) и групп (0-15) через MQTT.
*   **Мониторинг DALI:** Периодический опрос статуса (уровня яркости) выбранных устройств и групп.
*   **Публикация статуса:** Отправка текущего состояния (ВКЛ/ВЫКЛ, яркость) в соответствующие топики MQTT в формате JSON (совместимо с Home Assistant). Публикация происходит только при изменении состояния.
*   **Прием команд:** Обработка команд управления светом, полученных из MQTT.
*   **Конфигурация через MQTT:** Изменение интервала опроса DALI и масок опрашиваемых устройств/групп. Запрос текущей конфигурации.
*   **Управление группами DALI:** Добавление и удаление устройств из групп DALI через MQTT.
*   **Интеграция с Home Assistant:** Автоматическое обнаружение устройств через MQTT Discovery.
*   **Персистентность:** Сохранение настроек (WiFi, MQTT, параметры опроса) в NVS (Non-Volatile Storage) ESP32.
*   **Статус доступности:** Мост сообщает о своем онлайн/офлайн статусе через MQTT Last Will and Testament (LWT).

## Установка и сборка

**Предварительные требования:**

*   Установленный **ESP-IDF** (Espressif IoT Development Framework)
*   **CMake** и **Ninja**
*   **Python 3**
*   Плата ESP32 и DALI интерфейс (трансивер)

**Сборка:**

1.  **Клонировать репозиторий:**

2.  **Настроить окружение ESP-IDF:**
    ```bash
    source /path/to/esp-idf/export.sh
    ```

3.  **Настроить проект:**
    ```bash
    cmake -B build -DCMAKE_TOOLCHAIN_FILE=/path/to/esp-idf/tools/cmake/toolchain-esp32.cmake -DTARGET=esp32 -GNinja .
    cmake --build build --target menuconfig
    ```

4.  **Собрать проект:**
    ```bash
    cmake --build build
    ```

5.  **Прошить и запустить монитор:**
    ```bash
    cmake --build build --target app-flash monitor
    cmake --build build --target monitor # Запуск монитора
    ```

## Конфигурация во время выполнения

*   **Начальные значения:** Задаются через menuconfig перед сборкой.
*   **Через MQTT:** Мост подписывается на топики для изменения конфигурации:
    *   `dali_bridge/config/set` (JSON): Изменение интервала опроса (`poll_interval_ms`).
    *   `dali_bridge/config/group/set` (JSON): Управление членством устройств в группах (`{"action": "add/remove", "device_short_address": X, "group_number": Y}`).
*   **Запрос конфигурации:** Отправьте пустое сообщение в `dali_bridge/config/get`, ответ придет в `dali_bridge/config/get/response`.
*   **Хранение:** Конфигурация сохраняется в NVS и восстанавливается после перезагрузки.

## MQTT Топики

*   **Доступность:** `dali_bridge/status` (Сообщения: `online` / `offline`, LWT)
*   **Состояние светильника/группы:** `dali_bridge/light/<type>/<addr>/state` (где `<type>` = `device` или `group`, `<addr>` = 0-63 или 0-15). Публикуется JSON, например: `{"state": "ON", "brightness": 128}`. (Retained)
*   **Команда светильнику/группе:** `dali_bridge/light/<type>/<addr>/set`. Принимает JSON, например: `{"state": "OFF"}`, `{"brightness": 200}`, `{"state": "ON", "brightness": 50}`.
*   **Установка конфигурации:** `dali_bridge/config/set` (JSON)
*   **Запрос конфигурации:** `dali_bridge/config/get` (Пустое сообщение)
*   **Ответ на запрос конфигурации:** `dali_bridge/config/get/response` (JSON)
*   **Управление группами:** `dali_bridge/config/group/set` (JSON)
*   **Результат управления группами:** `dali_bridge/config/group/result` (JSON)

## Интеграция с Home Assistant

*   Мост использует **MQTT Discovery**.
*   После первого подключения к MQTT брокеру, устройства и группы, указанные в масках опроса (`poll_groups_mask`, `poll_devices_mask`), должны автоматически появиться в Home Assistant как `light` компоненты.
*   Для управления членством в группах из интерфейса HA рекомендуется использовать `input_number`, `input_select` и `script` для отправки команды в топик `dali_bridge/config/group/set`.