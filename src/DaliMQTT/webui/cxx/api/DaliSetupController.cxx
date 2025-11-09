#include <DaliDeviceController.hxx>
#include <DaliGroupManagement.hxx>
#include <DaliSceneManagement.hxx>
#include "ConfigManager.hxx"
#include "WebUI.hxx"
#include "DaliAPI.hxx"


namespace daliMQTT {
    enum class DaliTaskStatus { IDLE, SCANNING, INITIALIZING };
    static std::atomic<DaliTaskStatus> g_dali_task_status = DaliTaskStatus::IDLE;

    static constexpr char  TAG[] = "WebUIDali";

    static void dali_scan_task(void*) {
        ESP_LOGI(TAG, "Starting background DALI scan...");
        DaliDeviceController::getInstance().performScan();
        ESP_LOGI(TAG, "Background DALI scan finished.");
        g_dali_task_status = DaliTaskStatus::IDLE;
        vTaskDelete(nullptr);
    }
    esp_err_t WebUI::api::DaliGetDevicesHandler(httpd_req_t *req) {
            if (checkAuth(req) != ESP_OK) return ESP_FAIL;

            auto devices = DaliDeviceController::getInstance().getDiscoveredDevices();
            cJSON *root = cJSON_CreateArray();

            for (int i = 0; i < 64; ++i) {
                if (devices.test(i)) {
                    cJSON_AddItemToArray(root, cJSON_CreateNumber(i));
                }
            }

            char *json_string = cJSON_PrintUnformatted(root);
            httpd_resp_set_type(req, "application/json");
            httpd_resp_send(req, json_string, strlen(json_string));
            cJSON_Delete(root);
            free( json_string);

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
            const char* status_str = (g_dali_task_status == DaliTaskStatus::IDLE) ? "idle" : ((g_dali_task_status == DaliTaskStatus::SCANNING) ? "scanning" : "initializing");
            const std::string response = std::format(R"({{"status":"{}"}})", status_str);
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
            if (req->content_len >= 1024) {
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

            for (const auto& [addr, groups] : assignments) {
                cJSON* group_array = cJSON_CreateArray();
                for (int i = 0; i < 16; ++i) {
                    if (groups.test(i)) {
                        cJSON_AddItemToArray(group_array, cJSON_CreateNumber(i));
                    }
                }
                cJSON_AddItemToObject(root, std::to_string(addr).c_str(), group_array);
            }

            char *json_string = cJSON_PrintUnformatted(root);
            httpd_resp_set_type(req, "application/json");
            httpd_resp_send(req, json_string, strlen(json_string));
            cJSON_Delete(root);
            free( json_string);
            return ESP_OK;
        }

        esp_err_t WebUI::api::DaliSetGroupsHandler(httpd_req_t *req) {
            if (checkAuth(req) != ESP_OK) return ESP_FAIL;

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
                uint8_t addr = std::stoi(device_item->string);
                ESP_LOGD(TAG, "Called set group with JSON: %s", device_item->string);
                std::bitset<16> groups;
                if (cJSON_IsArray(device_item)) {
                    cJSON* group_item = nullptr;
                    cJSON_ArrayForEach(group_item, device_item) {
                        if (cJSON_IsNumber(group_item) && group_item->valueint < 16) {
                            groups.set(group_item->valueint);
                        }
                    }
                }
                new_assignments[addr] = groups;
            }
            cJSON_Delete(root);

            DaliGroupManagement::getInstance().setAllAssignments(new_assignments);

            httpd_resp_send(req, R"({"status":"ok", "message":"Group assignments saved."})", -1);
            return ESP_OK;
        }

        esp_err_t WebUI::api::DaliSetSceneHandler(httpd_req_t *req) {
            if (checkAuth(req) != ESP_OK) return ESP_FAIL;

            std::vector<char> buf(req->content_len + 1, 0);
            if (httpd_req_recv(req, buf.data(), req->content_len) <= 0) return ESP_FAIL;

            cJSON *root = cJSON_Parse(buf.data());
            if (!cJSON_IsObject(root)) {
                 httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON: root must be an object");
                 cJSON_Delete(root);
                 return ESP_FAIL;
            }

            cJSON* scene_id_item = cJSON_GetObjectItem(root, "scene_id");
            cJSON* levels_item = cJSON_GetObjectItem(root, "levels");

            if (!cJSON_IsNumber(scene_id_item) || !cJSON_IsObject(levels_item)) {
                httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON: 'scene_id' or 'levels' are missing/invalid");
                cJSON_Delete(root);
                return ESP_FAIL;
            }

            uint8_t scene_id = scene_id_item->valueint;
            SceneDeviceLevels levels;
            cJSON* level_item = nullptr;
            cJSON_ArrayForEach(level_item, levels_item) {
                uint8_t addr = std::stoi(level_item->string);
                const uint8_t level = level_item->valueint;
                levels[addr] = level;
            }

            cJSON_Delete(root);

            DaliSceneManagement::getInstance().saveScene(scene_id, levels);

            httpd_resp_send(req, R"({"status":"ok", "message":"Scene configuration saved to devices."})", -1);
            return ESP_OK;
        }

}