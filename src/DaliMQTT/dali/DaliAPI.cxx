#include "DaliAPI.hxx"

namespace daliMQTT
{
    static constexpr char TAG[] = "DaliAPI";
    constexpr int DALI_EVENT_QUEUE_SIZE = 32;
    constexpr uint32_t DALI_TIMER_RESOLUTION_HZ = 24000000; // 24MHz
    constexpr uint32_t DALI_TIMER_ALARM_PERIOD_US = 2500; // 24'000'000 / 9600 = 2500

    static uint8_t IRAM_ATTR bus_is_high() {
        return gpio_get_level(static_cast<gpio_num_t>(CONFIG_DALI2MQTT_DALI_RX_PIN));
    }
    static void IRAM_ATTR bus_set_low() {
        gpio_set_level(static_cast<gpio_num_t>(CONFIG_DALI2MQTT_DALI_TX_PIN), 1);
    }
    static void IRAM_ATTR bus_set_high() {
        gpio_set_level(static_cast<gpio_num_t>(CONFIG_DALI2MQTT_DALI_TX_PIN), 0);
    }

    static bool IRAM_ATTR dali_timer_isr_callback([[maybe_unused]] gptimer_handle_t timer, [[maybe_unused]] const gptimer_alarm_event_data_t *edata, void *user_ctx) {
        if (const auto dali_instance = static_cast<Dali *>(user_ctx)) {
            dali_instance->timer();
        }
        return false;
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

        m_dali_impl.cmd(dali_cmd, dali_arg, false);
        ESP_LOGD(TAG,"Executed Command: %u", dali_cmd);
        vTaskDelay(pdMS_TO_TICKS(CONFIG_DALI2MQTT_DALI_INTER_FRAME_DELAY_MS));
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

    std::optional<uint8_t> DaliAPI::sendRaw(const uint32_t data, const uint8_t bits, const bool reply) {
        std::lock_guard lock(bus_mutex);
        std::optional<int16_t> result = std::nullopt;

        if (bits == 16) {
            const uint8_t b1 = (data >> 8) & 0xFF;
            const uint8_t b2 = data & 0xFF;

            if (reply) {
                result = m_dali_impl.tx_wait_rx(b1, b2);
                ESP_LOGD(TAG, "Raw TX (16bit with Reply): 0x%02X 0x%02X -> Result: %d", b1, b2, *result);
            } else {
                uint8_t frame[2] = {b1, b2};
                m_dali_impl.tx_wait(frame, 16);
                ESP_LOGD(TAG, "Raw TX (16bit No Reply): 0x%02X 0x%02X", b1, b2);
            }
        }
        else if (bits == 24) {
            const uint8_t b1 = (data >> 16) & 0xFF;
            const uint8_t b2 = (data >> 8) & 0xFF;
            const uint8_t b3 = data & 0xFF;

            if (reply) {
                result = m_dali_impl.tx_wait_rx(b1, b2, b3);
                ESP_LOGD(TAG, "Raw TX (24bit with Reply): 0x%02X 0x%02X 0x%02X -> Result: %d", b1, b2, b3, result);
            } else {
                uint8_t frame[3] = {b1, b2, b3};
                m_dali_impl.tx_wait(frame, 24);
                ESP_LOGD(TAG, "Raw TX (24bit No Reply): 0x%02X 0x%02X 0x%02X", b1, b2, b3);
            }
        }
        else {
            ESP_LOGW(TAG, "Invalid bit length for sendRaw: %d", bits);
            return std::nullopt;
        }
        vTaskDelay(pdMS_TO_TICKS(CONFIG_DALI2MQTT_DALI_INTER_FRAME_DELAY_MS));
        if (result.has_value() && result >= 0) {
            return static_cast<uint8_t>(*result);
        }
        return std::nullopt;
    }

    std::optional<uint8_t> DaliAPI::sendInputDeviceCommand(const uint8_t shortAddress, const uint8_t opcode, const std::optional<uint8_t> param) {
        std::lock_guard lock(bus_mutex);
        const uint8_t addr_byte = (shortAddress << 1) | 1;
        const uint8_t param_byte = param.value_or(0x00);

        const int16_t result = m_dali_impl.tx_wait_rx(addr_byte, opcode, param_byte);

        vTaskDelay(pdMS_TO_TICKS(CONFIG_DALI2MQTT_DALI_INTER_FRAME_DELAY_MS));


        if (result >= 0) {
            return static_cast<uint8_t>(result);
        }
        return std::nullopt;
    }
    uint8_t DaliAPI::initializeBus() {
        std::lock_guard lock(bus_mutex);
        ESP_LOGI(TAG, "Starting DALI commissioning process...");
        uint8_t assigned_devices = m_dali_impl.commission(0xff);
        ESP_LOGI(TAG, "Commissioning finished. Assigned %u devices", assigned_devices);
        return assigned_devices;
    }
    uint8_t DaliAPI::initialize24BitDevicesBus() {
        std::lock_guard lock(bus_mutex);
        ESP_LOGI(TAG, "Starting DALI commissioning process (Input Devices)...");
        uint8_t assigned_devices = m_dali_impl.commission_id(0xff);
        ESP_LOGI(TAG, "Input Device Commissioning finished. Assigned %u devices", assigned_devices);
        return assigned_devices;
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
    std::optional<uint8_t> DaliAPI::getDeviceType(const uint8_t shortAddress) {
        // QUERY DEVICE TYPE
        return sendQuery(DALI_ADDRESS_TYPE_SHORT, shortAddress, DALI_COMMAND_QUERY_DEVICE_TYPE);
    }

    std::optional<uint8_t> DaliAPI::getDeviceStatus(const uint8_t shortAddress) {
        // QUERY STATUS
        return sendQuery(DALI_ADDRESS_TYPE_SHORT, shortAddress, DALI_COMMAND_QUERY_STATUS);
    }

    std::optional<std::string> DaliAPI::getGTIN(const uint8_t shortAddress) {
        std::lock_guard lock(bus_mutex);

        m_dali_impl.set_dtr1(0, shortAddress);

        std::string gtin_res;
        for (uint8_t offset = 0x03; offset <= 0x08; ++offset) {
            m_dali_impl.set_dtr0(offset, shortAddress);
            const int16_t byte_val = m_dali_impl.cmd(DALI_READ_MEMORY_LOCATION, shortAddress);

            if (byte_val >= 0) {
                char buf[4];
                snprintf(buf, sizeof(buf), "%02X", static_cast<uint8_t>(byte_val));
                gtin_res += buf;
            } else {
                return std::nullopt;
            }
            vTaskDelay(pdMS_TO_TICKS(5));
        }
        return gtin_res;
    }

     esp_err_t DaliAPI::sendSpecialCmdDT8(const uint8_t shortAddr, const uint8_t cmd) {
        m_dali_impl.cmd(DALI_SPECIAL_COMMAND_ENABLE_DEVICE_TYPE_X | 0x0100, 8, false);
        m_dali_impl.cmd(cmd, shortAddr, false);
        return ESP_OK;
    }
    static uint8_t make_dali_command_address(const dali_addressType_t type, const uint8_t addr) {
        switch (type) {
            case DALI_ADDRESS_TYPE_SHORT:
                return addr;
            case DALI_ADDRESS_TYPE_GROUP:
                return 0x40 | (addr & 0x0F);
            case DALI_ADDRESS_TYPE_BROADCAST:
                return 0x7F;
            default:
                return addr;
        }
    }
    esp_err_t DaliAPI::setDT8ColorTemp(const dali_addressType_t addr_type, const uint8_t addr, const uint16_t mireds) {
        std::lock_guard lock(bus_mutex);

        m_dali_impl.cmd(DALI_SPECIAL_COMMAND_DATA_TRANSFER_REGISTER_1, (mireds >> 8) & 0xFF, false);
        m_dali_impl.cmd(DALI_SPECIAL_COMMAND_DATA_TRANSFER_REGISTER, mireds & 0xFF, false);
        const uint8_t target_addr_byte = make_dali_command_address(addr_type, addr);

        sendSpecialCmdDT8(target_addr_byte, DALI_COMMAND_DT8_SET_COLOUR_TEMP_TC);
        sendSpecialCmdDT8(target_addr_byte, DALI_COMMAND_DT8_ACTIVATE);

        ESP_LOGD(TAG, "DT8 Set Tc: %d mireds to AddrType %d, Val %d", mireds, addr_type, addr);
        vTaskDelay(pdMS_TO_TICKS(CONFIG_DALI2MQTT_DALI_INTER_FRAME_DELAY_MS));
        return ESP_OK;
    }

    esp_err_t DaliAPI::setDT8RGB(const dali_addressType_t addr_type, const uint8_t addr, const uint8_t r, const uint8_t g, const uint8_t b) {
        std::lock_guard lock(bus_mutex);
        const uint8_t target_addr_byte = make_dali_command_address(addr_type, addr);

        m_dali_impl.cmd(DALI_SPECIAL_COMMAND_DATA_TRANSFER_REGISTER_1, 0x01, false); // DTR1 = Mask R
        m_dali_impl.cmd(DALI_SPECIAL_COMMAND_DATA_TRANSFER_REGISTER, r, false);      // DTR0 = Value R
        sendSpecialCmdDT8(target_addr_byte, DALI_COMMAND_DT8_SET_TEMPORARY_RGB_DIMLEVEL);

        m_dali_impl.cmd(DALI_SPECIAL_COMMAND_DATA_TRANSFER_REGISTER_1, 0x02, false); // DTR1 = Mask G
        m_dali_impl.cmd(DALI_SPECIAL_COMMAND_DATA_TRANSFER_REGISTER, g, false);      // DTR0 = Value G
        sendSpecialCmdDT8(target_addr_byte, DALI_COMMAND_DT8_SET_TEMPORARY_RGB_DIMLEVEL);

        m_dali_impl.cmd(DALI_SPECIAL_COMMAND_DATA_TRANSFER_REGISTER_1, 0x04, false); // DTR1 = Mask B
        m_dali_impl.cmd(DALI_SPECIAL_COMMAND_DATA_TRANSFER_REGISTER, b, false);      // DTR0 = Value B
        sendSpecialCmdDT8(target_addr_byte, DALI_COMMAND_DT8_SET_TEMPORARY_RGB_DIMLEVEL);

        sendSpecialCmdDT8(target_addr_byte, DALI_COMMAND_DT8_ACTIVATE);
        ESP_LOGD(TAG, "DT8 Set RGB: %d,%d,%d to AddrType %d, Val %d", r, g, b, addr_type, addr);
        vTaskDelay(pdMS_TO_TICKS(CONFIG_DALI2MQTT_DALI_INTER_FRAME_DELAY_MS));
        return ESP_OK;
    }

    std::optional<uint8_t> DaliAPI::getDT8Features(const uint8_t shortAddress) {
        std::lock_guard lock(bus_mutex);
        m_dali_impl.cmd(DALI_SPECIAL_COMMAND_ENABLE_DEVICE_TYPE_X | 0x0100, 8, false);

        const int16_t result = m_dali_impl.cmd(DALI_COMMAND_DT8_QUERY_COLOUR_TYPE_FEATURES, shortAddress);

        vTaskDelay(pdMS_TO_TICKS(CONFIG_DALI2MQTT_DALI_INTER_FRAME_DELAY_MS));

        if (result >= 0) {
            return static_cast<uint8_t>(result);
        }
        return std::nullopt;
    }

      std::optional<uint8_t> DaliAPI::readMemoryLocation(const uint8_t shortAddress, const uint8_t bank, const uint8_t offset) {
        std::lock_guard lock(bus_mutex);

        m_dali_impl.set_dtr1(bank, shortAddress);
        m_dali_impl.set_dtr0(offset, shortAddress);
        const int16_t result = m_dali_impl.cmd(DALI_READ_MEMORY_LOCATION, shortAddress);

        vTaskDelay(pdMS_TO_TICKS(CONFIG_DALI2MQTT_DALI_INTER_FRAME_DELAY_MS));

        if (result >= 0) {
            return static_cast<uint8_t>(result);
        }
        ESP_LOGW(TAG, "Failed to read memory loc: SA=%d, Bank=%d, Off=%d, Res=%d", shortAddress, bank, offset, result);
        return std::nullopt;
    }
    std::optional<uint8_t> DaliAPI::queryDT8Value(const uint8_t shortAddress, const uint8_t dtr0_selector) {
        std::lock_guard lock(bus_mutex);

        if (m_dali_impl.set_dtr0(dtr0_selector, shortAddress) != 0) {
            ESP_LOGW(TAG, "Failed to set DTR0 for SA %d", shortAddress);
            return std::nullopt;
        }
        m_dali_impl.cmd(DALI_SPECIAL_COMMAND_ENABLE_DEVICE_TYPE_X | 0x0100, 8, false);
        const int16_t result = m_dali_impl.cmd(DALI_COMMAND_DT8_QUERY_COLOUR_VALUE, shortAddress);

        vTaskDelay(pdMS_TO_TICKS(CONFIG_DALI2MQTT_DALI_INTER_FRAME_DELAY_MS));

        if (result >= 0) {
            return static_cast<uint8_t>(result);
        }
        return std::nullopt;
    }
    std::optional<uint16_t> DaliAPI::getDT8ColorTemp(const uint8_t shortAddress) {
        const auto msb = queryDT8Value(shortAddress, 0x00);
        if (!msb) return std::nullopt;

        const auto lsb = queryDT8Value(shortAddress, 0x01);
        if (!lsb) return std::nullopt;

        return (static_cast<uint16_t>(*msb) << 8) | *lsb;
    }

    std::optional<DaliRGB> DaliAPI::getDT8RGB(const uint8_t shortAddress) {
        const auto r = queryDT8Value(shortAddress, 0x00);
        if (!r) return std::nullopt;

        const auto g = queryDT8Value(shortAddress, 0x01);
        if (!g) return std::nullopt;

        const auto b = queryDT8Value(shortAddress, 0x02);
        if (!b) return std::nullopt;

        return DaliRGB{ *r, *g, *b };
    }

    bool DaliAPI::isInitialized() const {
        return m_initialized;
    }
} // daliMQTT