#ifndef DALIMQTT_DALIADDRESSMAP_HXX
#define DALIMQTT_DALIADDRESSMAP_HXX

#include "DaliTypes.hxx"

namespace daliMQTT {
    struct AddressMapping {
            DaliLongAddress_t long_address;
            uint8_t short_address;
    };

    class DaliAddressMap {
        public:
            // Загружает карту из NVS.
            static bool load(std::map<DaliLongAddress_t, DaliDevice>& devices, std::map<uint8_t, DaliLongAddress_t>& short_to_long);

            // Сохраняет текущую карту в NVS.
            static esp_err_t save(const std::map<DaliLongAddress_t, DaliDevice>& devices);

        private:
            static constexpr char  NVS_NAMESPACE[] = "dali_state";
            static constexpr char  MAP_KEY[] = "addr_map";
        };
    };

#endif //DALIMQTT_DALIADDRESSMAP_HXX