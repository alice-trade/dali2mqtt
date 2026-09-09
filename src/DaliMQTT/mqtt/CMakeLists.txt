target_sources(DaliMQTT-Core PRIVATE
        MqttBridge.cxx
        MqttClient.cxx
        HomeAssistantDiscovery.cxx

        MqttBridge.hxx
        MqttClient.hxx
        HomeAssistantDiscovery.hxx

        MqttBridge.icc
        MqttClient.icc

        MqttMessage.hxx
)