#ifndef DALIMQTT_MQTTCOMMANDHANDLER_HXX
#define DALIMQTT_MQTTCOMMANDHANDLER_HXX

#include "DaliAPI.hxx"

namespace daliMQTT {

    class MQTTCommandHandler {
    public:
        MQTTCommandHandler() = delete;
        /**
         * @brief Handle mqtt message
         * @param topic
         * @param data
         */
        static void handle(const std::string& topic, const std::string& data);
    private:
        // MQTT handlers
        static void handleLightCommand(const std::vector<std::string_view>& parts, const std::string& data);
        static void handleGroupCommand(const std::string& data);
        static void handleSceneCommand(const std::string& data);
        static void processSendDALICommand(const std::string& data);
        static void handleSyncCommand(const std::string& data);
        // Publishing Light state
        static void publishLightState(dali_addressType_t addr_type, uint8_t target_id, const std::string& state, std::optional<uint8_t> brightness);
    };

} // namespace daliMQTT

#endif //DALIMQTT_MQTTCOMMANDHANDLER_HXX