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

        // Получить состояние конкретной группы
        [[nodiscard]] DaliGroup getGroupState(uint8_t group_id) const;

        // Обновить состояние группы
        void updateGroupState(uint8_t group_id, const DaliState& state);

        // Восстановить уровень
        void restoreGroupLevel(uint8_t group_id);

        // Публикация текущей конфигурации групп в MQTT
        void publishAllGroups() const;


        /**
        * @brief Относительное изменение уровня
        * @param is_up: true для увеличения, false для уменьшения
        */
        void stepGroupLevel(uint8_t group_id, bool is_up);
    private:
        DaliGroupManagement() = default;

        void loadFromConfig();
        esp_err_t saveToConfig();

        void publishGroupState(uint8_t group_id, uint8_t level,
                                       std::optional<uint16_t> color_temp,
                                       std::optional<DaliRGB> rgb) const;
        void publishDeviceGroupState(DaliLongAddress_t longAddr, const std::bitset<16>& groups) const;

        GroupAssignments m_assignments{};
        std::array<DaliGroup, 16> m_group_states{};
        mutable std::mutex m_mutex{};
    };

} // namespace daliMQTT

#endif //DALIMQTT_DALIGROUPMANAGEMENT_HXX