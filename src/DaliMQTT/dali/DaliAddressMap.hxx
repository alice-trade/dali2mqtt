// Copyright (c) 2026 Alice-Trade Inc.
// SPDX-License-Identifier: GPL-2.0-or-later

#ifndef DALIMQTT_DALIADDRESSMAP_HXX
#define DALIMQTT_DALIADDRESSMAP_HXX

#include "dali/DaliСommon.hxx"

namespace daliMQTT {
    constexpr size_t GTIN_STORAGE_SIZE = 16;
    struct AddressMapping {
            DaliLongAddress_t long_address;
            uint8_t short_address;
            uint8_t device_type;
            char gtin[GTIN_STORAGE_SIZE];
            bool is_input_device;
            bool supports_rgb;
            uint8_t min_level;
            uint8_t max_level;
            uint8_t power_on_level;
            uint8_t system_failure_restore_level;
            bool supports_tc;
            uint8_t _padding;
            uint8_t _padding_2;
    };

    class DaliAddressMap {
        public:
            /** Loads the map from NVS. */
            static bool load(std::map<DaliLongAddress_t, DaliDevice>& devices, std::map<uint8_t, DaliLongAddress_t>& short_to_long);

            /** Saves the current map to NVS. */
            static esp_err_t save(const std::map<DaliLongAddress_t, DaliDevice>& devices);

        private:
            static constexpr char  NVS_NAMESPACE[] = "dali_state";
            static constexpr char  MAP_KEY[] = "DALIAddrMap";
        };
    };

#endif //DALIMQTT_DALIADDRESSMAP_HXX