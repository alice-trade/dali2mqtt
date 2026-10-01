// Copyright (c) 2026 Alice-Trade Inc.
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef DALIMQTT_JSONARENAALLOCATOR_HXX
#define DALIMQTT_JSONARENAALLOCATOR_HXX

#include "utils/Arena.hxx"
#include <ArduinoJson.h>
#include <cstring>

namespace daliMQTT::memory {
class JsonArenaAllocator final : public ArduinoJson::Allocator {
  public:
    explicit JsonArenaAllocator(Arena& arena) : m_arena(arena) {}

    void* allocate(size_t size) override { return m_arena.allocate(size); }

    void deallocate(void*) override {}

    void* reallocate(void* ptr, size_t new_size) override {
        void* new_ptr = m_arena.allocate(new_size);
        if (new_ptr && ptr) {
            memcpy(new_ptr, ptr, new_size);
        }
        return new_ptr;
    }

  private:
    Arena& m_arena;
};
} // namespace daliMQTT::memory
#endif // DALIMQTT_JSONARENAALLOCATOR_HXX
