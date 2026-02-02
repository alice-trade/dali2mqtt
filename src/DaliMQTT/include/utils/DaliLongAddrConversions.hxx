#ifndef DALIMQTT_DALILONGADDRCONVERSIONS_HXX
#define DALIMQTT_DALILONGADDRCONVERSIONS_HXX
#include "dali/DaliСommon.hxx"

// Utility functions
namespace daliMQTT::utils {
    /**
     * @brief Convert DaliLongAddress_t to HEX string
     * @param addr 24-bit DALI long address.
     * @return std::array<char, 7> with HEX string of long addr.
     */
    inline DaliLongAddrStr longAddressToString(const DaliLongAddress_t addr) {
        DaliLongAddrStr result{};
        if (auto [ptr, ec] =
                std::to_chars(result.data(), result.data() + 6, addr & 0xFFFFFF, 16);
                ec == std::errc()) {
            if (const size_t len = ptr - result.data(); len < 6) {
                std::move_backward(result.data(), result.data() + len, result.data() + 6);
                std::fill_n(result.data(), (6 - len), '0');
            }
            for(size_t i = 0; i < 6; ++i) {
                if (result[i] >= 'a' && result[i] <= 'f') {
                    result[i] -= ('a' - 'A');
                }
            }
                }
        result[6] = '\0';
        return result;
    }

    /**
     * @brief Convert HEX long addr string to DaliLongAddress_t.
     * @return Optional DaliLongAddress_t or nullopt.
     */
    inline std::optional<DaliLongAddress_t> stringToLongAddress(const std::string_view s) {
        DaliLongAddress_t addr = 0;
        if (s.length() > 6) return std::nullopt;
        if (auto [ptr, ec] = std::from_chars(s.data(), s.data() + s.size(), addr, 16); ec == std::errc() && ptr == s.data() + s.size()) {
            return addr;
        }
        return std::nullopt;
    }
}
#endif //DALIMQTT_DALILONGADDRCONVERSIONS_HXX