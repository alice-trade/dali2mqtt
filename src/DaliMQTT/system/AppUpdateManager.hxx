// Copyright (c) 2026 Alice-Trade Inc.
// SPDX-License-Identifier: GPL-2.0-or-later

#ifndef DALIMQTT_APPUPDATEMANAGER_HPP
#define DALIMQTT_APPUPDATEMANAGER_HPP

namespace daliMQTT
{
    class AppUpdateManager {
    public:
        AppUpdateManager(const AppUpdateManager&) = delete;
        AppUpdateManager& operator=(const AppUpdateManager&) = delete;

        static AppUpdateManager& Instance() {
            static AppUpdateManager instance;
            return instance;
        }

        bool startUpdateAsync(const std::string& url, int type = 0);

        [[nodiscard]] bool isUpdateInProgress() const;

    private:
        struct TaskParams {
            std::string url;
            int type;
        };
        void performSpiffsUpdate(const std::string& url);
        AppUpdateManager() = default;

        static void otaTask(void* pvParameter);
        void performUpdate(const std::string& url);

        std::atomic<bool> m_is_updating{false};
    };
} // daliMQTT

#endif //DALIMQTT_APPUPDATEMANAGER_HPP