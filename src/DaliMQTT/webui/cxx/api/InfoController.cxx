#include <esp_chip_info.h>

#include <esp_system.h>
#include "DaliAPI.hxx"
#include "DaliDeviceController.hxx"
#include "WebUI.hxx"
#include "Wifi.hxx"
#include "MQTTClient.hxx"

namespace daliMQTT {
    static constexpr char  TAG[] = "WebUIInfo";

 // --- Helpers for API Info ---
    static const char* get_chip_model_name(esp_chip_model_t model) {
        switch (model) {
            case CHIP_ESP32:   return "ESP32";
            case CHIP_ESP32S2: return "ESP32-S2";
            case CHIP_ESP32S3: return "ESP32-S3";
            case CHIP_ESP32C2: return "ESP32-C2";
            case CHIP_ESP32C3: return "ESP32-C3";
            case CHIP_ESP32C5: return "ESP32-C5";
            case CHIP_ESP32C6: return "ESP32-C6";
            case CHIP_ESP32H2: return "ESP32-H2";
            case CHIP_ESP32P4: return "ESP32-P4";
            default:           return "Unknown";
        }
    }

    static const char* get_mqtt_status_string(MqttStatus status) {
        switch (status) {
            case MqttStatus::CONNECTED:    return "Connected";
            case MqttStatus::CONNECTING:   return "Connecting";
            case MqttStatus::DISCONNECTED: return "Disconnected";
            default:                       return "Unknown";
        }
    }

    static const char* get_wifi_status_string(daliMQTT::Wifi::Status status) {
        switch (status) {
            case daliMQTT::Wifi::Status::CONNECTED:    return "Connected (STA)";
            case daliMQTT::Wifi::Status::CONNECTING:   return "Connecting (STA)";
            case daliMQTT::Wifi::Status::DISCONNECTED: return "Disconnected";
            case daliMQTT::Wifi::Status::AP_MODE:      return "Access Point";
            default:                                   return "Unknown";
        }
    }

    // --- API Handlers ---
    esp_err_t WebUI::api::GetInfoHandler(httpd_req_t *req) {
        if (checkAuth(req) != ESP_OK) return ESP_FAIL;

        esp_chip_info_t chip_info;
        esp_chip_info(&chip_info);

        // Get DALI status
        const auto& dali_api = DaliAPI::getInstance();
        std::string dali_status;
        if (dali_api.isInitialized()) {
            const auto discovered_devices = DaliDeviceController::getInstance().getDiscoveredDevices().count();
            dali_status = std::format("Active, {} devices found", discovered_devices);
        } else {
            dali_status = "Inactive (Provisioning Mode)";
        }

        // Get MQTT status
        const auto mqtt_status = MQTTClient::getInstance().getStatus();
        const char* mqtt_status_str = get_mqtt_status_string(mqtt_status);

        // Get WiFi status
        const auto& wifi = Wifi::getInstance();
        const std::string wifi_status_str = std::format("{} ({})", get_wifi_status_string(wifi.getStatus()), wifi.getIpAddress());

        cJSON *root = cJSON_CreateObject();
        cJSON_AddStringToObject(root, "version", DALIMQTT_VERSION);
        cJSON_AddStringToObject(root, "chip_model", get_chip_model_name(chip_info.model));
        cJSON_AddNumberToObject(root, "chip_cores", chip_info.cores);
        cJSON_AddNumberToObject(root, "free_heap", esp_get_free_heap_size());
        cJSON_AddStringToObject(root, "dali_status", dali_status.c_str());
        cJSON_AddStringToObject(root, "mqtt_status", mqtt_status_str);
        cJSON_AddStringToObject(root, "wifi_status", wifi_status_str.c_str());

        char *json_string = cJSON_Print(root);
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, json_string, strlen(json_string));
        cJSON_Delete(root);
        free( json_string);
        return ESP_OK;
    }

}