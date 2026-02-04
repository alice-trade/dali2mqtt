// Copyright (c) 2026 Alice-Trade Inc.
// SPDX-License-Identifier: GPL-2.0-or-later

#include "dali/DaliAddressMap.hxx"
#include "utils/NvsHandle.hxx"

namespace daliMQTT
{
    static constexpr char TAG[] = "DaliAddrMapLoader";

    bool DaliAddressMap::load(std::map<DaliLongAddress_t, DaliDevice>& devices, std::map<uint8_t, DaliLongAddress_t>& short_to_long) {
        NvsHandle nvs_handle(NVS_NAMESPACE, NVS_READONLY);
        if (!nvs_handle) {
            ESP_LOGE(TAG, "Failed to open NVS for reading address map.");
            return false;
        }

        size_t required_size = 0;
        esp_err_t err = nvs_get_blob(nvs_handle.get(), MAP_KEY, nullptr, &required_size);

        if (err == ESP_ERR_NVS_NOT_FOUND || required_size == 0) {
            ESP_LOGI(TAG, "No DALI address map found in NVS. A full scan is required.");
            return false;
        }
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Error getting blob size for address map: %s", esp_err_to_name(err));
            return false;
        }
        if (required_size % sizeof(AddressMapping) != 0) {
            ESP_LOGE(TAG, "Invalid blob size for address map. Data might be corrupt.");
            return false;
        }

        std::vector<AddressMapping> mappings(required_size / sizeof(AddressMapping));
        err = nvs_get_blob(nvs_handle.get(), MAP_KEY, mappings.data(), &required_size);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Error reading address map blob: %s", esp_err_to_name(err));
            return false;
        }

        devices.clear();
        short_to_long.clear();

        for (const auto& record : mappings) {
            if (record.is_input_device) {
                InputDevice dev;
                dev.long_address = record.long_address;
                dev.short_address = record.short_address;
                dev.gtin = std::string(record.gtin, strnlen(record.gtin, GTIN_STORAGE_SIZE));
                dev.available = false;
                devices.emplace(record.long_address, dev);
            } else {
                ControlGear dev;
                dev.long_address = record.long_address;
                dev.short_address = record.short_address;
                dev.gtin = std::string(record.gtin, strnlen(record.gtin, GTIN_STORAGE_SIZE));
                if (record.device_type != 0xFF) dev.device_type = record.device_type;
                if (record.supports_rgb || record.supports_tc) {
                    ColorFeatures cf;
                    cf.supports_rgb = record.supports_rgb;
                    cf.supports_tc = record.supports_tc;
                    dev.color = cf;
                }
                dev.min_level = record.min_level;
                dev.max_level = record.max_level;
                dev.power_on_level = record.power_on_level;
                dev.system_failure_level = record.system_failure_restore_level;
                if (dev.device_type.has_value() || !dev.gtin.empty()) {
                    dev.static_data_loaded = true;
                }
                dev.available = false;
                devices.emplace(record.long_address, dev);
            }
            short_to_long[record.short_address] = record.long_address;
        }
        
        ESP_LOGI(TAG, "Successfully loaded %zu DALI address mappings from NVS.", devices.size());
        return true;
    }

    esp_err_t DaliAddressMap::save(const std::map<DaliLongAddress_t, DaliDevice>& devices) {
        NvsHandle nvs_handle(NVS_NAMESPACE, NVS_READWRITE);
        if (!nvs_handle) {
            ESP_LOGE(TAG, "Failed to open NVS for writing address map.");
            return ESP_FAIL;
        }

        std::vector<AddressMapping> mappings;
        mappings.reserve(devices.size());
        for (const auto& device_var : devices | std::views::values) {
            AddressMapping record{};
            const auto& identity = getIdentity(device_var);

            record.long_address = identity.long_address;
            record.short_address = identity.short_address;
            if (!identity.gtin.empty()) {
                strncpy(record.gtin, identity.gtin.c_str(), GTIN_STORAGE_SIZE - 1);
            }

            if (std::holds_alternative<InputDevice>(device_var)) {
                record.is_input_device = true;
                record.device_type = 0xFF;
            } else if (const auto* gear = std::get_if<ControlGear>(&device_var)) {
                record.is_input_device = false;
                record.device_type = gear->device_type.value_or(0xFF);
                if (gear->color.has_value()) {
                    record.supports_rgb = gear->color->supports_rgb;
                    record.supports_tc = gear->color->supports_tc;
                } else {
                    record.supports_rgb = false;
                    record.supports_tc = false;
                }
                record.min_level = gear->min_level;
                record.max_level = gear->max_level;
                record.power_on_level = gear->power_on_level;
                record.system_failure_restore_level = gear->system_failure_level;
            }
            mappings.push_back(record);
        }


        esp_err_t err = nvs_set_blob(nvs_handle.get(), MAP_KEY, mappings.data(), mappings.size() * sizeof(AddressMapping));
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to save address map blob: %s", esp_err_to_name(err));
            return err;
        }
        
        err = nvs_commit(nvs_handle.get());
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to commit NVS after saving address map: %s", esp_err_to_name(err));
            return err;
        }

        ESP_LOGI(TAG, "Successfully saved %zu DALI address mappings to NVS.", mappings.size());
        return ESP_OK;
    }

} // namespace daliMQTT