#include "DaliAddressMap.hxx"
#include "utils/NvsHandle.hxx"
#include <esp_log.h>

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
            DaliDevice dev;
            dev.long_address = record.long_address;
            dev.short_address = record.short_address;

            if (record.device_type != 0xFF) {
                dev.device_type = record.device_type;
            }

            dev.gtin = std::string(record.gtin, strnlen(record.gtin, GTIN_STORAGE_SIZE));
            dev.is_input_device = record.is_input_device;

            dev.supports_rgb = record.supports_rgb;
            dev.supports_tc = record.supports_tc;

            if (dev.device_type.has_value() || !dev.gtin.empty()) {
                dev.static_data_loaded = true;
            }

            devices[record.long_address] = dev;
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
        for (const auto& [long_addr, device] : devices) {
            AddressMapping record{};
            record.long_address = long_addr;
            record.short_address = device.short_address;
            record.device_type = device.device_type.value_or(0xFF);
            record.supports_rgb = device.supports_rgb;
            record.supports_tc = device.supports_tc;
            record.is_input_device = device.is_input_device;
            record._padding = 0;
            if (!device.gtin.empty()) {
                strncpy(record.gtin, device.gtin.c_str(), GTIN_STORAGE_SIZE - 1);
                record.gtin[GTIN_STORAGE_SIZE - 1] = '\0';
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