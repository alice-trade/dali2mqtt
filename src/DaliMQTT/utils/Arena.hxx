// Copyright (c) 2026 Alice-Trade Inc.
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef DALIMQTT_ARENA_HXX
#define DALIMQTT_ARENA_HXX

#include <cstddef>
#include <cstdint>
#include <esp_heap_caps.h>
#include <new>
#include <type_traits>
#include <utility>

namespace daliMQTT::memory {

class Arena {
  public:
    explicit Arena(size_t capacity)
        : m_capacity(capacity),
          m_buffer(static_cast<uint8_t*>(heap_caps_malloc(capacity, MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL))),
          m_offset(0), m_ownsBuffer(true) {}

    Arena(uint8_t* externalBuffer, size_t capacity) noexcept
        : m_capacity(capacity), m_buffer(externalBuffer), m_offset(0), m_ownsBuffer(false) {}

    ~Arena() {
        if (m_ownsBuffer && m_buffer) {
            heap_caps_free(m_buffer);
        }
    }

    Arena(const Arena&) = delete;
    Arena& operator=(const Arena&) = delete;

    Arena(Arena&& other) noexcept
        : m_capacity(other.m_capacity), m_buffer(other.m_buffer), m_offset(other.m_offset),
          m_ownsBuffer(other.m_ownsBuffer) {
        other.m_buffer = nullptr;
        other.m_offset = 0;
        other.m_ownsBuffer = false;
    }

    Arena& operator=(Arena&& other) noexcept {
        if (this != &other) {
            if (m_ownsBuffer && m_buffer) {
                heap_caps_free(m_buffer);
            }
            m_capacity = other.m_capacity;
            m_buffer = other.m_buffer;
            m_offset = other.m_offset;
            m_ownsBuffer = other.m_ownsBuffer;

            other.m_buffer = nullptr;
            other.m_offset = 0;
            other.m_ownsBuffer = false;
        }
        return *this;
    }

    [[nodiscard]] bool isValid() const noexcept { return m_buffer != nullptr; }

    void* allocate(size_t size, size_t align = alignof(std::max_align_t)) noexcept {
        if (!m_buffer || size == 0)
            return nullptr;

        const uintptr_t current = reinterpret_cast<uintptr_t>(m_buffer + m_offset);
        const uintptr_t aligned = (current + (align - 1)) & ~(align - 1);
        const size_t padding = aligned - current;

        if (padding + size > m_capacity - m_offset) {
            return nullptr;
        }

        m_offset += padding;
        void* ptr = m_buffer + m_offset;
        m_offset += size;
        return ptr;
    }

    template <typename T, typename... Args> T* create(Args&&... args) {
        static_assert(std::is_trivially_destructible_v<T>, "Arena::create only supports trivially destructible types");

        void* ptr = allocate(sizeof(T), alignof(T));
        if (!ptr)
            return nullptr;
        return ::new (ptr) T(std::forward<Args>(args)...);
    }

    void reset() noexcept { m_offset = 0; }

    [[nodiscard]] size_t used() const noexcept { return m_offset; }
    [[nodiscard]] size_t capacity() const noexcept { return m_capacity; }

  private:
    size_t m_capacity{0};
    uint8_t* m_buffer{nullptr};
    size_t m_offset{0};
    bool m_ownsBuffer{false};
};

template <size_t Capacity> class StaticArena : public Arena {
  public:
    StaticArena() noexcept : Arena(m_storage, Capacity) {}

  private:
    alignas(std::max_align_t) uint8_t m_storage[Capacity]{};
};

} // namespace daliMQTT::memory

#endif // DALIMQTT_ARENA_HXX