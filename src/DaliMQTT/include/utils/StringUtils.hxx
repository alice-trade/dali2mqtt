#ifndef DALIMQTT_STRINGUTILS_HXX
#define DALIMQTT_STRINGUTILS_HXX

namespace daliMQTT::utils {
    inline std::string stringFormat(const char* fmt, ...) {
        va_list args;
        va_start(args, fmt);

        va_list args_copy;
        va_copy(args_copy, args);
        const int len = vsnprintf(nullptr, 0, fmt, args_copy);
        va_end(args_copy);

        if (len < 0) {
            va_end(args);
            return {};
        }

        std::vector<char> buf(len + 1);
        vsnprintf(buf.data(), len + 1, fmt, args);
        va_end(args);

        return {buf.data(), static_cast<size_t>(len)};
    }
}

#endif //DALIMQTT_STRINGUTILS_HXX
