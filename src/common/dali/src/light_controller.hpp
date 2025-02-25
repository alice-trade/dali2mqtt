//
// Created by danil on 25.02.2025.
//

#ifndef LIGHT_CONTROLLER_HPP
#define LIGHT_CONTROLLER_HPP
#include <cstdint>
#include <string>
namespace LightingController {

    /**
     * @brief Initializes the lighting controller module.
     *
     * @return true if initialization was successful, false otherwise.
     */
    bool init();

    /**
     * @brief Sets the brightness level of a DALI device.
     *
     * @param address The DALI address.
     * @param is_group True if it's a group address, false for a short address.
     * @param brightness The brightness level (0-254).
     * @return true if command was sent successfully, false otherwise.
     */
    bool setBrightness(uint8_t address, bool is_group, uint8_t brightness);

    /**
     * @brief Turns on a DALI device.
     *
     * @param address The DALI address.
     * @param is_group True if it's a group address, false for a short address.
     * @return true if command was sent successfully, false otherwise.
     */
    bool turnOn(uint8_t address, bool is_group);

    /**
     * @brief Turns off a DALI device.
     *
     * @param address The DALI address.
     * @param is_group True if it's a group address, false for a short address.
     * @return true if command was sent successfully, false otherwise.
     */
    bool turnOff(uint8_t address, bool is_group);

    /**
     * @brief Queries the brightness level of a DALI device.
     *
     * @param address The DALI address.
     * @param is_group True if it's a group address.
     * @param brightness Pointer to store the brightness value.
     * @return true if query was successful, false otherwise.
     */
    bool queryBrightness(uint8_t address, bool is_group, int* brightness);

} // namespace LightingController
#endif //LIGHT_CONTROLLER_HPP
