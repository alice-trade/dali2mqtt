// Copyright (c) 2026 Alice-Trade Inc.
// SPDX-License-Identifier: GPL-2.0-or-later

#ifndef DALIMQTT_FILEHANDLER_HXX
#define DALIMQTT_FILEHANDLER_HXX
#include <cstdio>

// RAII wrapper for FILE handle
class FileHandle {
public:
    explicit FileHandle(const char* path, const char* mode) {
        m_file = fopen(path, mode);
    }

    ~FileHandle() {
        if (m_file) {
            fclose(m_file);
        }
    }

    [[nodiscard]] FILE* get() const { return m_file; }
    explicit operator bool() const { return m_file != nullptr; }

    FileHandle(const FileHandle&) = delete;
    FileHandle& operator=(const FileHandle&) = delete;
private:
    FILE* m_file{nullptr};
};
#endif //DALIMQTT_FILEHANDLER_HXX