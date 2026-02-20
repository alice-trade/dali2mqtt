// Copyright (c) 2026 Alice-Trade Inc.
// SPDX-License-Identifier: GPL-2.0-or-later

#ifndef DALIMQTT_STRINGUTILS_HXX
#define DALIMQTT_STRINGUTILS_HXX

namespace daliMQTT::utils {
    inline std::string stringFormat(const char* fmt, ...) {
        char stack_buf[256];

        va_list args;
        va_start(args, fmt);
        const int len = vsnprintf(stack_buf, sizeof(stack_buf), fmt, args);
        va_end(args);

        if (len < 0) {
            return {};
        }

        if (static_cast<size_t>(len) < sizeof(stack_buf)) {
            return {stack_buf, static_cast<size_t>(len)};
        }

        std::vector<char> dyn_buf(len + 1);
        va_start(args, fmt);
        vsnprintf(dyn_buf.data(), dyn_buf.size(), fmt, args);
        va_end(args);

        return {dyn_buf.data(), static_cast<size_t>(len)};
    }

}

#endif //DALIMQTT_STRINGUTILS_HXX
