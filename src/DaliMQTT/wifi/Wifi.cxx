#include <esp_netif.h>
#include <esp_sleep.h>
#include <lwip/ip4_addr.h>
#include <mdns.h>
#include "Wifi.hxx"
#include <ConfigManager.hxx>
#include "utils/NvsHandle.hxx"
#include "sdkconfig.h"

namespace daliMQTT
{
    static constexpr char  TAG[] = "WifiManager";

    esp_err_t Wifi::init() {
        if (initialized) {
            return ESP_OK;
        }

        ESP_ERROR_CHECK(esp_netif_init());
        ESP_ERROR_CHECK(esp_event_loop_create_default());

        wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
        ESP_ERROR_CHECK(esp_wifi_init(&cfg));

        ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                            ESP_EVENT_ANY_ID,
                                                            &wifiEventHandler,
                                                            this,
                                                            nullptr));
        ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                            IP_EVENT_STA_GOT_IP,
                                                            &wifiEventHandler,
                                                            this,
                                                            nullptr));

        initialized = true;
        ESP_LOGI(TAG, "WiFi Manager initialized.");
        return ESP_OK;
    }

    esp_err_t Wifi::connectToAP(const std::string& ssid, const std::string& password) {
        status = Status::CONNECTING;
        esp_netif_create_default_wifi_sta();

        wifi_config_t wifi_config = {};
        strncpy(reinterpret_cast<char*>(wifi_config.sta.ssid), ssid.c_str(), sizeof(wifi_config.sta.ssid) -1);
        wifi_config.sta.ssid[sizeof(wifi_config.sta.ssid) - 1] = '\0';

        strncpy(reinterpret_cast<char*>(wifi_config.sta.password), password.c_str(), sizeof(wifi_config.sta.password) -1);
        wifi_config.sta.password[sizeof(wifi_config.sta.password) - 1] = '\0';

        ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
        ESP_ERROR_CHECK(esp_wifi_start());

        ESP_LOGI(TAG, "Connecting to AP SSID: %s", ssid.c_str());
        return ESP_OK;
    }

    esp_err_t Wifi::startAP(const std::string& ssid, const std::string& password) {
        status = Status::AP_MODE;
        esp_netif_create_default_wifi_ap();

        wifi_config_t wifi_config = {};
        strncpy(reinterpret_cast<char*>(wifi_config.ap.ssid), ssid.c_str(), sizeof(wifi_config.ap.ssid) -1);
        wifi_config.ap.ssid[sizeof(wifi_config.ap.ssid) - 1] = '\0';
        strncpy(reinterpret_cast<char*>(wifi_config.ap.password), password.c_str(), sizeof(wifi_config.ap.password) -1);
        wifi_config.ap.password[sizeof(wifi_config.ap.password) - 1] = '\0';

        wifi_config.ap.ssid_len = static_cast<uint8_t>(std::min(ssid.length(), sizeof(wifi_config.ap.ssid)));
        wifi_config.ap.max_connection = 4;
        wifi_config.ap.authmode = WIFI_AUTH_WPA_WPA2_PSK;

        if (password.empty()) {
            wifi_config.ap.authmode = WIFI_AUTH_OPEN;
        }

        ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
        ESP_ERROR_CHECK(esp_wifi_start());

        ESP_LOGI(TAG, "AP Mode started. SSID: %s", ssid.c_str());
        return ESP_OK;
    }

    void Wifi::disconnect() {
        esp_wifi_disconnect();
        esp_wifi_stop();
        status = Status::DISCONNECTED;
    }

    std::string Wifi::getIpAddress() const {
        if (status != Status::CONNECTED) {
            return "0.0.0.0";
        }
        esp_netif_t* netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
        if (!netif) {
            return "0.0.0.0";
        }
        esp_netif_ip_info_t ip_info;
        esp_netif_get_ip_info(netif, &ip_info);
        return ip4addr_ntoa(reinterpret_cast<const ip4_addr_t*>(&ip_info.ip));
    }

    void Wifi::startMdns() {
        if (mdns_started) {
            return;
        }
        ESP_LOGI(TAG, "Starting mDNS service...");
        if (esp_err_t err = mdns_init(); err != ESP_OK) {
            ESP_LOGE(TAG, "mDNS Init failed: %s", esp_err_to_name(err));
            return;
        }
        auto config = ConfigManager::getInstance().getConfig();
        std::string hostname = config.http_domain;

        ESP_ERROR_CHECK(mdns_hostname_set(hostname.c_str()));
        ESP_ERROR_CHECK(mdns_instance_name_set("DALI to MQTT Bridge"));

        static std::array<mdns_txt_item_t, 1> serviceTxtData = {{
            {"path", "/"}
        }};
        ESP_ERROR_CHECK(mdns_service_add("DALI-to-MQTT Bridge Web UI", "_http", "_tcp", 80, serviceTxtData.data(), serviceTxtData.size()));
        ESP_LOGI(TAG, "mDNS service started, advertising %s.local", hostname.c_str());
        mdns_started = true;
    }

    void Wifi::wifiEventHandler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data) {
        auto* manager = static_cast<Wifi*>(arg);

        if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
            ESP_LOGI(TAG, "STA_START: connecting...");
            esp_wifi_connect();
        } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
            manager->status = Status::DISCONNECTED;
            if(manager->onDisconnected) manager->onDisconnected();

            const NvsHandle nvs_handle("wifi_state", NVS_READWRITE);
            if (!nvs_handle) {
                ESP_LOGE(TAG, "Failed to open NVS for WiFi state. Retrying connection without counter.");
                esp_wifi_connect();
                return;
            }

            uint8_t retry_count = 0;
            nvs_get_u8(nvs_handle.get(), "retry_cnt", &retry_count);
            retry_count++;

            if (retry_count >= CONFIG_DALI2MQTT_WIFI_MAX_RETRY) {
                ESP_LOGE(TAG, "WiFi connection failed after %d attempts. Entering deep sleep for %d seconds.",
                         retry_count, CONFIG_DALI2MQTT_WIFI_DEEP_SLEEP_S);

                nvs_set_u8(nvs_handle.get(), "retry_cnt", 0);
                nvs_commit(nvs_handle.get());

                esp_deep_sleep(1000000LL * CONFIG_DALI2MQTT_WIFI_DEEP_SLEEP_S);
            } else {
                ESP_LOGW(TAG, "STA_DISCONNECTED: connection failed. Attempt %d of %d. Retrying...",
                         retry_count, CONFIG_DALI2MQTT_WIFI_MAX_RETRY);

                nvs_set_u8(nvs_handle.get(), "retry_cnt", retry_count);
                nvs_commit(nvs_handle.get());

                esp_wifi_connect();
            }

        } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
            ip_event_got_ip_t const* event = static_cast<ip_event_got_ip_t*>(event_data);
            ESP_LOGI(TAG, "GOT_IP: " IPSTR, IP2STR(&event->ip_info.ip));

            const NvsHandle nvs_handle("wifi_state", NVS_READWRITE);
            if (nvs_handle) {
                ESP_LOGI(TAG, "WiFi connected successfully. Resetting failure counter.");
                nvs_set_u8(nvs_handle.get(), "retry_cnt", 0);
                nvs_commit(nvs_handle.get());
            }

            manager->status = Status::CONNECTED;
            manager->startMdns();
            if(manager->onConnected) manager->onConnected();
        } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_AP_START) {
            ESP_LOGI(TAG, "AP Mode started. Starting mDNS.");
            manager->startMdns();
        } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_AP_STACONNECTED) {
            auto const* event = static_cast<wifi_event_ap_staconnected_t*>(event_data);
            //ESP_LOGI(TAG, "station " MACSTR " join, AID=%d", MAC2STR(event->mac), event->aid);
        } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_AP_STADISCONNECTED) {
            auto const* event = static_cast<wifi_event_ap_stadisconnected_t*>(event_data);
            //ESP_LOGI(TAG, "station " MACSTR " leave, AID=%d", MAC2STR(event->mac), event->aid);
        }
    }
} // daliMQTT