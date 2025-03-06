//
// Created by danil on 25.02.2025.
//

#include "DALI.hpp"

#include "ConfigManager.h"
#include "ethernet.h"
#include "mqtt_client.h"
#include "mqtt_protocol.hpp"
#include "light_controller.hpp"
#include "dali_commander.hpp"
#include <string>
#include <sstream>
#include <chrono>
#include <stdexcept> // For std::runtime_error



namespace DaliModule {
 // Anonymous namespace for private implementation details

        // Configuration parameters (loaded from ConfigManager)
        int daliRxGpioPin;
        int daliTxGpioPin;
        int statusPollIntervalSec;
        std::string mqttCommandTopicBase;
        std::string mqttStatusTopicBase;

        // Task handles
        TaskHandle_t mqttCommandHandlerTaskHandle = nullptr;
        TaskHandle_t statusPollingTaskHandle = nullptr;

        // Forward declarations of internal functions/tasks
        void mqttCommandHandlerTask(void *pvParameters);
        void statusPollingTask(void *pvParameters);
        bool handleMqttCommand(const std::string& topic, const std::string& payload);
        void publishDaliStatus(uint8_t daliAddress, uint8_t brightness, const std::string& state);

        // --- MQTT Command Handling ---
        void mqttMessageHandler(const std::string& topic, const std::string& payload) {
            if (!handleMqttCommand(topic, payload)) {
//                ESP_LOGW(TAG, "Failed to handle MQTT command on topic: %s", topic.c_str());
            }
        }

        bool handleMqttCommand(const std::string& topic, const std::string& payload) {
            // Topic format: <mqttCommandTopicBase>/<daliAddress>/<command>
            // Example: dali/command/10/set_brightness, dali/command/20/turn_on

            size_t baseTopicPos = topic.find(mqttCommandTopicBase);
            if (baseTopicPos == std::string::npos || baseTopicPos != 0) {
//                ESP_LOGW(TAG, "Received command on unexpected topic base: %s, expected base: %s", topic.c_str(), mqttCommandTopicBase.c_str());
                return false;
            }

            std::string remainingTopic = topic.substr(mqttCommandTopicBase.length());
            if (remainingTopic.rfind("/", 0) != 0) { // Must start with '/' after base
//                ESP_LOGW(TAG, "Invalid topic format after base: %s", remainingTopic.c_str());
                return false;
            }
            remainingTopic = remainingTopic.substr(1); // Remove leading '/'

            size_t addressEndPos = remainingTopic.find('/');
            if (addressEndPos == std::string::npos) {
//                ESP_LOGW(TAG, "No command specified after address in topic: %s", topic.c_str());
                return false;
            }

            std::string addressStr = remainingTopic.substr(0, addressEndPos);
            std::string commandStr = remainingTopic.substr(addressEndPos + 1);

            uint8_t daliAddress;
            try {
                daliAddress = static_cast<uint8_t>(std::stoi(addressStr));
            } catch (const std::invalid_argument& e) {
//                ESP_LOGE(TAG, "Invalid DALI address in topic: %s, error: %s", topic.c_str(), e.what());
                return false;
            } catch (const std::out_of_range& e) {
//                ESP_LOGE(TAG, "DALI address out of range in topic: %s, error: %s", topic.c_str(), e.what());
                return false;
            }

//            ESP_LOGI(TAG, "Parsed MQTT command - Address: %d, Command: %s, Payload: %s", daliAddress, commandStr.c_str(), payload.c_str());

            bool commandSuccess = false;
            if (commandStr == "set_brightness") {
                uint8_t brightness;
                try {
                    brightness = static_cast<uint8_t>(std::stoi(payload));
                } catch (const std::invalid_argument& e) {
//                    ESP_LOGE(TAG, "Invalid brightness payload: %s, error: %s", payload.c_str(), e.what());
                    return false;
                } catch (const std::out_of_range& e) {
//                    ESP_LOGE(TAG, "Brightness payload out of range: %s, error: %s", payload.c_str(), e.what());
                    return false;
                }
                commandSuccess = DaliProtocol::setBrightness(daliAddress, false, brightness);
            } else if (commandStr == "turn_on") {
                commandSuccess = DaliProtocol::turnOn(daliAddress, false);
            } else if (commandStr == "turn_off") {
                commandSuccess = DaliProtocol::turnOff(daliAddress, false);
            } else if (commandStr == "query_status") {
                commandSuccess = LightController::queryAndPublishBrightness(daliAddress, false);
            }
            else {
//                ESP_LOGW(TAG, "Unknown command: %s", commandStr.c_str());
                return false;
            }

            if (!commandSuccess) {
//                ESP_LOGE(TAG, "Failed to execute DALI command: %s for address: %d", commandStr.c_str(), daliAddress);
                return false;
            }
            return true;
        }



        void publishDaliStatus(uint8_t daliAddress, uint8_t brightness, const std::string& state) {
            std::stringstream brightnessTopicStream;
            brightnessTopicStream << mqttStatusTopicBase << "/" << static_cast<int>(daliAddress) << "/brightness";
            std::string brightnessTopic = brightnessTopicStream.str();

            std::stringstream stateTopicStream;
            stateTopicStream << mqttStatusTopicBase << "/" << static_cast<int>(daliAddress) << "/state";
            std::string stateTopic = stateTopicStream.str();

            // Convert brightness to string
            std::stringstream brightnessPayloadStream;
            brightnessPayloadStream << static_cast<int>(brightness); // Ensure integer conversion
            std::string brightnessPayload = brightnessPayloadStream.str();

            if (!MqttClient::publish(brightnessTopic, brightnessPayload, 0, true)) {
//                ESP_LOGW(TAG, "Failed to publish brightness status to MQTT topic: %s", brightnessTopic.c_str());
            }
            if (!MqttClient::publish(stateTopic, state, 0, true)) {
//                ESP_LOGW(TAG, "Failed to publish state status to MQTT topic: %s", stateTopic.c_str());
            }
        }


        bool loadConfiguration() {
            daliRxGpioPin = ConfigManager::getDaliRxGpio();
            daliTxGpioPin = ConfigManager::getDaliTxGpio();
            statusPollIntervalSec = ConfigManager::getStatusPollIntervalSec();
            mqttCommandTopicBase = ConfigManager::getMqttCommandTopicBase();
            mqttStatusTopicBase = ConfigManager::getMqttStatusTopicBase();

            if (daliRxGpioPin == -1 || daliTxGpioPin == -1 || statusPollIntervalSec == -1 || mqttCommandTopicBase.empty() || mqttStatusTopicBase.empty()) {
//                ESP_LOGE(TAG, "Failed to load all DALI module configurations. Check NVS settings.");
                return false;
            }
//            ESP_LOGI(TAG, "DALI Configuration loaded: RX=%d, TX=%d, PollInterval=%d sec, CommandTopicBase=%s, StatusTopicBase=%s",
//                     daliRxGpioPin, daliTxGpioPin, statusPollIntervalSec, mqttCommandTopicBase.c_str(), mqttStatusTopicBase.c_str());
            return true;
        }



} // namespace DaliModule