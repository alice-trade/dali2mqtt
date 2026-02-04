// Copyright (c) 2026 Alice-Trade Inc.
// SPDX-License-Identifier: GPL-2.0-or-later

#include "dali/DaliAdapter.hxx"

namespace daliMQTT {
    static constexpr char TAG[] = "DaliAdapter";
    using namespace Commands;

    esp_err_t DaliAdapter::init(gpio_num_t rx_pin, gpio_num_t tx_pin) {
        if (m_initialized) return ESP_OK;

        Driver::DaliDriverConfig drv_cfg = {
            .rx_pin = rx_pin,
            .tx_pin = tx_pin,
        };

        esp_err_t init_result = m_driver.init(drv_cfg);
        if (init_result != ESP_OK) {
            ESP_LOGE(TAG, "Failed to init RMT Driver: %s", esp_err_to_name(init_result));
            return init_result;
        }

        m_dali_event_queue = xQueueCreate(32, sizeof(dali_frame_t));
        m_response_queue = xQueueCreate(1, sizeof(uint8_t));
        xTaskCreate(busWorkerTask, "dali_bus_worker", 4096, this, configMAX_PRIORITIES - 2, &m_worker_task_handle);
        m_initialized = true;

        ESP_LOGI(TAG, "Adapter initialized with DALI Driver (RMT).");
        return ESP_OK;
    }

    bool DaliAdapter::isInitialized() const {
        return m_initialized;
    }

    QueueHandle_t DaliAdapter::getEventQueue() const {
        return m_dali_event_queue;
    }
    esp_err_t DaliAdapter::startSniffer() {
        m_sniffer_enabled = true;
        return ESP_OK;
    }

    esp_err_t DaliAdapter::stopSniffer() {
        m_sniffer_enabled = false;
        return ESP_OK;
    }
    esp_err_t DaliAdapter::sendRaw(const uint32_t data, const uint8_t bits) {
        m_tx_caller_task = xTaskGetCurrentTaskHandle();
        m_waiting_for_tx_result = true;

        std::lock_guard<std::recursive_mutex> lock(m_transaction_mutex);
        int retries = 0;
        constexpr int MAX_RETRIES = 3;
        constexpr int PRIORITY = 2;

        while (retries <= MAX_RETRIES) {
            const esp_err_t res = m_driver.sendAsync(data, bits);
            if (res != ESP_OK) return res;
            m_last_tx_status = Driver::DaliEventType::FrameReceived;
            m_waiting_for_tx_result = true;
            uint32_t wait_ticks = pdMS_TO_TICKS(50);
            uint32_t ulNotificationValue;

            if (xTaskNotifyWait(0, 0xFFFFFFFF, &ulNotificationValue, wait_ticks) == pdTRUE) {
                if (m_last_tx_status == Driver::DaliEventType::TxCompleted) {
                    m_waiting_for_tx_result = false;
                    return ESP_OK;
                } else if (m_last_tx_status == Driver::DaliEventType::CollisionDetected) {
                    ESP_LOGD(TAG, "Collision detected! Retry %d/%d", retries + 1, MAX_RETRIES);
                    m_driver.sendSystemFailureSignal();

                    // Back-off (IEC 62386-101)
                    // T_backoff = T_settle + (Priority * T_slot) + Random
                    uint32_t backoff_ms = (PRIORITY * 2) + (esp_random() % 4);
                    vTaskDelay(pdMS_TO_TICKS(backoff_ms));

                    retries++;
                } else {
                    m_waiting_for_tx_result = false;
                    m_tx_caller_task = nullptr;
                    return ESP_FAIL;
                }
            } else {
                m_waiting_for_tx_result = false;
                m_tx_caller_task = nullptr;
                ESP_LOGE(TAG, "TX Timeout waiting for confirm");
                return ESP_ERR_TIMEOUT;
            }
        }

        m_waiting_for_tx_result = false;
        m_tx_caller_task = nullptr;
        return ESP_FAIL; // Too many retries
    }

    std::optional<uint8_t> DaliAdapter::sendRawQuery(const uint32_t data, const uint8_t bits) {
        std::lock_guard<std::recursive_mutex> lock(m_transaction_mutex);

        xQueueReset(m_response_queue);
        m_expecting_response = true;
        m_driver.flushRxQueue();

        const esp_err_t sent_res = sendRaw(data, bits);
        if (sent_res != ESP_OK) {
            m_expecting_response = false;
            return std::nullopt;
        }

        uint8_t response_byte = 0;
        if (xQueueReceive(m_response_queue, &response_byte, pdMS_TO_TICKS(60)) == pdTRUE) {
            m_expecting_response = false;
            return response_byte;
        }

        m_expecting_response = false;
        return std::nullopt;
    }
 esp_err_t DaliAdapter::sendCommand(const DaliAddressType addr_type, const uint8_t addr, const OpCode command, const bool send_twice) {
        Frame frame;

        if (addr_type == DaliAddressType::Broadcast) frame = Factory::CommandBroadcast(command);
        else if (addr_type == DaliAddressType::Group) frame = Factory::CommandGroup(addr, command);
        else frame = Factory::Command(addr, command);

        esp_err_t res = sendRaw(frame.data, 16);
        if (send_twice) {
            vTaskDelay(pdMS_TO_TICKS(10));
            res = sendRaw(frame.data, 16);
        }
        return res;
    }

    esp_err_t DaliAdapter::sendCommand(const SpecialOpCode command, const uint8_t data, const bool send_twice) {
        auto [payload, bits] = Factory::Special(command, data);
        esp_err_t res = sendRaw(payload, 16);
        if (send_twice) {
            vTaskDelay(pdMS_TO_TICKS(10));
            res = sendRaw(payload, 16);
        }
        return res;
    }

    esp_err_t inline DaliAdapter::sendCommand(const DaliAddressType addr_type, const uint8_t addr, const DT8OpCode command, const bool send_twice) {
        return sendCommand(addr_type, addr, static_cast<OpCode>(command), send_twice);
    }

    std::optional<uint8_t> DaliAdapter::sendQuery(const DaliAddressType addr_type, const uint8_t addr, const OpCode command) {
        Frame frame;

        if (addr_type == DaliAddressType::Broadcast) frame = Factory::CommandBroadcast(command);
        else if (addr_type == DaliAddressType::Group) frame = Factory::CommandGroup(addr, command);
        else frame = Factory::Command(addr, command);

        return sendRawQuery(frame.data, 16);
    }

    std::optional<uint8_t> inline DaliAdapter::sendQuery(const DaliAddressType addr_type, const uint8_t addr, const DT8OpCode command) {
        return sendQuery(addr_type, addr, static_cast<OpCode>(command));
    }

    std::optional<uint8_t> DaliAdapter::sendQuery( const SpecialOpCode command, const uint8_t data) {
        auto [payload, bits] = Factory::Special(command, data);
        return sendRawQuery(payload, 16);
    }

    esp_err_t DaliAdapter::sendDACP(const DaliAddressType addr_type, const uint8_t addr, const uint8_t level) {
        Frame frame;

        if (addr_type == DaliAddressType::Broadcast) frame = Factory::DACPBroadcast(level);
        else if (addr_type == DaliAddressType::Group) frame = Factory::DACPGroup(addr, level);
        else frame = Factory::DACP(addr, level);

        return sendRaw(frame.data, 16);
    }

    std::optional<uint8_t> DaliAdapter::sendInputDeviceCommand(const uint8_t shortAddress, const uint8_t opcode, const std::optional<uint8_t> param) {
        // DALI-2 24-bit frame: AAAAAA1 (Short Addr) + INST + OPCODE
        const uint8_t addrByte = (shortAddress << 1) | 1;
        const uint8_t instByte = param.value_or(0x00);

        auto [data, bits] = Factory::InputDeviceCmd(addrByte, instByte, opcode);
        return sendRawQuery(data, 24);
    }

    [[noreturn]] void DaliAdapter::busWorkerTask(void* arg) {
        auto* self = static_cast<DaliAdapter*>(arg);
        auto queue = self->m_driver.getEventQueue();
        Driver::DaliMessage msg;

        while (true) {
            if (xQueueReceive(queue, &msg, portMAX_DELAY) == pdTRUE) {
                if (self->m_waiting_for_tx_result) {
                    if (msg.type == Driver::DaliEventType::TxCompleted ||
                        msg.type == Driver::DaliEventType::CollisionDetected ||
                        msg.type == Driver::DaliEventType::BusFailure) {
                            self->m_last_tx_status = msg.type;
                            if (self->m_tx_caller_task) {
                                xTaskNotify(self->m_tx_caller_task, 1, eSetBits);
                            }
                        }
                }
                if (msg.type == Driver::DaliEventType::FrameReceived && msg.is_backward && self->m_expecting_response) {
                    auto data = static_cast<uint8_t>(msg.data & 0xFF);
                    xQueueSend(self->m_response_queue, &data, 0);
                }

                if (self->m_sniffer_enabled && self->m_dali_event_queue) {
                    dali_frame_t frame = {};
                    frame.data = msg.data;
                    frame.length = msg.length;
                    frame.is_backward_frame = msg.is_backward;

                    if (msg.type == Driver::DaliEventType::TxCompleted) {
                        frame.is_backward_frame = false;
                    } else if (msg.type == Driver::DaliEventType::CollisionDetected) {
                        ESP_LOGD(TAG, "Bus Collision Detected");
                        continue;
                    } else if (msg.type == Driver::DaliEventType::FrameError) {
                        continue;
                    }

                    xQueueSend(self->m_dali_event_queue, &frame, 0);
                }
            }
        }
    }

    void DaliAdapter::setDtr0(const uint8_t val) {
        sendRaw(Factory::Special(SpecialOpCode::Dtr0, val).data, 16);
    }
    void DaliAdapter::setDtr1(const uint8_t val) {
        sendRaw(Factory::Special(SpecialOpCode::Dtr1, val).data, 16);
    }

    uint8_t DaliAdapter::initializeBus(const bool provision_all) {
        ESP_LOGI(TAG, "Starting Commissioning (Control Gear)...");

        sendRaw(Factory::Special(SpecialOpCode::Terminate, 0).data, 16);
        sendRaw(Factory::Special(SpecialOpCode::Terminate, 0).data, 16);

        // Initialise
        const uint8_t init_arg = provision_all ? 0xFF : 0x00; // 0xFF = Unaddressed, 0x00 = All
        sendRaw(Factory::Special(SpecialOpCode::Initialise, init_arg).data, 16);
        sendRaw(Factory::Special(SpecialOpCode::Initialise, init_arg).data, 16); // Send twice

        // Randomise
        sendRaw(Factory::Special(SpecialOpCode::Randomise, 0).data, 16);
        sendRaw(Factory::Special(SpecialOpCode::Randomise, 0).data, 16); // Send twice
        vTaskDelay(pdMS_TO_TICKS(100));

        // Binary Search loop
        uint8_t devices_found = 0;
        while (true) {
            uint32_t longAddr = findAddressBinarySearch(false);
            if (longAddr > 0xFFFFFF) break; // No more devices
            uint8_t prog_byte = (devices_found << 1) | 1;
            if (devices_found >= 64) {
                ESP_LOGW(TAG, "More than 64 devices found. Skipping assignment.");
                sendRaw(Factory::Special(SpecialOpCode::Withdraw, 0).data, 16);
                continue;
            }

            sendRaw(Factory::Special(SpecialOpCode::ProgramShortAddr, prog_byte).data, 16);
            ESP_LOGI(TAG, "Assigned Short Addr %d to Long Addr 0x%06lX", devices_found, longAddr);

            devices_found++;
            sendRaw(Factory::Special(SpecialOpCode::Withdraw, 0).data, 16);
        }

        sendRaw(Factory::Special(SpecialOpCode::Terminate, 0).data, 16);
        return devices_found;
    }

    uint32_t DaliAdapter::findAddressBinarySearch(const bool input_devices) {
        uint32_t low = 0;
        uint32_t high = 0xFFFFFF;
        uint32_t searchAddr = 0xFFFFFF;

        auto sendSearchAddr = [&](const uint32_t addr) {
            if (input_devices) {
                // Input Device Ops: 0x08 (H), 0x09 (M), 0x0A (L)
                sendRaw(Factory::InputDeviceCmd(0xFF, (addr >> 16) & 0xFF, 0x08).data, 24);
                sendRaw(Factory::InputDeviceCmd(0xFF, (addr >> 8) & 0xFF,  0x09).data, 24);
                sendRaw(Factory::InputDeviceCmd(0xFF, addr & 0xFF,         0x0A).data, 24);
            } else {
                sendRaw(Factory::Special(SpecialOpCode::SearchAddrH, (addr >> 16) & 0xFF).data, 16);
                sendRaw(Factory::Special(SpecialOpCode::SearchAddrM, (addr >> 8) & 0xFF).data, 16);
                sendRaw(Factory::Special(SpecialOpCode::SearchAddrL, (addr) & 0xFF).data, 16);
            }
        };

        auto sendCompare = [&]() -> bool {
            if (input_devices) {
                auto res = sendRawQuery(Factory::InputDeviceCmd(0xFF, 0xFF, 0x02).data, 24);
                return res.has_value();
            } else {
                auto res = sendRawQuery(Factory::Special(SpecialOpCode::Compare, 0).data, 16);
                return res.has_value();
            }
        };

        sendSearchAddr(0xFFFFFF);
        if (!sendCompare()) return 0xFFFFFFFF; // No devices

        while ((high - low) > 0) {
            searchAddr = low + (high - low) / 2;
            sendSearchAddr(searchAddr);

            if (sendCompare()) {
                high = searchAddr;
            } else {
                low = searchAddr + 1;
            }
        }

        // Verify
        searchAddr = low;
        sendSearchAddr(searchAddr);
        if (sendCompare()) return searchAddr;

        return 0xFFFFFFFF;
    }

    uint8_t DaliAdapter::initialize24BitDevicesBus() {
        ESP_LOGI(TAG, "Starting Commissioning (Input Devices)...");

        // Terminate
        sendRaw(Factory::InputDeviceCmd(0xFF, 0xFF, 0x06).data, 24);

        // Initialise (0x00 = All, 0xFF = Unaddressed)
        sendRaw(Factory::InputDeviceCmd(0xFF, 0xFF, 0x00).data, 24);
        sendRaw(Factory::InputDeviceCmd(0xFF, 0xFF, 0x00).data, 24);

        // Randomise
        sendRaw(Factory::InputDeviceCmd(0xFF, 0xFF, 0x01).data, 24);
        sendRaw(Factory::InputDeviceCmd(0xFF, 0xFF, 0x01).data, 24);
        vTaskDelay(pdMS_TO_TICKS(100));

        uint8_t devices_found = 0;

        while (true) {
            uint32_t longAddr = findAddressBinarySearch(true);
            if (longAddr > 0xFFFFFF) break;

            if (devices_found >= 64) {
                // Withdraw (0x03)
                sendRaw(Factory::InputDeviceCmd(0xFF, 0xFF, 0x03).data, 24);
                continue;
            }
            uint8_t progData = (devices_found << 1) | 1;
            sendRaw(Factory::InputDeviceCmd(0xFF, progData, 0x07).data, 24);

            ESP_LOGI(TAG, "Found Input Device at 0x%06lX -> SA %d", longAddr, devices_found);
            devices_found++;

            // Withdraw (0x03)
            sendRaw(Factory::InputDeviceCmd(0xFF, 0xFF, 0x03).data, 24);
        }

        // Terminate
        sendRaw(Factory::InputDeviceCmd(0xFF, 0xFF, 0x06).data, 24);
        return devices_found;
    }

    std::optional<DaliLongAddress_t> DaliAdapter::getLongAddress(const uint8_t shortAddress) {
        const auto h = sendQuery(DaliAddressType::Short, shortAddress, OpCode::QueryRandomAddrH);
        if (!h) return std::nullopt;
        const auto m = sendQuery(DaliAddressType::Short, shortAddress, OpCode::QueryRandomAddrM);
        if (!m) return std::nullopt;
        const auto l = sendQuery(DaliAddressType::Short, shortAddress, OpCode::QueryRandomAddrL);
        if (!l) return std::nullopt;

        return (static_cast<uint32_t>(*h) << 16) | (static_cast<uint32_t>(*m) << 8) | (*l);
    }

    std::optional<uint8_t> DaliAdapter::getDeviceStatus(const uint8_t shortAddress) {
        return sendQuery(DaliAddressType::Short, shortAddress, OpCode::QueryStatus);
    }

    std::optional<uint8_t> DaliAdapter::getDeviceType(const uint8_t shortAddress) {
        return sendQuery(DaliAddressType::Short, shortAddress, OpCode::QueryDeviceType);
    }

    std::optional<std::bitset<16>> DaliAdapter::getDeviceGroups(const uint8_t shortAddress) {
        const auto g0_7 = sendQuery(DaliAddressType::Short, shortAddress, OpCode::QueryGroups0_7);
        const auto g8_15 = sendQuery(DaliAddressType::Short, shortAddress, OpCode::QueryGroups8_15);

        if (g0_7 && g8_15) {
            uint16_t mask = (*g8_15 << 8) | *g0_7;
            return std::bitset<16>(mask);
        }
        return std::nullopt;
    }

    std::optional<std::string> DaliAdapter::getGTIN(const uint8_t shortAddress) {
        std::string gtin;
        for(uint8_t i=0; i<6; i++) {
            auto byte = readMemoryLocation(shortAddress, 0, 3 + i);
            if(byte) {
                char hex[3];
                snprintf(hex, sizeof(hex), "%02X", *byte);
                gtin += hex;
            } else {
                return std::nullopt;
            }
        }
        return gtin;
    }

    std::optional<uint8_t> DaliAdapter::readMemoryLocation(const uint8_t shortAddress, const uint8_t bank, const uint8_t offset) {
        setDtr1(bank);
        setDtr0(offset);
        return sendQuery(DaliAddressType::Short, shortAddress, OpCode::ReadMemoryLocation);
    }

    esp_err_t DaliAdapter::assignToGroup(const uint8_t shortAddress, const uint8_t group) {
        return sendCommand(DaliAddressType::Short, shortAddress, static_cast<OpCode>(0x60 + group), true);
    }

    esp_err_t DaliAdapter::removeFromGroup(const uint8_t shortAddress, const uint8_t group) {
        return sendCommand(DaliAddressType::Short, shortAddress, static_cast<OpCode>(0x70 + group), true);
    }

    std::optional<uint8_t> DaliAdapter::getDT8Features(const uint8_t shortAddress) {
        sendRaw(Factory::Special(SpecialOpCode::EnableDeviceTypeX, 8).data, 16);
        return sendQuery(DaliAddressType::Short, shortAddress, DT8OpCode::QueryColourType);
    }

    std::optional<uint8_t> DaliAdapter::queryDT8Value(const uint8_t shortAddress, const uint8_t dtr0_selector) {
        setDtr0(dtr0_selector);
        sendRaw(Factory::Special(SpecialOpCode::EnableDeviceTypeX, 8).data, 16);
        return sendQuery(DaliAddressType::Short, shortAddress, DT8OpCode::QueryColourValue);
    }

    std::optional<uint16_t> DaliAdapter::getDT8ColorTemp(const uint8_t shortAddress) {
        const auto msb = queryDT8Value(shortAddress, 0); // High byte
        if(!msb) return std::nullopt;
        const auto lsb = queryDT8Value(shortAddress, 1); // Low byte
        if(!lsb) return std::nullopt;
        return (static_cast<uint16_t>(*msb) << 8) | *lsb;
    }

    std::optional<DaliRGB> DaliAdapter::getDT8RGB(const uint8_t shortAddress) {
        const auto r = queryDT8Value(shortAddress, 2);
        const auto g = queryDT8Value(shortAddress, 3);
        const auto b = queryDT8Value(shortAddress, 4);
        if(r && g && b) return DaliRGB{*r, *g, *b};
        return std::nullopt;
    }

    esp_err_t DaliAdapter::sendDT8Cmd(const uint8_t shortAddr, const DT8OpCode cmd) {
        // Frame: 0xC1 <Type> (Special command).
        sendRaw(Factory::Special(SpecialOpCode::EnableDeviceTypeX, 8).data, 16);
        return sendCommand(DaliAddressType::Short, shortAddr, cmd, false);
    }

    esp_err_t DaliAdapter::setDT8ColorTemp(const DaliAddressType addr_type, const uint8_t addr, const uint16_t mireds) {
        // Set DTR1 (High Byte)
        setDtr1((mireds >> 8) & 0xFF);
        // Set DTR0 (Low Byte)
        setDtr0(mireds & 0xFF);

        // Enable DT8
        sendRaw(Factory::Special(SpecialOpCode::EnableDeviceTypeX, 8).data, 16);

        // Send SET COLOUR TEMPERATURE TC (0xE7)
        sendCommand(addr_type, addr, DT8OpCode::SetTempTc, false);

        // Activate
        sendRaw(Factory::Special(SpecialOpCode::EnableDeviceTypeX, 8).data, 16);
        sendCommand(addr_type, addr, DT8OpCode::Activate, false);

        return ESP_OK;
    }

    esp_err_t DaliAdapter::setDT8RGB(const DaliAddressType addr_type, const uint8_t addr, const uint8_t r, const uint8_t g, const uint8_t b) {
        // Sequence: DTR1=Mask, DTR0=Val -> Set Temporary RGB Dimlevel (0xEB)
        auto sendColorComp = [&](const uint8_t mask, const uint8_t val) {
            setDtr1(mask);
            setDtr0(val);
            sendRaw(Factory::Special(SpecialOpCode::EnableDeviceTypeX, 8).data, 16);
            sendCommand(addr_type, addr, DT8OpCode::SetTempRGB, false);
        };

        sendColorComp(1, r);
        sendColorComp(2, g);
        sendColorComp(4, b);

        // Activate
        sendRaw(Factory::Special(SpecialOpCode::EnableDeviceTypeX, 8).data, 16);
        sendCommand(addr_type, addr, DT8OpCode::Activate, false);

        return ESP_OK;
    }
} // daliMQTT