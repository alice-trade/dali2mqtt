// Copyright (c) 2026 Alice-Trade Inc.
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef DALIMQTT_JSONARENAALLOCATOR_HXX
#define DALIMQTT_JSONARENAALLOCATOR_HXX

#include "utils/Arena.hxx"
#include <ArduinoJson.h>
#include <algorithm>
#include <cstddef>
#include <cstring>

namespace daliMQTT::memory {

class JsonArenaAllocator final : public ArduinoJson::Allocator {
  private:
    struct alignas(std::max_align_t) BlockHeader {
        size_t size{0};
    };

  public:
    explicit JsonArenaAllocator(Arena& arena) : m_arena(arena) {}

    void* allocate(size_t size) override {
        if (size == 0) {
            return nullptr;
        }

        const size_t totalBytes = sizeof(BlockHeader) + size;
        auto* header = static_cast<BlockHeader*>(m_arena.allocate(totalBytes, alignof(BlockHeader)));
        if (!header) {
            return nullptr;
        }

        header->size = size;
        return header + 1;
    }

    void deallocate(void*) override {
    }

    void* reallocate(void* ptr, size_t new_size) override {
        if (!ptr) {
            return allocate(new_size);
        }
        if (new_size == 0) {
            return nullptr;
        }

        auto* old_header = static_cast<BlockHeader*>(ptr) - 1;
        const size_t old_size = old_header->size;

        if (new_size <= old_size) {
            old_header->size = new_size;
            return ptr;
        }

        void* new_ptr = allocate(new_size);
        if (new_ptr) {
            std::memcpy(new_ptr, ptr, old_size);
        }
        return new_ptr;
    }

  private:
    Arena& m_arena;
};

} // namespace daliMQTT::memory
#endif // DALIMQTT_JSONARENAALLOCATOR_HXX