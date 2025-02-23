//
// Created by danil on 23.02.2025.
//

#ifndef MQTT_PROTOCOL_HPP
#define MQTT_PROTOCOL_HPP

#include <cstdint>
#include <esp_err.h>
#include <string>

namespace MqttProtocol {

    /**
      * @brief Initializes the MQTT protocol module.
      *        Sets up subscriptions and other MQTT related configurations.
      * @return esp_err_t ESP_OK on success, error code otherwise.
      */
    esp_err_t init();

    /**
     * @brief Publishes the DALI device status to MQTT.
     *
     * @param address The DALI address of the device.
     * @param brightness The brightness level (0-254).
     * @param state The on/off state ("on" or "off").
     * @return esp_err_t ESP_OK on success, error code otherwise.
     */
    esp_err_t publishStatus(uint8_t address, uint8_t brightness, const std::string& state);


} // namespace MqttProtocol

#endif //MQTT_PROTOCOL_HPP
