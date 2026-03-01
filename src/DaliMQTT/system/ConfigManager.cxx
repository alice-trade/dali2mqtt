// Copyright (c) 2026 Alice-Trade Inc.
// SPDX-License-Identifier: GPL-3.0-or-later

#include "system/ConfigManager.hxx"
#include <esp_littlefs.h>
#include <dirent.h>
#include <esp_mac.h>
#include <utils/StringUtils.hxx>
#include "dali/DaliCommon.hxx"

namespace daliMQTT
{
    static constexpr char  TAG[] = "Config";

    esp_err_t ConfigManager::init() {
        if (initialized) {
            return ESP_OK;
        }
        esp_err_t ret = nvs_flash_init();
        if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
            ESP_LOGW(TAG, "NVS partition was truncated, erasing and re-initializing...");
            ESP_ERROR_CHECK(nvs_flash_erase());
            ret = nvs_flash_init();
        }
        ESP_ERROR_CHECK(ret);

        ret = initLittleFs();
        ESP_ERROR_CHECK(ret);

        if (ret == ESP_OK) {
            initialized = true;
            ESP_LOGI(TAG, "NVS and FS initialized successfully.");
        }
        return ret;
    }

    esp_err_t ConfigManager::initLittleFs() {
        ESP_LOGI(TAG, "Initializing LittleFS");

        esp_vfs_littlefs_conf_t conf = {
            .base_path = "/littlefs",
            .partition_label = CONFIG_DALI2MQTT_WEBUI_SPIFFS_PARTITION_LABEL,
            .format_if_mount_failed = false,
            .dont_mount = false,
        };

        esp_err_t ret = esp_vfs_littlefs_register(&conf);

        if (ret != ESP_OK) {
            if (ret == ESP_FAIL) {
                ESP_LOGE(TAG, "Failed to mount or format filesystem");
            } else if (ret == ESP_ERR_NOT_FOUND) {
                ESP_LOGE(TAG, "Failed to find LittleFS partition");
            } else {
                ESP_LOGE(TAG, "Failed to initialize LittleFS (%s)", esp_err_to_name(ret));
            }
            return ret;
        }
        return ESP_OK;
    }


    esp_err_t ConfigManager::load() {
        std::lock_guard<std::mutex> lock(config_mutex);

        const NvsHandle nvs_handle(NVS_NAMESPACE, NVS_READWRITE);
        if (!nvs_handle) {
            return ESP_FAIL;
        }
        AppConfig temp_cfg;

        getString(nvs_handle.get(), "wifi_ssid", temp_cfg.wifi_ssid, "");
        getString(nvs_handle.get(), "wifi_pass", temp_cfg.wifi_password, "");
        getString(nvs_handle.get(), "mqtt_uri", temp_cfg.mqtt_uri, "");
        getString(nvs_handle.get(), "mqtt_user", temp_cfg.mqtt_user, "");
        getString(nvs_handle.get(), "mqtt_pass", temp_cfg.mqtt_pass, "");
        getString(nvs_handle.get(), "mqtt_cert", temp_cfg.mqtt_ca_cert, "");
        getString(nvs_handle.get(), "cid", temp_cfg.client_id, "");
        getString(nvs_handle.get(), "mqtt_base", temp_cfg.mqtt_base_topic, CONFIG_DALI2MQTT_MQTT_BASE_TOPIC);
        getString(nvs_handle.get(), "http_domain", temp_cfg.http_domain, CONFIG_DALI2MQTT_WEBUI_DEFAULT_MDNS_DOMAIN);
        getString(nvs_handle.get(), "http_user", temp_cfg.http_user, CONFIG_DALI2MQTT_WEBUI_DEFAULT_USER);
        getString(nvs_handle.get(), "http_pass", temp_cfg.http_pass, CONFIG_DALI2MQTT_WEBUI_DEFAULT_PASS);
        getString(nvs_handle.get(), "dali_identif", temp_cfg.dali_device_identificators, "{}");
        getString(nvs_handle.get(), "dali_groups", temp_cfg.dali_group_assignments, "{}");
        getString(nvs_handle.get(), "syslog_srv", temp_cfg.syslog_server, "");
        #ifdef CONFIG_DALI2MQTT_SYSLOG_ENABLED_BY_DEFAULT
        if (temp_cfg.syslog_server.empty() && strlen(CONFIG_DALI2MQTT_SYSLOG_DEFAULT_SERVER) > 0) {
            temp_cfg.syslog_server = CONFIG_DALI2MQTT_SYSLOG_DEFAULT_SERVER;
        }
        #endif

        for (uint8_t i = 0; i < Constants::MaxBuses; ++i) {
            uint8_t en = 0;
            int32_t rx = -1, tx = -1;

            if (i == 0) {
                #ifdef CONFIG_DALI2MQTT_DALI_RX_PIN
                                rx = CONFIG_DALI2MQTT_DALI_RX_PIN;
                #endif
                #ifdef CONFIG_DALI2MQTT_DALI_TX_PIN
                                tx = CONFIG_DALI2MQTT_DALI_TX_PIN;
                #endif
            }
            nvs_get_u8(nvs_handle.get(), utils::stringFormat("b%d_en", i).c_str(), &en);
            nvs_get_i32(nvs_handle.get(), utils::stringFormat("b%d_rx", i).c_str(), &rx);
            nvs_get_i32(nvs_handle.get(), utils::stringFormat("b%d_tx", i).c_str(), &tx);
            temp_cfg.buses[i].enabled = (en == 1);
            temp_cfg.buses[i].rx_pin = rx;
            temp_cfg.buses[i].tx_pin = tx;
        }

        getString(nvs_handle.get(), "ota_url", temp_cfg.app_ota_url, "");
        getU32(nvs_handle.get(), "dali_poll", temp_cfg.dali_poll_interval_ms, CONFIG_DALI2MQTT_DALI_DEFAULT_POLL_INTERVAL_MS);

        #ifdef CONFIG_DALI2MQTT_SYSLOG_ENABLED_BY_DEFAULT
                uint8_t syslog_enabled_flag = 1;
        #else
                uint8_t syslog_enabled_flag = 0;
        #endif
        nvs_get_u8(nvs_handle.get(), "syslog_en", &syslog_enabled_flag);
        temp_cfg.syslog_enabled = (syslog_enabled_flag == 1);

        uint8_t hass_disc_flag = 0;
        nvs_get_u8(nvs_handle.get(), "hass_disc", &hass_disc_flag);
        temp_cfg.hass_discovery_enabled = (hass_disc_flag == 1);

        if (temp_cfg.client_id.empty()) {
          uint8_t mac[6];
          esp_read_mac(mac, ESP_MAC_WIFI_STA);
          temp_cfg.client_id = utils::stringFormat("dali_%02x%02x%02x", mac[3], mac[4], mac[5]);
        }

        uint8_t configured_flag = 0;
        nvs_get_u8(nvs_handle.get(), "configured", &configured_flag);
        ESP_LOGI(TAG, "Configured flag value: %d", configured_flag);
        temp_cfg.configured = (configured_flag == 1);

        config_cache = std::make_shared<const AppConfig>(std::move(temp_cfg));

        ESP_LOGI(TAG, "Configuration loaded successfully.");
        return ESP_OK;
    }

    esp_err_t ConfigManager::writeBasicSettings(const nvs_handle_t handle, const AppConfig& cfg) {
        esp_err_t err;
        #define SetNVS(func, key, value, ...) \
        if ((err = func(handle, key, value, ##__VA_ARGS__)) != ESP_OK) return err;
        SetNVS(setString, "wifi_ssid", cfg.wifi_ssid);
        SetNVS(setString, "wifi_pass", cfg.wifi_password);
        SetNVS(setString, "mqtt_uri",  cfg.mqtt_uri);
        SetNVS(setString, "mqtt_user", cfg.mqtt_user);
        SetNVS(setString, "mqtt_pass", cfg.mqtt_pass);
        SetNVS(setString, "mqtt_cert", cfg.mqtt_ca_cert);
        SetNVS(setString, "cid",       cfg.client_id);
        SetNVS(setString, "mqtt_base", cfg.mqtt_base_topic);
        SetNVS(setString, "http_domain", cfg.http_domain);
        SetNVS(setString, "http_user",   cfg.http_user);
        SetNVS(setString, "http_pass",   cfg.http_pass);
        SetNVS(setString, "syslog_srv",  cfg.syslog_server);
        SetNVS(nvs_set_u8, "syslog_en",  cfg.syslog_enabled ? 1 : 0);
        SetNVS(setString, "ota_url",     cfg.app_ota_url);
        SetNVS(nvs_set_u32, "dali_poll", cfg.dali_poll_interval_ms);
        SetNVS(nvs_set_u8, "hass_disc",  cfg.hass_discovery_enabled ? 1 : 0);
        for (uint8_t i = 0; i < Constants::MaxBuses; ++i) {
            SetNVS(nvs_set_u8, utils::stringFormat("b%d_en", i).c_str(), cfg.buses[i].enabled ? 1 : 0);
            SetNVS(nvs_set_i32, utils::stringFormat("b%d_rx", i).c_str(), cfg.buses[i].rx_pin);
            SetNVS(nvs_set_i32, utils::stringFormat("b%d_tx", i).c_str(), cfg.buses[i].tx_pin);
        }
        #undef SetNVS
        return ESP_OK;
    }

    esp_err_t ConfigManager::saveMainConfig(const AppConfig& new_config) {
        return processConfigUpdate([this, &new_config](nvs_handle_t handle) {
            config_cache = std::make_shared<const AppConfig>(new_config);
            return writeBasicSettings(handle, *config_cache);
        });
    }

    esp_err_t ConfigManager::saveDaliDeviceIdentificators(const std::string& identificators) {
        return processConfigUpdate([this, &identificators](nvs_handle_t handle) {
            AppConfig updated_cfg = *config_cache;
            updated_cfg.dali_device_identificators = identificators;
            config_cache = std::make_shared<const AppConfig>(std::move(updated_cfg));
            return setString(handle, "dali_identif", identificators);
        });
    }

    esp_err_t ConfigManager::saveDaliGroupAssignments(const std::string& assignments) {
         return processConfigUpdate([this, &assignments](nvs_handle_t handle) {
             AppConfig updated_cfg = *config_cache;
             updated_cfg.dali_group_assignments = assignments;
             config_cache = std::make_shared<const AppConfig>(std::move(updated_cfg));
            return setString(handle, "dali_groups", assignments);
        });
    }

    esp_err_t ConfigManager::save() {
        return processConfigUpdate([this](const nvs_handle_t handle) {
            esp_err_t err = writeBasicSettings(handle, *config_cache);
            if (err != ESP_OK) return err;

            if ((err = setString(handle, "dali_identif", config_cache->dali_device_identificators)) != ESP_OK) return err;
            return setString(handle, "dali_groups", config_cache->dali_group_assignments);
        });
    }

    esp_err_t ConfigManager::resetConfiguredFlag() {
        return processConfigUpdate([this](nvs_handle_t handle) {
            AppConfig updated_cfg = *config_cache;

            updated_cfg.configured = false;
            config_cache = std::make_shared<const AppConfig>(std::move(updated_cfg));

            const esp_err_t err = nvs_set_u8(handle, "configured", 0);
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "Failed to set configured flag to 0: %s", esp_err_to_name(err));
            }
            return err;
        });
    }

    esp_err_t ConfigManager::ensureConfiguredAndCommit(nvs_handle_t handle) {
        if (!config_cache->configured) {
            AppConfig updated_cfg = *config_cache;
            updated_cfg.configured = true;
            config_cache = std::make_shared<const AppConfig>(std::move(updated_cfg));

            const esp_err_t err = nvs_set_u8(handle, "configured", 1);
            if (err != ESP_OK) return err;
        }

        const esp_err_t err = nvs_commit(handle);
        if (err != ESP_OK) return err;
        ESP_LOGI(TAG, "Configuration saved successfully.");
        return err;
    }


    std::shared_ptr<const AppConfig> ConfigManager::getConfig() const {
        std::lock_guard<std::mutex> lock(config_mutex);
        return config_cache; // Только инкремент счетчика ссылок!
    }

    void ConfigManager::setConfig(const AppConfig& new_config) {
        std::lock_guard<std::mutex> lock(config_mutex);
        config_cache = std::make_shared<AppConfig>(new_config);
    }


    bool ConfigManager::isConfigured() const {
        std::lock_guard<std::mutex> lock(config_mutex);
        return config_cache->configured;
    }

    esp_err_t ConfigManager::getString(nvs_handle_t handle, const char* key, std::string& out_value, const char* default_value) {
        size_t required_size = 0;
        esp_err_t err = nvs_get_str(handle, key, nullptr, &required_size);

        if (err == ESP_ERR_NVS_NOT_FOUND) {
            if (default_value) {
                out_value = default_value;
                 ESP_LOGW(TAG, "Key '%s' not found in NVS, using default value: '%s'", key, default_value);
            } else {
                out_value.clear();
            }
            return ESP_OK;
        }
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Error reading key '%s': %s", key, esp_err_to_name(err));
            return err;
        }
        if (required_size == 0) {
            out_value.clear();
            return ESP_OK;
        }

        std::vector<char> buf(required_size);
        err = nvs_get_str(handle, key, buf.data(), &required_size);
        if (err == ESP_OK) {
            out_value.assign(buf.data(), required_size > 0 ? required_size - 1 : 0);
        }
        return err;
    }

    esp_err_t ConfigManager::setString(const nvs_handle_t handle, const char* key, const std::string& value) {
        return nvs_set_str(handle, key, value.c_str());
    }


    esp_err_t ConfigManager::getU32(const nvs_handle_t handle, const char* key, uint32_t& out_value, uint32_t default_value) {
        const esp_err_t err = nvs_get_u32(handle, key, &out_value);
        if (err == ESP_ERR_NVS_NOT_FOUND) {
            out_value = default_value;
            ESP_LOGW(TAG, "Key %s not found in NVS, using default value: %lu", key, static_cast<unsigned long>(default_value));
            return ESP_OK;
        }
        return err;
    }

    std::string ConfigManager::getMqttBaseTopic() const {
        std::lock_guard<std::mutex> lock(config_mutex);
        return config_cache->mqtt_base_topic;
    }

    std::string ConfigManager::getSerializedConfig(const bool mask_passwords) const {
        auto cfg = getConfig();
        JsonDocument doc;

        doc["wifi_ssid"] = cfg->wifi_ssid;
        doc["mqtt_uri"] = cfg->mqtt_uri;
        doc["mqtt_user"] = cfg->mqtt_user;
        doc["client_id"] = cfg->client_id;
        doc["mqtt_base_topic"] = cfg->mqtt_base_topic;
        doc["http_domain"] = cfg->http_domain;
        doc["http_user"] = cfg->http_user;
        doc["syslog_server"] = cfg->syslog_server;
        doc["syslog_enabled"] = cfg->syslog_enabled;
        doc["dali_poll_interval_ms"] = cfg->dali_poll_interval_ms;
        doc["ota_url"] = cfg->app_ota_url;
        doc["hass_discovery_enabled"] = cfg->hass_discovery_enabled;

        const auto busesArray = doc["buses"].to<JsonArray>();
        for (const auto& b : cfg->buses) {
            auto busObj = busesArray.add<JsonObject>();
            busObj["enabled"] = b.enabled;
            busObj["rx_pin"] = b.rx_pin;
            busObj["tx_pin"] = b.tx_pin;
        }

        const char* pass_placeholder = mask_passwords ? "***" : "";
        doc["wifi_password"] = mask_passwords ? pass_placeholder : cfg->wifi_password;
        doc["mqtt_pass"] = mask_passwords ? pass_placeholder : cfg->mqtt_pass;
        doc["http_pass"] = mask_passwords ? pass_placeholder : cfg->http_pass;

        std::string json_string;
        serializeJson(doc, json_string);
        return json_string;
    }

    ConfigUpdateResult ConfigManager::updateConfigFromJson(const char* json_str) {
        JsonDocument doc;
        DeserializationError error = deserializeJson(doc, json_str);

        if (error) {
            ESP_LOGE(TAG, "Failed to parse configuration JSON: %s", error.c_str());
            return ConfigUpdateResult::NoUpdate;
        }

        auto current_cfg = getConfig();
        AppConfig new_cfg = *current_cfg;
        AppConfig old_cfg = new_cfg;
        bool changed = false;

        #define JsonSetStrConfig(NAME, KEY) \
            if (doc[KEY].is<const char*>()) { \
                std::string val = doc[KEY].as<std::string>(); \
                if (!val.empty() && val != "***" && new_cfg.NAME != val) { \
                    new_cfg.NAME = val; \
                    changed = true; \
                } \
            }

        JsonSetStrConfig(wifi_ssid, "wifi_ssid");
        JsonSetStrConfig(wifi_password, "wifi_pass");
        JsonSetStrConfig(wifi_password, "wifi_password");

        JsonSetStrConfig(mqtt_uri, "mqtt_uri");
        JsonSetStrConfig(mqtt_user, "mqtt_user");
        JsonSetStrConfig(mqtt_pass, "mqtt_pass");

        if (doc["mqtt_ca_cert"].is<const char*>()) {
            auto val = doc["mqtt_ca_cert"].as<std::string>();
            if (val != "***" && new_cfg.mqtt_ca_cert != val) {
                new_cfg.mqtt_ca_cert = val;
                changed = true;
            }
        }

        JsonSetStrConfig(client_id, "client_id");
        JsonSetStrConfig(client_id, "cid");

        JsonSetStrConfig(mqtt_base_topic, "mqtt_base_topic");
        JsonSetStrConfig(mqtt_base_topic, "mqtt_base");

        JsonSetStrConfig(http_domain, "http_domain");
        JsonSetStrConfig(http_user, "http_user");
        JsonSetStrConfig(http_pass, "http_pass");
        JsonSetStrConfig(syslog_server, "syslog_server");
        JsonSetStrConfig(syslog_server, "syslog_srv");

        JsonSetStrConfig(app_ota_url, "ota_url");

        #undef JsonSetStrConfig

        auto checkBool = [&](const char* key, bool& target) {
            if (!doc[key].isNull()) {
                bool val = doc[key].is<bool>() ? doc[key].as<bool>() : (doc[key].as<int>() != 0);
                if (target != val) { target = val; changed = true; }
            }
        };

        checkBool("syslog_enabled", new_cfg.syslog_enabled);
        checkBool("syslog_en", new_cfg.syslog_enabled);
        checkBool("hass_discovery_enabled", new_cfg.hass_discovery_enabled);

        auto checkNum = [&](const char* key, uint32_t& target) {
            if (doc[key].is<uint32_t>()) {
                const auto val = doc[key].as<uint32_t>();
                if (target != val) { target = val; changed = true; }
            }
        };

        checkNum("dali_poll_interval_ms", new_cfg.dali_poll_interval_ms);
        checkNum("dali_poll", new_cfg.dali_poll_interval_ms);

        if (doc["buses"].is<JsonArray>()) {
            auto arr = doc["buses"].as<JsonArray>();
            for (uint8_t i = 0; i < Constants::MaxBuses && i < arr.size(); ++i) {
                auto bus = arr[i].as<JsonObject>();
                if (bus["enabled"].is<bool>() && new_cfg.buses[i].enabled != bus["enabled"].as<bool>()) {
                    new_cfg.buses[i].enabled = bus["enabled"].as<bool>(); changed = true;
                }
                if (bus["rx_pin"].is<int>() && new_cfg.buses[i].rx_pin != bus["rx_pin"].as<int>()) {
                    new_cfg.buses[i].rx_pin = bus["rx_pin"].as<int>(); changed = true;
                }
                if (bus["tx_pin"].is<int>() && new_cfg.buses[i].tx_pin != bus["tx_pin"].as<int>()) {
                    new_cfg.buses[i].tx_pin = bus["tx_pin"].as<int>(); changed = true;
                }
            }
        }
        if (!changed) {
            return ConfigUpdateResult::NoUpdate;
        }

        if(new_cfg.wifi_ssid.empty() || new_cfg.mqtt_uri.empty()) {
            ESP_LOGE(TAG, "SSID and MQTT URI cannot be empty");
            return ConfigUpdateResult::NoUpdate;
        }

        if (esp_err_t saveResult = saveMainConfig(new_cfg); saveResult != ESP_OK) {
            return ConfigUpdateResult::NoUpdate;
        }

        if (old_cfg.wifi_ssid != new_cfg.wifi_ssid || old_cfg.wifi_password != new_cfg.wifi_password) {
            return ConfigUpdateResult::WIFIUpdate;
        }

        if (old_cfg.mqtt_uri != new_cfg.mqtt_uri || old_cfg.mqtt_user != new_cfg.mqtt_user ||
            old_cfg.mqtt_pass != new_cfg.mqtt_pass || old_cfg.mqtt_ca_cert != new_cfg.mqtt_ca_cert ||
            old_cfg.mqtt_base_topic != new_cfg.mqtt_base_topic || old_cfg.client_id != new_cfg.client_id) {
            return ConfigUpdateResult::MQTTUpdate;
        }

        return ConfigUpdateResult::SystemUpdate;
    }
}
