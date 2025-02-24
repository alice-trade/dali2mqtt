//
// Created by danil on 23.02.2025.
//

#include <cstdint>

#include "dali_commander.hpp"
#include "dali_commands.h" // DALI command definitions

static const char *TAG = "DaliProtocol";

namespace DaliProtocol {

    int init() {
        // Initialization, if needed, for the protocol layer.  Could be empty.
        return 1;
    }

    int sendCommand(uint8_t address, bool is_group, uint8_t command, int* result) {
        dali_addressType_t address_type = is_group ? DALI_ADDRESS_TYPE_GROUP : DALI_ADDRESS_TYPE_SHORT;
        return dali_transaction(address_type, address, true, command, false, DALI_TX_TIMEOUT_DEFAULT_MS, result);
    }

    int sendBroadcast(uint8_t command) {
        return dali_transaction(DALI_ADDRESS_TYPE_BROADCAST, 0, true, command, false, DALI_TX_TIMEOUT_DEFAULT_MS, nullptr);
    }

    int setBrightness(uint8_t address, bool is_group, uint8_t brightness) {
        // DALI uses "Direct Arc Power Control" (DAPC). Brightness 0 is off, 1-254 is the level, 255 often means "no change".
        if (brightness == 0) {
            return sendCommand(address, is_group, DALI_COMMAND_OFF, nullptr);
        }
        dali_addressType_t address_type = is_group ? DALI_ADDRESS_TYPE_GROUP : DALI_ADDRESS_TYPE_SHORT;
        return dali_transaction(address_type, address, false, brightness, false, DALI_TX_TIMEOUT_DEFAULT_MS, nullptr);
    }
    int turnOn(uint8_t address, bool is_group) {
        return sendCommand(address, is_group, DALI_COMMAND_RECALL_MAX_LEVEL, nullptr);  // Or use a dedicated "ON" command if your devices require it.
    }

    int turnOff(uint8_t address, bool is_group) {
        return sendCommand(address, is_group, DALI_COMMAND_OFF, nullptr);
    }

    int queryBrightness(uint8_t address, bool is_group, int* brightness) {
        dali_addressType_t address_type = is_group ? DALI_ADDRESS_TYPE_GROUP : DALI_ADDRESS_TYPE_SHORT;

        return dali_transaction(address_type, address, true, DALI_COMMAND_QUERY_ACTUAL_LEVEL, false, DALI_TX_TIMEOUT_DEFAULT_MS, brightness);
    }

} // namespace DaliProtocol