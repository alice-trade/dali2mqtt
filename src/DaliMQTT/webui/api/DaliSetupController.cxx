// Copyright (c) 2026 Alice-Trade Inc.
// SPDX-License-Identifier: GPL-2.0-or-later

#include <dali/DaliDeviceController.hxx>
#include <dali/DaliGroupManagement.hxx>
#include <dali/DaliSceneManagement.hxx>
#include <utils/StringUtils.hxx>
#include "system/ConfigManager.hxx"
#include "webui/WebUI.hxx"
#include "dali/DaliAdapter.hxx"
#include "utils/DaliLongAddrConversions.hxx"

namespace daliMQTT {
    enum class DaliTaskStatus { IDLE, SCANNING, INITIALIZING, REFRESHING_GROUPS };
    static std::atomic<DaliTaskStatus> g_dali_task_status{DaliTaskStatus::IDLE};

    static constexpr char  TAG[] = "WebUIDali";

    static void dali_scan_task(void*) {
        ESP_LOGI(TAG, "Starting background DALI scan...");
        DaliDeviceController::Instance().performScan();
        DaliGroupManagement::Instance().refreshAssignmentsFromBus();
        ESP_LOGI(TAG, "Background DALI scan finished.");
        g_dali_task_status = DaliTaskStatus::IDLE;
        vTaskDelete(nullptr);
    }

     esp_err_t WebUI::api::DaliGetDevicesHandler(httpd_req_t *req) {
        if (checkAuth(req) != ESP_OK) return ESP_FAIL;

        auto devices = DaliDeviceController::Instance().getDevices();
        JsonDocument doc;
        JsonArray root = doc.to<JsonArray>();

        for (const auto& dev : devices) {
            auto device_obj = root.add<JsonObject>();
            const auto& identity = getIdentity(dev);

            const auto addr_str = utils::longAddressToString(identity.long_address);
            device_obj["long_address"] = addr_str.data();

            if (!identity.gtin.empty()) {
                device_obj["gtin"] = identity.gtin.c_str();
            }

            if (const auto* gear = std::get_if<ControlGear>(&dev)) {
                device_obj["type"] = "gear";
                device_obj["short_address"] = gear->short_address;
                device_obj["level"] = gear->current_level;
                device_obj["available"] = gear->available;
                device_obj["lamp_failure"] = (gear->status_byte >> 1) & 0x01;

                if (gear->device_type.has_value()) {
                    device_obj["dt"] = gear->device_type.value();
                } else {
                    device_obj["dt"] = nullptr;
                }
                if (gear->static_data_loaded) {
                    device_obj["min"] = gear->min_level;
                    device_obj["max"] = gear->max_level;
                    device_obj["on_level"] = gear->power_on_level;
                    device_obj["fail_level"] = gear->system_failure_level;
                }
            }
            else if (const auto* id = std::get_if<InputDevice>(&dev)) {
                device_obj["type"] = "input";
                device_obj["short_address"] = id->short_address;
                device_obj["available"] = id->available;
            }
        }

        std::string json_string;
        serializeJson(doc, json_string);
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, json_string.c_str(), json_string.length());

        return ESP_OK;
    }

    esp_err_t WebUI::api::DaliScanHandler(httpd_req_t *req) {
        if (checkAuth(req) != ESP_OK) return ESP_FAIL;
        if (g_dali_task_status != DaliTaskStatus::IDLE) {
            httpd_resp_set_status(req, "409 Conflict");
            httpd_resp_send(req, "Another DALI operation is already in progress.", HTTPD_RESP_USE_STRLEN);
            return ESP_OK;
        }

        g_dali_task_status = DaliTaskStatus::SCANNING;
        if (xTaskCreate(dali_scan_task, "dali_scan_task", 8192, nullptr, 4, nullptr) != pdPASS) {
            g_dali_task_status = DaliTaskStatus::IDLE;
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to create scan task");
            return ESP_FAIL;
        }

        ESP_LOGI(TAG, "Triggering DALI scan from WebUI.");
        httpd_resp_set_status(req, "202 Accepted");
        httpd_resp_send(req, R"({"status":"ok", "message":"Scan initiated."})", -1);
        return ESP_OK;
    }

    static void dali_init_task(void*) {
        ESP_LOGI(TAG, "Starting background DALI initialization...");
        DaliDeviceController::Instance().performFullInitialization();
        DaliGroupManagement::Instance().refreshAssignmentsFromBus();
        ESP_LOGI(TAG, "Background DALI initialization finished.");
        g_dali_task_status = DaliTaskStatus::IDLE;
        vTaskDelete(nullptr);
    }

    esp_err_t WebUI::api::DaliInitializeHandler(httpd_req_t *req) {
        if (checkAuth(req) != ESP_OK) return ESP_FAIL;
        if (g_dali_task_status != DaliTaskStatus::IDLE) { // Check if busy
            httpd_resp_set_status(req, "409 Conflict");
            httpd_resp_send(req, "Another DALI operation is already in progress.", HTTPD_RESP_USE_STRLEN);
            return ESP_OK;
        }

        g_dali_task_status = DaliTaskStatus::INITIALIZING;
        if (xTaskCreate(dali_init_task, "dali_init_task", 8192, nullptr, 4, nullptr) != pdPASS) {
            g_dali_task_status = DaliTaskStatus::IDLE;
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to create initialization task");
            return ESP_FAIL;
        }

        ESP_LOGI(TAG, "Triggering DALI initialization from WebUI.");
        httpd_resp_set_status(req, "202 Accepted");
        httpd_resp_send(req, R"({"status":"ok", "message":"Initialization initiated."})", -1);
        return ESP_OK;
    }

    esp_err_t WebUI::api::DaliGetStatusHandler(httpd_req_t *req) {
        const char *status_str = nullptr;
        switch(g_dali_task_status.load()) {
            using enum DaliTaskStatus;
            case IDLE:
                status_str = "idle";
                break;
            case SCANNING:
                status_str = "scanning";
                break;
            case INITIALIZING:
                status_str = "initializing";
                break;
            case REFRESHING_GROUPS:
                status_str = "refreshing_groups";
                break;
        }
        char response[64];
        snprintf(response, sizeof(response), R"({"status":"%s"})", status_str);

        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, response, HTTPD_RESP_USE_STRLEN);
        return ESP_OK;
    }

    esp_err_t WebUI::api::DaliGetNamesHandler(httpd_req_t *req) {
        if (checkAuth(req) != ESP_OK) return ESP_FAIL;
        const auto cfg = ConfigManager::Instance().getConfig();
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, cfg.dali_device_identificators.c_str(), HTTPD_RESP_USE_STRLEN);
        return ESP_OK;
    }

    esp_err_t WebUI::api::DaliSetNamesHandler(httpd_req_t *req) {
        if (checkAuth(req) != ESP_OK) return ESP_FAIL;
        if (req->content_len >= 4096) {
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Request too long");
            return ESP_FAIL;
        }
        std::vector<char> buf(req->content_len + 1);
        const int ret = httpd_req_recv(req, buf.data(), req->content_len);
        if (ret <= 0) return ESP_FAIL;
        buf[ret] = '\0';

        JsonDocument doc;
        if (deserializeJson(doc, buf.data()) || !doc.is<JsonObject>()) {
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON: root must be an object");
            return ESP_FAIL;
        }

        for (JsonPair kv : doc.as<JsonObject>()) {
            if (!utils::stringToLongAddress(kv.key().c_str())) {
                httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON: all keys must be valid long addresses.");
                return ESP_FAIL;
            }
            if (!kv.value().is<const char*>()) {
                httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON: all device names must be strings.");
                return ESP_FAIL;
            }
        }

        std::string clean_json_string;
        serializeJson(doc, clean_json_string);
        ConfigManager::Instance().saveDaliDeviceIdentificators(clean_json_string);

        httpd_resp_send(req, R"({"status":"ok", "message":"Device names saved."})", -1);
        return ESP_OK;
    }


    esp_err_t WebUI::api::DaliGetGroupsHandler(httpd_req_t *req) {
        if (checkAuth(req) != ESP_OK) return ESP_FAIL;

        const auto assignments = DaliGroupManagement::Instance().getAllAssignments();
        JsonDocument doc;

        for (const auto& [long_addr, groups] : assignments) {
            const auto addr_str = utils::longAddressToString(long_addr);
            JsonArray group_array = doc[addr_str.data()].to<JsonArray>();
            for (int i = 0; i < 16; ++i) {
                if (groups.test(i)) {
                    group_array.add(i);
                }
            }
        }

        std::string json_string;
        serializeJson(doc, json_string);
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, json_string.c_str(), json_string.length());
        return ESP_OK;
    }

    esp_err_t WebUI::api::DaliSetGroupsHandler(httpd_req_t *req) {
        if (checkAuth(req) != ESP_OK) return ESP_FAIL;
        if (req->content_len >= 10240) {
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Request payload too large");
            return ESP_FAIL;
        }
        std::vector<char> buf(req->content_len + 1, 0);
        if (httpd_req_recv(req, buf.data(), req->content_len) <= 0) return ESP_FAIL;

        JsonDocument doc;
        if (deserializeJson(doc, buf.data()) || !doc.is<JsonObject>()) {
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON: root must be an object");
            return ESP_FAIL;
        }

        GroupAssignments new_assignments;
        for (JsonPair kv : doc.as<JsonObject>()) {
            auto long_addr_opt = utils::stringToLongAddress(kv.key().c_str());
            if (!long_addr_opt) continue;

            std::bitset<16> groups;
            if (kv.value().is<JsonArray>()) {
                for (JsonVariant v : kv.value().as<JsonArray>()) {
                    if (v.is<int>()) {
                        int g = v.as<int>();
                        if (g >= 0 && g < 16) groups.set(g);
                    }
                }
            }
            new_assignments.push_back({*long_addr_opt, groups});
        }

        DaliGroupManagement::Instance().setAllAssignments(new_assignments);

        httpd_resp_send(req, R"({"status":"ok", "message":"Group assignments saved."})", -1);
        return ESP_OK;
    }

    esp_err_t WebUI::api::DaliSetSceneHandler(httpd_req_t *req) {
        if (checkAuth(req) != ESP_OK) return ESP_FAIL;
        if (req->content_len >= 4096) {
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Request payload too large");
            return ESP_FAIL;
        }
        std::vector<char> buf(req->content_len + 1, 0);
        if (httpd_req_recv(req, buf.data(), req->content_len) <= 0) return ESP_FAIL;

        JsonDocument doc;
        if (deserializeJson(doc, buf.data()) || !doc.is<JsonObject>()) {
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON: root must be an object");
            return ESP_FAIL;
        }

        if (!doc["scene_id"].is<int>() || !doc["levels"].is<JsonObject>()) {
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON: 'scene_id' or 'levels' are missing/invalid");
            return ESP_FAIL;
        }

        const uint8_t scene_id = doc["scene_id"].as<int>();
        SceneDeviceLevels levels;
        levels.fill(255);
        const auto& controller = DaliDeviceController::Instance();

        for (JsonPair kv : doc["levels"].as<JsonObject>()) {
            auto long_addr_opt = utils::stringToLongAddress(kv.key().c_str());
            if (!long_addr_opt) continue;

            auto short_addr_opt = controller.getShortAddress(*long_addr_opt);
            if (!short_addr_opt) continue;

            if (kv.value().is<int>()) {
                levels[*short_addr_opt] = kv.value().as<int>();
            }
        }

        DaliSceneManagement::Instance().saveScene(scene_id, levels);

        httpd_resp_send(req, R"({"status":"ok", "message":"Scene configuration saved to devices."})", -1);
        return ESP_OK;
    }

    static void dali_refresh_groups_task(void*) {
        ESP_LOGI(TAG, "Starting background DALI group refresh...");
        DaliGroupManagement::Instance().refreshAssignmentsFromBus();
        ESP_LOGI(TAG, "Background DALI group refresh finished.");
        g_dali_task_status = DaliTaskStatus::IDLE;
        vTaskDelete(nullptr);
    }

    esp_err_t WebUI::api::DaliRefreshGroupsHandler(httpd_req_t *req) {
        if (checkAuth(req) != ESP_OK) return ESP_FAIL;
        if (g_dali_task_status != DaliTaskStatus::IDLE) {
            httpd_resp_set_status(req, "409 Conflict");
            httpd_resp_send(req, "Another DALI operation is already in progress.", HTTPD_RESP_USE_STRLEN);
            return ESP_OK;
        }
        g_dali_task_status = DaliTaskStatus::REFRESHING_GROUPS;
        if (xTaskCreate(dali_refresh_groups_task, "dali_refresh_groups_task", 4096, nullptr, 4, nullptr) != pdPASS) {
            g_dali_task_status = DaliTaskStatus::IDLE;
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to create refresh task");
            return ESP_FAIL;
        }
        ESP_LOGI(TAG, "Triggering DALI group refresh from WebUI.");
        httpd_resp_set_status(req, "202 Accepted");
        httpd_resp_send(req, R"({"status":"ok", "message":"Group refresh initiated."})", -1);
        return ESP_OK;
    }

    esp_err_t WebUI::api::DaliGetSceneHandler(httpd_req_t *req) {
        if (checkAuth(req) != ESP_OK) return ESP_FAIL;

        char buf[32];
        constexpr size_t buf_len = sizeof(buf);
        int scene_id = 0;

        if (httpd_req_get_url_query_str(req, buf, buf_len) == ESP_OK) {
            char param[8];
            if (httpd_query_key_value(buf, "id", param, sizeof(param)) == ESP_OK) {
                scene_id = atoi(param);
            }
        }

        if (scene_id < 0 || scene_id > 15) {
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid scene ID");
            return ESP_FAIL;
        }

        const auto levels = DaliSceneManagement::Instance().getSceneLevels(static_cast<uint8_t>(scene_id));

        JsonDocument doc;
        doc["scene_id"] = scene_id;

        JsonObject levels_obj = doc["levels"].to<JsonObject>();
        const auto& controller = DaliDeviceController::Instance();

        for (uint8_t short_addr = 0; short_addr < 64; ++short_addr) {
            uint8_t level = levels[short_addr];
            if (level != 255) {
                auto long_addr_opt = controller.getLongAddress(short_addr);
                if (long_addr_opt) {
                    levels_obj[utils::longAddressToString(*long_addr_opt).data()] = level;
                }
            }
        }

        std::string json_string;
        serializeJson(doc, json_string);
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, json_string.c_str(), json_string.length());

        return ESP_OK;
    }
}