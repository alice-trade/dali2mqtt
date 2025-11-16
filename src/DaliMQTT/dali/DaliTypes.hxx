#ifndef DALIMQTT_DALITYPES_HXX
#define DALIMQTT_DALITYPES_HXX

namespace daliMQTT
{
    // 24-битный уникальный адрес DALI
    using DaliLongAddress_t = uint32_t;

    // Структура для представления DALI устройства в системе
    struct DaliDevice {
        DaliLongAddress_t long_address;
        uint8_t short_address;
        uint8_t current_level;
        bool is_present; // Флаг, что устройство ответило при последнем сканировании
    };

    // Представление long address в виде строки без аллокаций
    using LongAddrStr = std::array<char, 7>; // 6 hex chars + null

    /**
     * @brief Конвертирует DaliLongAddress_t в его строковое (HEX) представление.
     * @param addr 24-bit DALI long address.
     * @return std::array<char, 7> with HEX string of long addr.
     */
    inline LongAddrStr longAddressToString(const DaliLongAddress_t addr) {
        LongAddrStr result{};
        if (auto [ptr, ec] =
                std::to_chars(result.data(), result.data() + 6, addr & 0xFFFFFF, 16);
                ec == std::errc()) {
            if (const size_t len = ptr - result.data(); len < 6) {
                std::move_backward(result.data(), result.data() + len, result.data() + 6);
                std::fill_n(result.data(), (6 - len), '0');
            }
            for(size_t i = 0; i < 6; ++i) {
                if (result[i] >= 'a' && result[i] <= 'f') {
                    result[i] = result[i] - 'a' + 'A';
                }
            }
        }
        result[6] = '\0';
        return result;
    }

    /**
     * @brief Конвертирует строковое HEX представление в DaliLongAddress.
     * @params string DaliLongAddress.
     * @return Optional DaliLongAddress_t or nullopt.
     */
    inline std::optional<DaliLongAddress_t> stringToLongAddress(std::string_view s) {
        DaliLongAddress_t addr = 0;
        if (s.length() > 6) return std::nullopt;
        if (auto [ptr, ec] = std::from_chars(s.data(), s.data() + s.size(), addr, 16); ec == std::errc() && ptr == s.data() + s.size()) {
            return addr;
        }
        return std::nullopt;
    }

} // daliMQTT

#endif //DALIMQTT_DALITYPES_HXX