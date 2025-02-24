//
// Created by danil on 23.02.2025.
//
// TODO: Move it to DALI module!
#include "mqtt_protocol.hpp"
#include "mqtt_client.hpp"
#include "DALI.hpp"
#include <esp_log.h>
#include <string>
#include <sstream>
#include <charconv> // For std::to_chars

static const char *TAG = "MqttProtocol";

namespace MqttProtocol {

// Helper function to create topic strings.
std::string createTopic(const std::string& base, uint8_t address, const std::string& subtopic) {
    std::stringstream ss;
    ss << base << "/" << static_cast<int>(address) << "/" << subtopic;  // Cast to int for output
    return ss.str();
}

// Callback to handle incoming MQTT messages.
void handleMqttMessage(const std::string& topic, const std::string& payload) {
    ESP_LOGI(TAG, "Received message on topic: %s, payload: %s", topic.c_str(), payload.c_str());

    // Basic parsing of topic to extract address and command type
    // Example topics:
    //  - dali/command/10/set_brightness
    //  - dali/command/10/turn_on
    //  - dali/command/10/turn_off

    std::string address_str;
    std::string command_type;
    size_t first_slash = topic.find('/', 0); //dali
    size_t second_slash = topic.find('/', first_slash + 1); // command
    size_t third_slash = topic.find('/', second_slash + 1); // 10
    if (first_slash != std::string::npos && second_slash != std::string::npos && third_slash!= std::string::npos) {
        address_str = topic.substr(second_slash + 1, third_slash - (second_slash + 1));
        command_type = topic.substr(third_slash + 1);

        uint8_t address;
        // Convert address string to integer. Use std::from_chars for efficient conversion.
        auto [ptr, ec] = std::from_chars(address_str.data(), address_str.data() + address_str.size(), address);
        if (ec != std::errc()) {
            ESP_LOGE(TAG, "Invalid address in MQTT topic: %s", address_str.c_str());
            return;
        }


         if (command_type == "set_brightness") {
                uint8_t brightness;
                auto [ptr2, ec2] = std::from_chars(payload.data(), payload.data() + payload.size(), brightness);
                if (ec2 != std::errc() ) {
                    ESP_LOGE(TAG, "Invalid brightness value in MQTT payload: %s", payload.c_str());
                    return;
                }
                DALI::setBrightness(address, false, brightness); // Assuming individual control.
            } else if (command_type == "turn_on") {
                 DALI::turnOn(address, false);
            } else if (command_type == "turn_off") {
                 DALI::turnOff(address, false);
            }else {
                ESP_LOGW(TAG, "Unknown command type: %s", command_type.c_str());
            }


    } else {
        ESP_LOGW(TAG, "Invalid MQTT topic format: %s", topic.c_str());
    }
}

int init() {
    // Subscribe to command topics.  Use a wildcard to subscribe to all addresses.
    return MqttClient::subscribe("dali/command/+/+", 0, handleMqttMessage);
}

int publishStatus(uint8_t address, uint8_t brightness, const std::string& state) {
    std::string brightness_topic = createTopic("dali/status", address, "brightness");
    std::string state_topic = createTopic("dali/status", address, "state");

    // Convert brightness to string. Use std::to_chars for most efficient conversion.
    char brightness_str[4]; // Max 3 digits + null terminator
    auto [ptr, ec] = std::to_chars(brightness_str, brightness_str + sizeof(brightness_str), brightness);
    if(ec != std::errc()) {
        ESP_LOGE(TAG, "Failed to convert brightness to string");
        return ESP_FAIL;
    }
    *ptr = '\0'; // Null-terminate the string


    esp_err_t err = MqttClient::publish(brightness_topic, brightness_str, 0, true);  // Retain status
    if (err != ESP_OK) {
        return err;
    }
    return MqttClient::publish(state_topic, state, 0, true); // Retain status
}

} // namespace MqttProtocol