#include "sdkconfig.h"
#include "DaliAPI.hxx"

namespace daliMQTT
{
    static constexpr char TAG[] = "DaliAPI";
    constexpr int DALI_EVENT_QUEUE_SIZE = 20;
    constexpr uint32_t DALI_TIMER_RESOLUTION_HZ = 24000000; // 24MHz
    constexpr uint32_t DALI_TIMER_ALARM_PERIOD_US = 2500; // 24'000'000 / 9600 = 2500

    static uint8_t bus_is_high() {
        return gpio_get_level(static_cast<gpio_num_t>(CONFIG_DALI2MQTT_DALI_RX_PIN));
    }
    static void bus_set_low() {
        gpio_set_level(static_cast<gpio_num_t>(CONFIG_DALI2MQTT_DALI_TX_PIN), 1);
    }
    static void bus_set_high() {
        gpio_set_level(static_cast<gpio_num_t>(CONFIG_DALI2MQTT_DALI_TX_PIN), 0);
    }

    static bool IRAM_ATTR dali_timer_isr_callback([[maybe_unused]] gptimer_handle_t timer, [[maybe_unused]] const gptimer_alarm_event_data_t *edata, void *user_ctx) {
        if (const auto dali_instance = static_cast<Dali *>(user_ctx)) {
            dali_instance->timer();
        }
        return false;
    }

    [[noreturn]] void DaliAPI::dali_sniffer_task(void* arg) {
        auto* self = static_cast<DaliAPI*>(arg);
        QueueHandle_t queue = self->m_dali_event_queue;

        uint8_t decoded_data[4];

        while (true) {
            ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

            uint8_t bit_len;
            {
                std::lock_guard lock(self->bus_mutex);
                bit_len = self->m_dali_impl.rx(decoded_data);
            }

            if (bit_len > 2) {
                dali_frame_t frame = {};
                frame.length = bit_len;

                if (bit_len == 8) {
                    frame.is_backward_frame = true;
                    frame.data = decoded_data[0];
                    ESP_LOGD(TAG, "Sniffed Backward Frame: 0x%02X", frame.data);
                } else if (bit_len == 16) {
                    frame.is_backward_frame = false;
                    frame.data = (decoded_data[0] << 8) | decoded_data[1];
                    ESP_LOGD(TAG, "Sniffed Forward Frame: 0x%04X", frame.data);
                } else if (bit_len == 24) {
                    frame.is_backward_frame = false;
                    frame.data = (static_cast<uint32_t>(decoded_data[0]) << 16) |
                                 (static_cast<uint32_t>(decoded_data[1]) << 8) |
                                 decoded_data[2];
                    ESP_LOGD(TAG, "Sniffed Forward Frame (24): 0x%06lX", frame.data);
                } else {
                    ESP_LOGW(TAG, "Sniffed frame with unusual bit length: %d", bit_len);
                    continue;
                }

                if (queue) {
                    xQueueSend(queue, &frame, 0);
                }
            }
        }
    }


    esp_err_t DaliAPI::init(gpio_num_t rx_pin, gpio_num_t tx_pin) {
        if (m_initialized) {
            return ESP_OK;
        }

        ESP_LOGI(TAG, "Configuring DALI GPIOs: RX=%d, TX=%d", rx_pin, tx_pin);
        gpio_config_t tx_conf = {
            .pin_bit_mask = (1ULL << tx_pin),
            .mode = GPIO_MODE_OUTPUT,
            .pull_up_en = GPIO_PULLUP_DISABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE,
        };
        ESP_ERROR_CHECK(gpio_config(&tx_conf));

        gpio_config_t rx_conf = {
            .pin_bit_mask = (1ULL << rx_pin),
            .mode = GPIO_MODE_INPUT,
            .pull_up_en = GPIO_PULLUP_DISABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE,
        };
        ESP_ERROR_CHECK(gpio_config(&rx_conf));

        bus_set_high();

        m_dali_impl.begin(bus_is_high, bus_set_low, bus_set_high);
        m_dali_impl.setRxCallback(rx_complete_isr, this);

        ESP_LOGI(TAG, "Configuring DALI GPTimer...");
        gptimer_config_t timer_config = {
            .clk_src = GPTIMER_CLK_SRC_DEFAULT,
            .direction = GPTIMER_COUNT_UP,
            .resolution_hz = DALI_TIMER_RESOLUTION_HZ,
        };
        ESP_ERROR_CHECK(gptimer_new_timer(&timer_config, &m_dali_timer));

        gptimer_event_callbacks_t cbs = {
            .on_alarm = dali_timer_isr_callback,
        };
        ESP_ERROR_CHECK(gptimer_register_event_callbacks(m_dali_timer, &cbs, &m_dali_impl));
        ESP_ERROR_CHECK(gptimer_enable(m_dali_timer));

        gptimer_alarm_config_t alarm_config = {
            .alarm_count = DALI_TIMER_ALARM_PERIOD_US,
            .reload_count = 0,
            .flags = {
                .auto_reload_on_alarm = true,
            },
        };
        ESP_ERROR_CHECK(gptimer_set_alarm_action(m_dali_timer, &alarm_config));
        ESP_ERROR_CHECK(gptimer_start(m_dali_timer));

        if (!m_dali_event_queue) {
            m_dali_event_queue = xQueueCreate(DALI_EVENT_QUEUE_SIZE, sizeof(dali_frame_t));
            if (!m_dali_event_queue) {
                ESP_LOGE(TAG, "Failed to create DALI event queue");
                return ESP_ERR_NO_MEM;
            }
        }

        m_initialized = true;
        ESP_LOGI(TAG, "DaliAPI initialized with Lib_DALI (timer mode)");
        return ESP_OK;
    }

    esp_err_t DaliAPI::startSniffer() {
        if (!m_initialized) return ESP_ERR_INVALID_STATE;
        if (m_sniffer_task_handle) {
            ESP_LOGW(TAG, "Sniffer task already running.");
            return ESP_OK;
        }
        ESP_LOGI(TAG, "Starting DALI sniffer task...");
        xTaskCreate(DaliAPI::dali_sniffer_task, "dali_sniffer_task", 4096, this, 5, &m_sniffer_task_handle);        return ESP_OK;
    }

    esp_err_t DaliAPI::stopSniffer() {
        if (m_sniffer_task_handle) {
            ESP_LOGI(TAG, "Stopping DALI sniffer task...");
            vTaskDelete(m_sniffer_task_handle);
            m_sniffer_task_handle = nullptr;
        }
        return ESP_OK;
    }

    QueueHandle_t DaliAPI::getEventQueue() const {
        return m_dali_event_queue;
    }

    esp_err_t DaliAPI::sendDACP(const dali_addressType_t addr_type, const uint8_t addr, const uint8_t level) {
        std::lock_guard lock(bus_mutex);
        uint8_t dali_addr;
        switch (addr_type) {
            case DALI_ADDRESS_TYPE_SHORT: dali_addr = addr; break;
            case DALI_ADDRESS_TYPE_GROUP: dali_addr = 0x40 | addr; break;
            case DALI_ADDRESS_TYPE_BROADCAST: dali_addr = 0x7F; break;
            default: return ESP_ERR_INVALID_ARG;
        }
        m_dali_impl.set_level(level, dali_addr);
        return ESP_OK;
    }

    esp_err_t DaliAPI::sendCommand(dali_addressType_t addr_type, uint8_t addr, uint8_t command, bool send_twice) {
        std::lock_guard lock(bus_mutex);
        uint8_t dali_arg;
        uint16_t dali_cmd = command;
        if (send_twice) {
            dali_cmd |= 0x0200;
        }
        ESP_LOGD(TAG, "Got Command: %u", dali_cmd);
        if (addr_type == DALI_ADDRESS_TYPE_SPECIAL_CMD) {
            dali_cmd |= 0x0100;
            ESP_LOGD(TAG, "Sending Special Command: %u", dali_cmd);
            dali_arg = addr;
        } else {
            switch (addr_type) {
                case DALI_ADDRESS_TYPE_SHORT:
                    dali_arg = addr;
                    ESP_LOGD(TAG,"Short addr execution command for: %u", dali_arg);
                    break;
                case DALI_ADDRESS_TYPE_GROUP:
                    dali_arg = 0x40 | addr;
                    ESP_LOGD(TAG,"Group command execution command for: %u", dali_arg);
                    break;
                case DALI_ADDRESS_TYPE_BROADCAST:
                    dali_arg = 0x7F;
                    ESP_LOGD(TAG,"Got Broadcast command from: %u with cmd %u", addr, command);
                    break;
                default: return ESP_ERR_INVALID_ARG;
            }
        }

        int16_t result = m_dali_impl.cmd(dali_cmd, dali_arg);
        ESP_LOGD(TAG,"Executed Command: %u with code %u", dali_cmd, result);
        return ESP_OK;
    }

    std::optional<uint8_t> DaliAPI::sendQuery(const dali_addressType_t addr_type, const uint8_t addr, const uint8_t command) {
        std::lock_guard lock(bus_mutex);
        uint8_t dali_arg;
        uint16_t dali_cmd = command;

        if (addr_type == DALI_ADDRESS_TYPE_SPECIAL_CMD) {
            dali_cmd |= 0x0100;
            dali_arg = addr;
        } else {
             switch (addr_type) {
                case DALI_ADDRESS_TYPE_SHORT: dali_arg = addr; break;
                case DALI_ADDRESS_TYPE_GROUP: dali_arg = 0x40 | addr; break;
                case DALI_ADDRESS_TYPE_BROADCAST: dali_arg = 0x7F; break;
                default: return std::nullopt;
            }
        }

        const int16_t result = m_dali_impl.cmd(dali_cmd, dali_arg);
        vTaskDelay(pdMS_TO_TICKS(CONFIG_DALI2MQTT_DALI_INTER_FRAME_DELAY_MS));

        if (result >= 0) {
            return static_cast<uint8_t>(result);
        }
        return std::nullopt;
    }

    std::bitset<64> DaliAPI::scanBus() {
        std::lock_guard lock(bus_mutex);
        std::bitset<64> found_devices;
        ESP_LOGI(TAG, "Starting DALI bus scan...");
        sendCommand(DALI_ADDRESS_TYPE_SHORT, 0, DALI_COMMAND_ON_AND_STEP_UP);
        for (uint8_t i = 0; i < 64; ++i) {
            int16_t rv = m_dali_impl.cmd(DALI_COMMAND_QUERY_STATUS, i);
            if (rv >= 0) {
                ESP_LOGI(TAG, "Device found at short address %d (Status: 0x%02X)", i, rv);
                found_devices.set(i);


                m_dali_impl.set_level(254, i);
                vTaskDelay(pdMS_TO_TICKS(500));
                m_dali_impl.set_level(0, i);
            } else if (-rv != DALI_RESULT_NO_REPLY) {
                ESP_LOGW(TAG, "Scan error at address %d: code %d", i, -rv);
            }

            vTaskDelay(pdMS_TO_TICKS(CONFIG_DALI2MQTT_DALI_POLL_DELAY_MS));
        }

        ESP_LOGI(TAG, "Scan finished. Found %zu devices.", found_devices.count());
        return found_devices;
    }

    std::bitset<64> DaliAPI::initializeBus() {
        std::lock_guard lock(bus_mutex);
        ESP_LOGI(TAG, "Starting DALI commissioning process...");
        m_dali_impl.commission(0xff);
        ESP_LOGI(TAG, "Commissioning finished.");
        return scanBus();
    }

    esp_err_t DaliAPI::assignToGroup(const uint8_t shortAddress, const uint8_t group) {
        if (group >= 16) return ESP_ERR_INVALID_ARG;
        ESP_LOGD(TAG, "Process group addition of %u to %u", shortAddress, group);
        return sendCommand(DALI_ADDRESS_TYPE_SHORT, shortAddress, DALI_COMMAND_ADD_TO_GROUP_0 + group, true);
    }

    esp_err_t DaliAPI::removeFromGroup(const uint8_t shortAddress, const uint8_t group) {
        if (group >= 16) return ESP_ERR_INVALID_ARG;
        return sendCommand(DALI_ADDRESS_TYPE_SHORT, shortAddress, DALI_COMMAND_REMOVE_FROM_GROUP_0 + group, true);
    }

    std::optional<std::bitset<16>> DaliAPI::getDeviceGroups(const uint8_t shortAddress) {
        const auto groups_0_7 = sendQuery(DALI_ADDRESS_TYPE_SHORT, shortAddress, DALI_COMMAND_QUERY_GROUPS_0_7);
        vTaskDelay(pdMS_TO_TICKS(CONFIG_DALI2MQTT_DALI_INTER_FRAME_DELAY_MS));
        const auto groups_8_15 = sendQuery(DALI_ADDRESS_TYPE_SHORT, shortAddress, DALI_COMMAND_QUERY_GROUPS_8_15);

        if (groups_0_7.has_value() && groups_8_15.has_value()) {
            const uint16_t combined = (groups_8_15.value() << 8) | groups_0_7.value();
            return std::bitset<16>(combined);
        }
        return std::nullopt;
    }

    void IRAM_ATTR DaliAPI::rx_complete_isr(void* arg) {
        const auto* self = static_cast<DaliAPI*>(arg);
        BaseType_t xHigherPriorityTaskWoken = pdFALSE;
        if (self->m_sniffer_task_handle) {
            vTaskNotifyGiveFromISR(self->m_sniffer_task_handle, &xHigherPriorityTaskWoken);
        }
        #ifndef traceISR_EXIT_TO_SCHEDULER // underlying macro define
            #define traceISR_EXIT_TO_SCHEDULER()
        #endif
        portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
    }

    std::optional<DaliLongAddress_t> DaliAPI::getLongAddress(const uint8_t shortAddress) {
        const auto h_opt = sendQuery(DALI_ADDRESS_TYPE_SHORT, shortAddress, DALI_COMMAND_QUERY_RANDOM_ADDRESS_H);
        vTaskDelay(pdMS_TO_TICKS(CONFIG_DALI2MQTT_DALI_INTER_FRAME_DELAY_MS));
        const auto m_opt = sendQuery(DALI_ADDRESS_TYPE_SHORT, shortAddress, DALI_COMMAND_QUERY_RANDOM_ADDRESS_M);
        vTaskDelay(pdMS_TO_TICKS(CONFIG_DALI2MQTT_DALI_INTER_FRAME_DELAY_MS));
        const auto l_opt = sendQuery(DALI_ADDRESS_TYPE_SHORT, shortAddress, DALI_COMMAND_QUERY_RANDOM_ADDRESS_L);

        if (h_opt && m_opt && l_opt) {
            return (static_cast<DaliLongAddress_t>(h_opt.value()) << 16) | (static_cast<DaliLongAddress_t>(m_opt.value()) << 8) | l_opt.value();
        }
        return std::nullopt;
    }

    bool DaliAPI::isInitialized() const {
        return m_initialized;
    }
} // daliMQTT