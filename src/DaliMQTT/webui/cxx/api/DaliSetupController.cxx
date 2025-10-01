#include <DaliDeviceController.hxx>
#include <DaliGroupManagement.hxx>
#include <DaliSceneManagement.hxx>
#include "ConfigManager.hxx"
#include "WebUI.hxx"
#include "DaliAPI.hxx"


namespace daliMQTT {
    static constexpr char  TAG[] = "WebUI::Dali";
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

            ESP_LOGI(TAG, "Triggering DALI scan from WebUI.");
            DaliDeviceController::getInstance().performScan();

            httpd_resp_send(req, R"({"status":"ok", "message":"Scan completed."})", -1);
            return ESP_OK;
        }

        esp_err_t WebUI::api::DaliInitializeHandler(httpd_req_t *req) {
            if (checkAuth(req) != ESP_OK) return ESP_FAIL;

            ESP_LOGI(TAG, "Triggering DALI initialization from WebUI.");
            DaliDeviceController::getInstance().performFullInitialization();

            httpd_resp_send(req, R"({"status":"ok", "message":"Initialization completed."})", -1);
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
            cJSON_Delete(root);

            auto& configManager = ConfigManager::getInstance();
            auto current_cfg = configManager.getConfig();
            current_cfg.dali_device_identificators = std::string(buf.data(), ret);
            configManager.setConfig(current_cfg);
            configManager.save();

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