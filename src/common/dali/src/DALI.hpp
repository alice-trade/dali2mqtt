//
// Created by danil on 23.02.2025.
//

#ifndef DALI_INTERFACE_HPP
#define DALI_INTERFACE_HPP
#include <cstdint>
#include "dali.h" // Подключаем низкоуровневый DALI

namespace DaliProtocol {

// Forward declarations (чтобы избежать циклических зависимостей)
struct DaliCommand;
struct DaliResponse;

/**
 * @brief Initializes the DALI protocol layer.
 *
 * @return esp_err_t ESP_OK on success, error code otherwise.
 */
int init();

/**
 * @brief Sends a DALI command to a specific address.
 *
 * @param address The DALI address (short or group).
 * @param is_group True if it's a group address, false for a short address.
 * @param command The DALI command (from dali_commands.h).
 * @param result Pointer to store the DALI response (optional, can be nullptr).
 * @return esp_err_t ESP_OK on success, error code otherwise.
 */
int sendCommand(uint8_t address, bool is_group, uint8_t command, int* result = nullptr);

/**
 * @brief Sends a DALI broadcast command.
 *
 * @param command The DALI command.
 * @return esp_err_t ESP_OK on success, error code otherwise.
 */
int sendBroadcast(uint8_t command);

/**
 * @brief Sets the brightness level of a DALI device.
 *
 * @param address The DALI address.
 * @param is_group True if it's a group address, false for a short address.
 * @param brightness The brightness level (0-254).  255 is a special value, often for "no change".
 * @return esp_err_t ESP_OK on success, error code otherwise.
 */
int setBrightness(uint8_t address, bool is_group, uint8_t brightness);

/**
 * @brief Turns on a DALI device.
 *
 * @param address The DALI address.
 * @param is_group True if it's a group address, false for a short address.
 * @return esp_err_t ESP_OK on success, error code otherwise.
 */
int turnOn(uint8_t address, bool is_group);

/**
 * @brief Turns off a DALI device.
 *
 * @param address The DALI address.
 * @param is_group True if it's a group address, false for a short address.
 * @return esp_err_t ESP_OK on success, error code otherwise.
 */
int turnOff(uint8_t address, bool is_group);

/**
 * @brief Queries the actual brightness level of a DALI device.
 *
 * @param address The DALI address.
 * @param is_group True if it's a group address.
 * @param brightness Pointer to store the result.
 * @return esp_err_t ESP_OK on success, error code otherwise.
 */
int queryBrightness(uint8_t address, bool is_group, int* brightness);

} // namespace DaliProtocol

#endif //DALI_INTERFACE_HPP
