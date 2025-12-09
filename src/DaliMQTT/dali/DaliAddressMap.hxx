#ifndef DALIMQTT_DALIADDRESSMAP_HXX
#define DALIMQTT_DALIADDRESSMAP_HXX

#include "DaliTypes.hxx"

namespace daliMQTT {
    constexpr size_t GTIN_STORAGE_SIZE = 16;
    struct AddressMapping {
            DaliLongAddress_t long_address;
            uint8_t short_address;
            uint8_t device_type;
            char gtin[GTIN_STORAGE_SIZE];
            bool is_input_device;
            bool supports_rgb;
            bool supports_tc;
            uint8_t _padding;
    };

    class DaliAddressMap {
        public:
            // Загружает карту из NVS.
            static bool load(std::map<DaliLongAddress_t, DaliDevice>& devices, std::map<uint8_t, DaliLongAddress_t>& short_to_long);

            // Сохраняет текущую карту в NVS.
            static esp_err_t save(const std::map<DaliLongAddress_t, DaliDevice>& devices);

        private:
            static constexpr char  NVS_NAMESPACE[] = "dali_state";
            static constexpr char  MAP_KEY[] = "DALIAddrMap";
        };
    };

#endif //DALIMQTT_DALIADDRESSMAP_HXX