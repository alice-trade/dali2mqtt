#ifndef DALIMQTT_MQTTCOMMANDHANDLER_HXX
#define DALIMQTT_MQTTCOMMANDHANDLER_HXX

#include "dali/DaliAdapter.hxx"

namespace daliMQTT {

    class MQTTCommandHandler {
    public:
        MQTTCommandHandler() = delete;
        /**
         * @brief Main handler for incoming MQTT messages.
         * @param topic
         * @param data
         */
        static void handle(const std::string& topic, const std::string& data);
    private:
        /** MQTT command handlers */
        static void handleLightCommand(const std::vector<std::string_view>& parts, const std::string& data);
        static void handleGroupCommand(const std::string& data);
        static void handleSceneCommand(const std::string& data);
        static void processSendDALICommand(const std::string& data);
        static void handleSyncCommand(const std::string& data);
        static void handleScanCommand();
        static void handleInitializeCommand();
        static void handleConfigGet();
        static void handleConfigSet(const std::string& data);
        // Background tasks
        static void backgroundScanTask(void* arg);
        static void backgroundInitTask(void* arg);
        static void backgroundInputInitTask(void* arg);

        // Publishing Light state
        static void publishLightState(dali_addressType_t addr_type, uint8_t target_id, const std::string& state_str, const DaliPublishState& state_data);
    };

} // namespace daliMQTT

#endif //DALIMQTT_MQTTCOMMANDHANDLER_HXX