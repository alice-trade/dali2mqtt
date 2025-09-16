#ifndef DALIMQTT_DALIGROUPMANAGEMENT_HXX
#define DALIMQTT_DALIGROUPMANAGEMENT_HXX

namespace daliMQTT
{
    using GroupAssignments = std::map<uint8_t, std::bitset<16>>;

    class DaliGroupManagement {
    public:
        DaliGroupManagement(const DaliGroupManagement&) = delete;
        DaliGroupManagement& operator=(const DaliGroupManagement&) = delete;

        static DaliGroupManagement& getInstance() {
            static DaliGroupManagement instance;
            return instance;
        }

        // Инициализация менеджера, загрузка данных из NVS
        void init();

        // Получить все назначения групп
        [[nodiscard]] GroupAssignments getAllAssignments() const;

        // Получить группы для конкретного устройства
        [[nodiscard]] std::optional<std::bitset<16>> getGroupsForDevice(uint8_t shortAddress) const;

        // Установить новое состояние принадлежности устройства к группе
        esp_err_t setGroupMembership(uint8_t shortAddress, uint8_t group, bool assigned);

        // Установить все назначения (например, из WebUI)
        esp_err_t setAllAssignments(const GroupAssignments& newAssignments);

    private:
        DaliGroupManagement() = default;

        void loadFromConfig();
        esp_err_t saveToConfig();

        // Синхронизация одного устройства с шиной DALI
        void syncDeviceToBus(uint8_t shortAddress, std::bitset<16> newGroups, std::bitset<16> oldGroups);

        GroupAssignments m_assignments;
        mutable std::mutex m_mutex;
    };

} // namespace daliMQTT

#endif //DALIMQTT_DALIGROUPMANAGEMENT_HXX