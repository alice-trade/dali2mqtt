#ifndef DALIMQTT_DALIGROUPMANAGEMENT_HXX
#define DALIMQTT_DALIGROUPMANAGEMENT_HXX
#include "DaliTypes.hxx"

namespace daliMQTT
{
    using GroupAssignments = std::map<DaliLongAddress_t, std::bitset<16>>;

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
        [[nodiscard]] std::optional<std::bitset<16>> getGroupsForDevice(DaliLongAddress_t longAddress) const;

        // Установить новое состояние принадлежности устройства к группе
        esp_err_t setGroupMembership(DaliLongAddress_t longAddress, uint8_t group, bool assigned);

        // Установить все назначения (например, из WebUI)
        esp_err_t setAllAssignments(const GroupAssignments& newAssignments);

        // Обновить назначения групп, опросив все устройства на шине
        esp_err_t refreshAssignmentsFromBus();
    private:
        DaliGroupManagement() = default;

        void loadFromConfig();
        esp_err_t saveToConfig();

        GroupAssignments m_assignments;
        mutable std::mutex m_mutex;
    };

} // namespace daliMQTT

#endif //DALIMQTT_DALIGROUPMANAGEMENT_HXX