target_sources(DaliMQTT-Core PRIVATE
        NetworkPlatform.hxx
        NetworkPlatform.icc
)

if(CONFIG_DALI2MQTT_NET_ETHERNET)
    message(STATUS "Network: Compiling Ethernet Backend (W5500/EMAC)")
    target_sources(DaliMQTT-Core PRIVATE EthernetBackend.cxx EthernetBackend.hxx EthernetBackend.icc)
    target_link_libraries(DaliMQTT-Core PUBLIC idf::esp_eth)
else()
    message(STATUS "Network: Compiling Wi-Fi Backend")
    target_sources(DaliMQTT-Core PRIVATE WifiBackend.cxx WifiBackend.hxx WifiBackend.icc)
    target_link_libraries(DaliMQTT-Core PUBLIC idf::esp_wifi)
endif()