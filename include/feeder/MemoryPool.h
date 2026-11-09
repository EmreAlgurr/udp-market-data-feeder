#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>
#include <stdexcept>
#include <new>

namespace feeder {

/**
 * Fixed-Block Preallocated Memory Pool.
 * Eliminates heap allocations during critical market data processing loops.
 */
template <typename T, size_t BlockCount = 65536>
class FixedMemoryPool {
public:
    FixedMemoryPool() {
        m_storage.resize(BlockCount);
        m_freeList.reserve(BlockCount);
        for (size_t i = 0; i < BlockCount; ++i) {
            m_freeList.push_back(reinterpret_cast<T*>(&m_storage[i]));
        }
    }

    ~FixedMemoryPool() = default;

    FixedMemoryPool(const FixedMemoryPool&) = delete;
    FixedMemoryPool& operator=(const FixedMemoryPool&) = delete;

    template <typename... Args>
    T* allocate(Args&&... args) {
        if (m_freeList.empty()) {
            return nullptr; // Pool exhausted (prevents unbounded memory growth)
        }
        T* ptr = m_freeList.back();
        m_freeList.pop_back();
        return new (ptr) T(std::forward<Args>(args)...);
    }

    void deallocate(T* ptr) noexcept {
        if (!ptr) return;
        ptr->~T();
        m_freeList.push_back(ptr);
    }

    [[nodiscard]] size_t available() const noexcept {
        return m_freeList.size();
    }

    [[nodiscard]] size_t capacity() const noexcept {
        return BlockCount;
    }

private:
    union Element {
        T data;
        Element() {}
        ~Element() {}
    };

    std::vector<Element> m_storage;
    std::vector<T*> m_freeList;
};

} // namespace feeder
