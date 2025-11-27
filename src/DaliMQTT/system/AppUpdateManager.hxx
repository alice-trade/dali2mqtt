#ifndef DALIMQTT_APPUPDATEMANAGER_HPP
#define DALIMQTT_APPUPDATEMANAGER_HPP

namespace daliMQTT
{
    class AppUpdateManager {
    public:
        AppUpdateManager(const AppUpdateManager&) = delete;
        AppUpdateManager& operator=(const AppUpdateManager&) = delete;

        static AppUpdateManager& getInstance() {
            static AppUpdateManager instance;
            return instance;
        }

        bool startUpdateAsync(const std::string& url);

        bool isUpdateInProgress() const;

    private:
        AppUpdateManager() = default;

        static void otaTask(void* pvParameter);
        void performUpdate(const std::string& url);

        std::atomic<bool> m_is_updating{false};
    };
} // daliMQTT

#endif //DALIMQTT_APPUPDATEMANAGER_HPP