target_sources(DaliMQTT-Core PRIVATE
        WebUI.hxx
        ApiContext.hxx
        WebUI.icc
)

if(CONFIG_DALI2MQTT_ENABLE_WEBUI)
    target_sources(DaliMQTT-Core PRIVATE
            WebUI.cxx
            ApiHandlers.cxx

            ApiHandlers.hxx
    )
    target_link_libraries(DaliMQTT-Core PUBLIC idf::esp_http_server)
endif()