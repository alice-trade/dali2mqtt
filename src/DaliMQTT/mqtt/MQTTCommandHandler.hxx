#ifndef DALIMQTT_MQTTCOMMANDHANDLER_HXX
#define DALIMQTT_MQTTCOMMANDHANDLER_HXX

#include "DaliAPI.hxx"

namespace daliMQTT {

    class MQTTCommandHandler {
    public:
        MQTTCommandHandler(const MQTTCommandHandler&) = delete;
        MQTTCommandHandler& operator=(const MQTTCommandHandler&) = delete;

        static MQTTCommandHandler& getInstance() {
            static MQTTCommandHandler instance;
            return instance;
        }

        /**
         * @brief Обрабатывает входящее MQTT-сообщение.
         * @param topic Тема сообщения.
         * @param data Данные сообщения.
         */
        void handle(const std::string& topic, const std::string& data);

    private:
        MQTTCommandHandler() = default;

        // Приватные методы для обработки конкретных команд
        static void handleLightCommand(const std::vector<std::string_view>& parts, const std::string& data);
        static void handleGroupCommand(const std::string& data);
        static void handleSceneCommand(const std::string& data);

        // Вспомогательная функция для публикации состояния
        static void publishLightState(dali_addressType_t addr_type, uint8_t target_id, const std::string& state, uint8_t brightness);
    };

} // namespace daliMQTT

#endif //DALIMQTT_MQTTCOMMANDHANDLER_HXX