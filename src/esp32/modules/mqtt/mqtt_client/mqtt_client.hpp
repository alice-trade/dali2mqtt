//
// Created by danil on 23.02.2025.
//

#ifndef MQTT_CLIENT_HPP
#define MQTT_CLIENT_HPP


#include <esp_err.h>
#include <string>
#include <functional>

namespace MqttClient {

    using MqttMessageHandler = std::function<void(const std::string& topic, const std::string& payload)>;

    // Оставляем esp_err_t для init, start и stop, так как там важна информация об ошибках MQTT клиента.
    esp_err_t init(const std::string& broker_url, const std::string& client_id);
    esp_err_t start();
    esp_err_t stop();

    // Для publish, subscribe и unsubscribe используем bool: true - успех, false - ошибка.
    bool publish(const std::string& topic, const std::string& payload, int qos = 0, bool retain = false);
    bool subscribe(const std::string& topic, int qos, MqttMessageHandler handler);
    bool unsubscribe(const std::string& topic);

} // namespace MqttClient


#endif //MQTT_CLIENT_HPP
