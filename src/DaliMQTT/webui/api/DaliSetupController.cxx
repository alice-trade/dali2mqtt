#include <DaliDeviceController.hxx>
#include <DaliGroupManagement.hxx>
#include <DaliSceneManagement.hxx>
#include <utils/StringUtils.hxx>
#include "ConfigManager.hxx"
#include "WebUI.hxx"
#include "DaliAPI.hxx"
#include "utils/DaliLongAddrConversions.hxx"

namespace daliMQTT {
    enum class DaliTaskStatus { IDLE, SCANNING, INITIALIZING, REFRESHING_GROUPS };
    static std::atomic<DaliTaskStatus> g_dali_task_status{DaliTaskStatus::IDLE};

    static constexpr char  TAG[] = "WebUIDali";

    static void dali_scan_task(void*) {
        ESP_LOGI(TAG, "Starting background DALI scan...");
        DaliDeviceController::getInstance().performScan();
        DaliGroupManagement::getInstance().refreshAssignmentsFromBus();
        ESP_LOGI(TAG, "Background DALI scan finished.");
        g_dali_task_status = DaliTaskStatus::IDLE;
        vTaskDelete(nullptr);
    }

    esp_err_t WebUI::api::DaliGetDevicesHandler(httpd_req_t *req) {
        if (checkAuth(req) != ESP_OK) return ESP_FAIL;

        auto devices = DaliDeviceController::getInstance().getDevices();
        cJSON *root = cJSON_CreateArray();

        for (const auto& [long_addr, device] : devices) {
            cJSON* device_obj = cJSON_CreateObject();
            const auto addr_str = utils::longAddressToString(long_addr);

            cJSON_AddStringToObject(device_obj, "long_address", addr_str.data());
            cJSON_AddNumberToObject(device_obj, "short_address", device.short_address);

            cJSON_AddNumberToObject(device_obj, "level", device.current_level);
            cJSON_AddBoolToObject(device_obj, "available", device.available);
            cJSON_AddBoolToObject(device_obj, "is_input_device", device.is_input_device);
            const bool is_failure = (device.status_byte >> 1) & 0x01;
            cJSON_AddBoolToObject(device_obj, "lamp_failure", is_failure);

            if (device.device_type.has_value()) {
                cJSON_AddNumberToObject(device_obj, "dt", device.device_type.value());
            } else {
                cJSON_AddNullToObject(device_obj, "dt");
            }

            cJSON_AddItemToArray(root, device_obj);
        }

        char *json_string = cJSON_PrintUnformatted(root);
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, json_string, HTTPD_RESP_USE_STRLEN);
        cJSON_Delete(root);
        free(json_string);

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
        if (xTaskCreate(dali_scan_task, "dali_scan_task", 4096, nullptr, 4, nullptr) != pdPASS) {
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
        DaliDeviceController::getInstance().performFullInitialization();
        DaliGroupManagement::getInstance().refreshAssignmentsFromBus();
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
        if (xTaskCreate(dali_init_task, "dali_init_task", 4096, nullptr, 4, nullptr) != pdPASS) {
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
        const std::string response = utils::stringFormat(R"({"status":"%s"})", status_str);
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, response.c_str(), HTTPD_RESP_USE_STRLEN);
        return ESP_OK;
    }

    esp_err_t WebUI::api::DaliGetNamesHandler(httpd_req_t *req) {
        if (checkAuth(req) != ESP_OK) return ESP_FAIL;
        const auto cfg = ConfigManager::getInstance().getConfig();
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

        cJSON *root = cJSON_Parse(buf.data());
        if (!cJSON_IsObject(root)) {
            cJSON_Delete(root);
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON: root must be an object");
            return ESP_FAIL;
        }

        const cJSON* item = nullptr;
        cJSON_ArrayForEach(item, root) {
            if (!utils::stringToLongAddress(item->string)) {
                 cJSON_Delete(root);
                 httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON: all keys must be valid long addresses.");
                 return ESP_FAIL;
            }
            if (!cJSON_IsString(item)) {
                cJSON_Delete(root);
                httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON: all device names must be strings.");
                return ESP_FAIL;
            }
        }

        char* clean_json_string = cJSON_PrintUnformatted(root);
        cJSON_Delete(root);
        if (!clean_json_string) {
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to serialize JSON.");
            return ESP_FAIL;
        }
        ESP_LOGD(TAG, "Called set names with JSON: %s", clean_json_string);

        ConfigManager::getInstance().saveDaliDeviceIdentificators(clean_json_string);
        free(clean_json_string);

        httpd_resp_send(req, R"({"status":"ok", "message":"Device names saved."})", -1);
        return ESP_OK;
    }

    esp_err_t WebUI::api::DaliGetGroupsHandler(httpd_req_t *req) {
        if (checkAuth(req) != ESP_OK) return ESP_FAIL;

        const auto assignments = DaliGroupManagement::getInstance().getAllAssignments();
        cJSON *root = cJSON_CreateObject();

        for (const auto& [long_addr, groups] : assignments) {
            const auto addr_str = utils::longAddressToString(long_addr);
            cJSON* group_array = cJSON_CreateArray();
            for (int i = 0; i < 16; ++i) {
                if (groups.test(i)) {
                    cJSON_AddItemToArray(group_array, cJSON_CreateNumber(i));
                }
            }
            cJSON_AddItemToObject(root, addr_str.data(), group_array);
        }

        char *json_string = cJSON_PrintUnformatted(root);
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, json_string, HTTPD_RESP_USE_STRLEN);
        cJSON_Delete(root);
        free(json_string);
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

        cJSON *root = cJSON_Parse(buf.data());
        if (!cJSON_IsObject(root)) {
                httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON: root must be an object");
                cJSON_Delete(root);
                return ESP_FAIL;
        }

        GroupAssignments new_assignments;
        cJSON* device_item = nullptr;
        cJSON_ArrayForEach(device_item, root) {
            auto long_addr_opt = utils::stringToLongAddress(device_item->string);
            if (!long_addr_opt) {
                ESP_LOGW(TAG, "Skipping invalid long address key '%s' in set groups request.", device_item->string);
                continue;
            }

            std::bitset<16> groups;
            if (cJSON_IsArray(device_item)) {
                cJSON* group_item = nullptr;
                cJSON_ArrayForEach(group_item, device_item) {
                    if (cJSON_IsNumber(group_item) && group_item->valueint >= 0 && group_item->valueint < 16) {
                        groups.set(group_item->valueint);
                    }
                }
            }
            new_assignments[*long_addr_opt] = groups;
        }
        cJSON_Delete(root);

        DaliGroupManagement::getInstance().setAllAssignments(new_assignments);

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

        cJSON *root = cJSON_Parse(buf.data());
        if (!cJSON_IsObject(root)) {
                httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON: root must be an object");
                cJSON_Delete(root);
                return ESP_FAIL;
        }

        const cJSON* scene_id_item = cJSON_GetObjectItem(root, "scene_id");
        const cJSON* levels_item = cJSON_GetObjectItem(root, "levels");

        if (!cJSON_IsNumber(scene_id_item) || !cJSON_IsObject(levels_item)) {
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON: 'scene_id' or 'levels' are missing/invalid");
            cJSON_Delete(root);
            return ESP_FAIL;
        }

        const uint8_t scene_id = scene_id_item->valueint;
        SceneDeviceLevels levels; // This is map<short_addr, level>
        const auto& controller = DaliDeviceController::getInstance();

        const cJSON* level_item = nullptr;
        cJSON_ArrayForEach(level_item, levels_item) {
            auto long_addr_opt = utils::stringToLongAddress(level_item->string);
            if (!long_addr_opt) continue;

            auto short_addr_opt = controller.getShortAddress(*long_addr_opt);
            if (!short_addr_opt) continue;

            const uint8_t level = level_item->valueint;
            levels[*short_addr_opt] = level;
        }

        cJSON_Delete(root);

        DaliSceneManagement::getInstance().saveScene(scene_id, levels);

        httpd_resp_send(req, R"({"status":"ok", "message":"Scene configuration saved to devices."})", -1);
        return ESP_OK;
    }

    static void dali_refresh_groups_task(void*) {
        ESP_LOGI(TAG, "Starting background DALI group refresh...");
        DaliGroupManagement::getInstance().refreshAssignmentsFromBus();
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

        auto levels = DaliSceneManagement::getInstance().getSceneLevels(static_cast<uint8_t>(scene_id));

        cJSON *root = cJSON_CreateObject();
        cJSON_AddNumberToObject(root, "scene_id", scene_id);

        cJSON *levels_obj = cJSON_CreateObject();
        const auto& controller = DaliDeviceController::getInstance();

        for (const auto& [short_addr, level] : levels) {
            auto long_addr_opt = controller.getLongAddress(short_addr);
            if (long_addr_opt) {
                cJSON_AddNumberToObject(levels_obj, utils::longAddressToString(*long_addr_opt).data(), level);
            }
        }
        cJSON_AddItemToObject(root, "levels", levels_obj);

        char *json_string = cJSON_PrintUnformatted(root);
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, json_string, HTTPD_RESP_USE_STRLEN);
        cJSON_Delete(root);
        free(json_string);

        return ESP_OK;
    }
}