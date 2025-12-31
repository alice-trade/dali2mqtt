#ifndef DALIMQTT_DALIGROUPMANAGEMENT_HXX
#define DALIMQTT_DALIGROUPMANAGEMENT_HXX
#include "dali/DaliTypes.hxx"

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

        /** Initializes the manager, loads data from NVS. */
        void init();

        /** Gets all group assignments. */
        [[nodiscard]] GroupAssignments getAllAssignments() const;

        /** Gets groups for a specific device. */
        [[nodiscard]] std::optional<std::bitset<16>> getGroupsForDevice(DaliLongAddress_t longAddress) const;

        /** Sets the group membership for a device. */
        esp_err_t setGroupMembership(DaliLongAddress_t longAddress, uint8_t group, bool assigned);

        /** Sets all assignments (e.g. from WebUI). */
        esp_err_t setAllAssignments(const GroupAssignments& newAssignments);

        /** Refreshes group assignments by querying all devices on the bus. */
        esp_err_t refreshAssignmentsFromBus();

        /** Gets the state of a specific group. */
        [[nodiscard]] DaliGroup getGroupState(uint8_t group_id) const;

        /** Updates the state of a group. */
        void updateGroupState(uint8_t group_id, const DaliState& state);

        /** Restores the group level. */
        void restoreGroupLevel(uint8_t group_id);

        /** Publishes the current group configuration to MQTT. */
        void publishAllGroups() const;


        /**
        * @brief Relative level change (Step Up/Down)
        * @param is_up: true to increase, false to decrease
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