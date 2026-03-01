// Copyright (c) 2026 Alice-Trade Inc.
// SPDX-License-Identifier: GPL-3.0-or-later

#include "dali/DaliAdapter.hxx"

namespace daliMQTT {

    static constexpr char TAG[] = "DaliAdapter";
    using namespace Commands;

    struct AdapterLock {
        const DaliAdapter* adapter;
        explicit AdapterLock(const DaliAdapter* a) : adapter(a) { if (adapter) adapter->lockBus(); }
        ~AdapterLock() { if (adapter) adapter->unlockBus(); }
    };

    DaliAdapter::DaliAdapter(const uint8_t bus_id, QueueHandle_t shared_central_queue)
            : m_bus_id(bus_id), m_dali_event_queue(shared_central_queue) {}

    DaliAdapter::~DaliAdapter() {
        if (m_worker_task_handle) vTaskDelete(m_worker_task_handle);
        if (m_bus_mutex) vSemaphoreDelete(m_bus_mutex);
        if (m_event_queue) vQueueDelete(m_event_queue);
    }

    esp_err_t DaliAdapter::init(gpio_num_t rx_pin, gpio_num_t tx_pin) {
        if (m_initialized) return ESP_OK;

        m_bus_mutex = xSemaphoreCreateRecursiveMutex();
        m_event_queue = xQueueCreate(64, sizeof(AdapterEvent));

        Driver::DaliDriverConfig drv_cfg = {
            .rx_pin = rx_pin,
            .tx_pin = tx_pin,
        };

        m_driver.setEventCallback([](const Driver::DaliMessage& msg, void* ctx) {
            static_cast<DaliAdapter*>(ctx)->onDriverEvent(msg);
        }, this);

        esp_err_t init_result = m_driver.init(drv_cfg);
        if (init_result != ESP_OK) {
            ESP_LOGE(TAG, "Failed to init RMT Driver: %s", esp_err_to_name(init_result));
            return init_result;
        }

        m_dali_event_queue = xQueueCreate(32, sizeof(dali_frame_t));
        xTaskCreate(busWorkerTask, "dali_bus_worker", 4096, this, 5, &m_worker_task_handle);

        m_initialized = true;
        ESP_LOGI(TAG, "Adapter initialized with DALI Driver (RMT).");
        return ESP_OK;
    }

    void DaliAdapter::onDriverEvent(const Driver::DaliMessage& msg) const {
        AdapterEvent ev;
        ev.type = AdapterEvent::Type::DRIVER_EVENT;
        ev.msg = msg;
        xQueueSend(m_event_queue, &ev, 0);
    }

    esp_err_t DaliAdapter::startSniffer() {
        m_sniffer_enabled = true;
        return ESP_OK;
    }

    esp_err_t DaliAdapter::stopSniffer() {
        m_sniffer_enabled = false;
        return ESP_OK;
    }

    esp_err_t DaliAdapter::sendRaw(const uint32_t data, const uint8_t bits) const {
        AdapterLock lock(this);
        AdapterEvent ev;
        ev.type = AdapterEvent::Type::CMD;
        ev.cmd = { data, bits, false, false, xTaskGetCurrentTaskHandle() };
        xTaskNotifyStateClearIndexed(nullptr, NOTIFY_IDX);
        xQueueSend(m_event_queue, &ev, portMAX_DELAY);

        uint32_t notify_val = 0;
        xTaskNotifyWaitIndexed(NOTIFY_IDX, 0, 0xFFFFFFFF, &notify_val, portMAX_DELAY);
        return static_cast<esp_err_t>(notify_val >> 16);
    }

    std::optional<uint8_t> DaliAdapter::sendRawQuery(const uint32_t data, const uint8_t bits) const {
        AdapterLock lock(this);
        AdapterEvent ev;
        ev.type = AdapterEvent::Type::CMD;
        ev.cmd = { data, bits, true, false, xTaskGetCurrentTaskHandle() };
        xTaskNotifyStateClearIndexed(nullptr, NOTIFY_IDX);
        xQueueSend(m_event_queue, &ev, portMAX_DELAY);

        uint32_t notify_val = 0;
        xTaskNotifyWaitIndexed(NOTIFY_IDX, 0, 0xFFFFFFFF, &notify_val, portMAX_DELAY);
        if (esp_err_t res = static_cast<esp_err_t>(notify_val >> 16); res == ESP_OK) {
            return static_cast<uint8_t>(notify_val & 0xFF);
        }
        return std::nullopt;
    }

    esp_err_t DaliAdapter::sendCommand(const DaliAddressType addr_type, const uint8_t addr, const OpCode command, const bool send_twice) const {
        AdapterLock lock(this);
        Frame frame;
        if (addr_type == DaliAddressType::Broadcast) frame = Factory::CommandBroadcast(command);
        else if (addr_type == DaliAddressType::Group) frame = Factory::CommandGroup(addr, command);
        else frame = Factory::Command(addr, command);

        AdapterEvent ev;
        ev.type = AdapterEvent::Type::CMD;
        ev.cmd = { frame.data, 16, false, send_twice, xTaskGetCurrentTaskHandle() };
        xTaskNotifyStateClearIndexed(nullptr, NOTIFY_IDX);
        xQueueSend(m_event_queue, &ev, portMAX_DELAY);

        uint32_t notify_val = 0;
        xTaskNotifyWaitIndexed(NOTIFY_IDX, 0, 0xFFFFFFFF, &notify_val, portMAX_DELAY);
        return static_cast<esp_err_t>(notify_val >> 16);
    }

    esp_err_t DaliAdapter::sendCommand(const SpecialOpCode command, const uint8_t data, const bool send_twice) const {
        AdapterLock lock(this);
        auto [payload, bits] = Factory::Special(command, data);

        AdapterEvent ev;
        ev.type = AdapterEvent::Type::CMD;
        ev.cmd = { payload, 16, false, send_twice, xTaskGetCurrentTaskHandle() };
        xTaskNotifyStateClearIndexed(nullptr, NOTIFY_IDX);
        xQueueSend(m_event_queue, &ev, portMAX_DELAY);

        uint32_t notify_val = 0;
        xTaskNotifyWaitIndexed(NOTIFY_IDX, 0, 0xFFFFFFFF, &notify_val, portMAX_DELAY);
        return static_cast<esp_err_t>(notify_val >> 16);
    }

    std::optional<uint8_t> DaliAdapter::sendQuery(const DaliAddressType addr_type, const uint8_t addr, const OpCode command) const {
        Frame frame;

        if (addr_type == DaliAddressType::Broadcast) frame = Factory::CommandBroadcast(command);
        else if (addr_type == DaliAddressType::Group) frame = Factory::CommandGroup(addr, command);
        else frame = Factory::Command(addr, command);

        return sendRawQuery(frame.data, 16);
    }

    std::optional<uint8_t> DaliAdapter::sendQuery( const SpecialOpCode command, const uint8_t data) const {
        auto [payload, bits] = Factory::Special(command, data);
        return sendRawQuery(payload, 16);
    }

    esp_err_t DaliAdapter::sendDACP(const DaliAddressType addr_type, const uint8_t addr, const uint8_t level) const {
        Frame frame;

        if (addr_type == DaliAddressType::Broadcast) frame = Factory::DACPBroadcast(level);
        else if (addr_type == DaliAddressType::Group) frame = Factory::DACPGroup(addr, level);
        else frame = Factory::DACP(addr, level);

        return sendRaw(frame.data, 16);
    }

    std::optional<uint8_t> DaliAdapter::sendInputDeviceCommand(const uint8_t shortAddress, const uint8_t opcode, const std::optional<uint8_t> param) const {
        // DALI-2 24-bit frame: AAAAAA1 (Short Addr) + INST + OPCODE
        const uint8_t addrByte = (shortAddress << 1) | 1;
        const uint8_t instByte = param.value_or(0x00);

        auto [data, bits] = Factory::InputDeviceCmd(addrByte, instByte, opcode);
        return sendRawQuery(data, 24);
    }

    [[noreturn]] void DaliAdapter::busWorkerTask(void* arg) {
        auto* self = static_cast<DaliAdapter*>(arg);
        enum class State { IDLE, TX_WAIT, WAIT_RX };
        auto state = State::IDLE;
        AdapterEvent::CmdData active_cmd{};
        uint8_t retries = 0;
        int64_t rx_timeout = 0;
        int64_t tx_timeout = 0;

        auto finishCmd = [&](const esp_err_t res, const uint8_t resp) {
            if (active_cmd.caller) {
                const uint32_t val = (static_cast<uint32_t>(res) << 16) | resp;
                xTaskNotifyIndexed(active_cmd.caller, NOTIFY_IDX, val, eSetValueWithOverwrite);
            }
            state = State::IDLE;
            if (!self->m_cmd_buffer.empty()) {
                active_cmd = self->m_cmd_buffer.front();
                self->m_cmd_buffer.pop();
                retries = 0;
                static_cast<void>(self->m_driver.sendAsync(active_cmd.data, active_cmd.bits));
                state = State::TX_WAIT;
                tx_timeout = esp_timer_get_time();
            }
        };

        while (true) {
            AdapterEvent ev;
            const TickType_t wait_ticks = (state == State::WAIT_RX) ? pdMS_TO_TICKS(10) : portMAX_DELAY;

            if (xQueueReceive(self->m_event_queue, &ev, wait_ticks) == pdTRUE) {
                if (ev.type == AdapterEvent::Type::CMD) {
                    if (state == State::IDLE) {
                        active_cmd = ev.cmd;
                        retries = 0;
                        static_cast<void>(self->m_driver.sendAsync(active_cmd.data, active_cmd.bits));
                        state = State::TX_WAIT;
                        tx_timeout = esp_timer_get_time();
                    } else {
                        self->m_cmd_buffer.push(ev.cmd);
                    }
                } else if (ev.type == AdapterEvent::Type::DRIVER_EVENT) {
                    const auto& msg = ev.msg;
                    if (state == State::IDLE) {
                        if (msg.type == Driver::DaliEventType::FrameReceived && self->m_sniffer_enabled && self->m_dali_event_queue) {
                            dali_frame_t frame{msg.data, msg.length, msg.is_backward, self->m_bus_id};
                            xQueueSend(self->m_dali_event_queue, &frame, 0);
                        }
                    } else if (state == State::TX_WAIT) {
                        if (msg.type == Driver::DaliEventType::TxCompleted) {
                            if (active_cmd.send_twice) {
                                active_cmd.send_twice = false;
                                vTaskDelay(pdMS_TO_TICKS(10));
                                static_cast<void>(self->m_driver.sendAsync(active_cmd.data, active_cmd.bits));
                            } else if (active_cmd.is_query) {
                                state = State::WAIT_RX;
                                rx_timeout = esp_timer_get_time();
                            } else {
                                finishCmd(ESP_OK, 0);
                            }
                        } else if (msg.type == Driver::DaliEventType::CollisionDetected) {
                            retries++;
                            if (retries <= 3) {
                                self->m_driver.sendSystemFailureSignal();
                                vTaskDelay(pdMS_TO_TICKS(4 + (esp_random() % 4)));
                                static_cast<void>(self->m_driver.sendAsync(active_cmd.data, active_cmd.bits));
                                tx_timeout = esp_timer_get_time();
                            } else {
                                finishCmd(ESP_FAIL, 0);
                            }
                        } else if (msg.type == Driver::DaliEventType::BusFailure) {
                            finishCmd(ESP_FAIL, 0);
                        }
                    } else if (state == State::WAIT_RX) {
                        if (msg.type == Driver::DaliEventType::FrameReceived && msg.is_backward) {
                            finishCmd(ESP_OK, msg.data & 0xFF);
                        }
                    }
                }
            }
            int64_t now = esp_timer_get_time();
            if (state == State::WAIT_RX && (now - rx_timeout) > 15000) finishCmd(ESP_ERR_TIMEOUT, 0);
            if (state == State::TX_WAIT && (now - tx_timeout) > 100000) {
                ESP_LOGD(TAG, "TX Timeout! Resetting state.");
                finishCmd(ESP_FAIL, 0);
            }
        }
    }

    uint8_t DaliAdapter::initializeBus(const bool provision_all) {
        using enum daliMQTT::Commands::SpecialOpCode;
        ESP_LOGI(TAG, "Starting Commissioning (Control Gear)...");
        AdapterLock lock(this);

        sendRaw(Factory::Special(Terminate, 0).data, 16);
        sendRaw(Factory::Special(Terminate, 0).data, 16);

        // Initialise
        const uint8_t init_arg = provision_all ? 0xFF : 0x00; // 0xFF = Unaddressed, 0x00 = All
        sendRaw(Factory::Special(Initialise, init_arg).data, 16);
        sendRaw(Factory::Special(Initialise, init_arg).data, 16); // Send twice

        // Randomise
        sendRaw(Factory::Special(Randomise, 0).data, 16);
        sendRaw(Factory::Special(Randomise, 0).data, 16); // Send twice
        vTaskDelay(pdMS_TO_TICKS(100));

        // Binary Search loop
        uint8_t devices_found = 0;
        while (true) {
            uint32_t longAddr = findAddressBinarySearch(false);
            if (longAddr == InvalidLongAddr) break; // No more devices
            uint8_t prog_byte = (devices_found << 1) | 1;
            if (devices_found >= 64) {
                ESP_LOGW(TAG, "More than 64 devices found. Skipping assignment.");
                sendRaw(Factory::Special(Withdraw, 0).data, 16);
                break;
            }

            sendRaw(Factory::Special(ProgramShortAddr, prog_byte).data, 16);
            ESP_LOGI(TAG, "Assigned Short Addr %d to Long Addr 0x%06lX", devices_found, longAddr);

            devices_found++;
            sendRaw(Factory::Special(Withdraw, 0).data, 16);
        }

        sendRaw(Factory::Special(Terminate, 0).data, 16);
        return devices_found;
    }

    uint32_t DaliAdapter::findAddressBinarySearch(const bool input_devices) const {
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
        if (!sendCompare()) return InvalidLongAddr; // No devices

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

        return InvalidLongAddr;
    }

    uint8_t DaliAdapter::initialize24BitDevicesBus() {
        ESP_LOGI(TAG, "Starting Commissioning (Input Devices)...");
        AdapterLock lock(this);
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
            if (longAddr == InvalidLongAddr) break;
            if (devices_found >= 64) {
                // Withdraw (0x03)
                sendRaw(Factory::InputDeviceCmd(0xFF, 0xFF, 0x03).data, 24);
                break;
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
        AdapterLock lock(this);
        const auto h = sendQuery(DaliAddressType::Short, shortAddress, OpCode::QueryRandomAddrH);
        if (!h) return std::nullopt;
        const auto m = sendQuery(DaliAddressType::Short, shortAddress, OpCode::QueryRandomAddrM);
        if (!m) return std::nullopt;
        const auto l = sendQuery(DaliAddressType::Short, shortAddress, OpCode::QueryRandomAddrL);
        if (!l) return std::nullopt;

        return (static_cast<uint32_t>(*h) << 16) | (static_cast<uint32_t>(*m) << 8) | (*l);
    }

    std::optional<std::bitset<16>> DaliAdapter::getDeviceGroups(const uint8_t shortAddress) {
        AdapterLock lock(this);
        const auto g0_7 = sendQuery(DaliAddressType::Short, shortAddress, OpCode::QueryGroups0_7);
        const auto g8_15 = sendQuery(DaliAddressType::Short, shortAddress, OpCode::QueryGroups8_15);

        if (g0_7 && g8_15) {
            uint16_t mask = (*g8_15 << 8) | *g0_7;
            return std::bitset<16>(mask);
        }
        return std::nullopt;
    }

    std::optional<etl::string<16>> DaliAdapter::getGTIN(const uint8_t shortAddress) {
        AdapterLock lock(this);
        etl::string<16> gtin;
        for(uint8_t i=0; i<6; i++) {
            auto byte = readMemoryLocation(shortAddress, 0, 3 + i);
            if(byte) {
                char hex[3];
                snprintf(hex, sizeof(hex), "%02X", *byte);
                gtin.append(hex);
            } else {
                return std::nullopt;
            }
        }
        return gtin;
    }

    std::optional<uint8_t> DaliAdapter::readMemoryLocation(const uint8_t shortAddress, const uint8_t bank, const uint8_t offset) {
        AdapterLock lock(this);
        setDtr1(bank);
        setDtr0(offset);
        return sendQuery(DaliAddressType::Short, shortAddress, OpCode::ReadMemoryLocation);
    }

    std::optional<uint8_t> DaliAdapter::getDT8Features(const uint8_t shortAddress) {
        AdapterLock lock(this);
        sendRaw(Factory::Special(SpecialOpCode::EnableDeviceTypeX, 8).data, 16);
        return sendQuery(DaliAddressType::Short, shortAddress, DT8OpCode::QueryColourType);
    }

    std::optional<uint8_t> DaliAdapter::queryDT8Value(const uint8_t shortAddress, const uint8_t dtr0_selector) {
        AdapterLock lock(this);
        setDtr0(dtr0_selector);
        sendRaw(Factory::Special(SpecialOpCode::EnableDeviceTypeX, 8).data, 16);
        return sendQuery(DaliAddressType::Short, shortAddress, DT8OpCode::QueryColourValue);
    }

    std::optional<uint16_t> DaliAdapter::getDT8ColorTemp(const uint8_t shortAddress) {
        AdapterLock lock(this);
        const auto msb = queryDT8Value(shortAddress, 0); // High byte
        if(!msb) return std::nullopt;
        const auto lsb = queryDT8Value(shortAddress, 1); // Low byte
        if(!lsb) return std::nullopt;
        return (static_cast<uint16_t>(*msb) << 8) | *lsb;
    }

    std::optional<DaliRGB> DaliAdapter::getDT8RGB(const uint8_t shortAddress) {
        AdapterLock lock(this);
        const auto r = queryDT8Value(shortAddress, 2);
        const auto g = queryDT8Value(shortAddress, 3);
        const auto b = queryDT8Value(shortAddress, 4);
        if(r && g && b) return DaliRGB{*r, *g, *b};
        return std::nullopt;
    }

    esp_err_t DaliAdapter::sendDT8Cmd(const uint8_t shortAddr, const DT8OpCode cmd) {
        AdapterLock lock(this);
        // Frame: 0xC1 <Type> (Special command).
        sendRaw(Factory::Special(SpecialOpCode::EnableDeviceTypeX, 8).data, 16);
        return sendCommand(DaliAddressType::Short, shortAddr, cmd, false);
    }

    esp_err_t DaliAdapter::setDT8ColorTemp(const DaliAddressType addr_type, const uint8_t addr, const uint16_t mireds) {
        AdapterLock lock(this);
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
        AdapterLock lock(this);
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