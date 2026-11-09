#pragma once

#include <atomic>
#include <cstddef>
#include <new>
#include <type_traits>
#include <utility>
#include <memory>
#include <cstdlib>

namespace feeder {

/**
 * High-Performance Lock-Free Single-Producer Single-Consumer (SPSC) Ring Buffer.
 * - Cache-line aligned (64 bytes) to eliminate false sharing.
 * - Power-of-two capacity for O(1) bitwise modular indexing.
 * - Heap-allocated aligned buffer (safe from thread stack limits).
 * - Acquire/Release memory ordering for zero-overhead lockless synchronization.
 */
template <typename T, size_t Capacity = 65536>
class SPSCRingBuffer {
    static_assert((Capacity & (Capacity - 1)) == 0, "Capacity must be a power of two");

public:
    SPSCRingBuffer() : m_head(0), m_tail(0) {
#if defined(_WIN32)
        m_buffer = static_cast<T*>(_aligned_malloc(sizeof(T) * Capacity, 64));
#else
        m_buffer = static_cast<T*>(std::aligned_alloc(64, sizeof(T) * Capacity));
#endif
        if (!m_buffer) {
            throw std::bad_alloc();
        }
    }

    ~SPSCRingBuffer() {
        if (m_buffer) {
#if defined(_WIN32)
            _aligned_free(m_buffer);
#else
            std::free(m_buffer);
#endif
        }
    }

    SPSCRingBuffer(const SPSCRingBuffer&) = delete;
    SPSCRingBuffer& operator=(const SPSCRingBuffer&) = delete;

    template <typename... Args>
    bool emplace(Args&&... args) noexcept {
        const size_t currentTail = m_tail.load(std::memory_order_relaxed);
        const size_t currentHead = m_head.load(std::memory_order_acquire);

        if (currentTail - currentHead >= Capacity) {
            return false; // Buffer Full
        }

        new (&m_buffer[currentTail & Mask]) T(std::forward<Args>(args)...);
        m_tail.store(currentTail + 1, std::memory_order_release);
        return true;
    }

    bool push(const T& item) noexcept {
        return emplace(item);
    }

    bool push(T&& item) noexcept {
        return emplace(std::move(item));
    }

    bool pop(T& outItem) noexcept {
        const size_t currentHead = m_head.load(std::memory_order_relaxed);
        const size_t currentTail = m_tail.load(std::memory_order_acquire);

        if (currentHead == currentTail) {
            return false; // Buffer Empty
        }

        outItem = std::move(m_buffer[currentHead & Mask]);
        m_head.store(currentHead + 1, std::memory_order_release);
        return true;
    }

    [[nodiscard]] size_t size() const noexcept {
        const size_t head = m_head.load(std::memory_order_relaxed);
        const size_t tail = m_tail.load(std::memory_order_relaxed);
        return (tail >= head) ? (tail - head) : 0;
    }

    [[nodiscard]] bool empty() const noexcept {
        return m_head.load(std::memory_order_relaxed) == m_tail.load(std::memory_order_relaxed);
    }

    [[nodiscard]] constexpr size_t capacity() const noexcept {
        return Capacity;
    }

private:
    static constexpr size_t Mask = Capacity - 1;

    // Cacheline 1: Producer State
    alignas(64) std::atomic<size_t> m_tail;

    // Cacheline 2: Consumer State
    alignas(64) std::atomic<size_t> m_head;

    // Cacheline 3: Aligned buffer pointer
    alignas(64) T* m_buffer{nullptr};
};

} // namespace feeder
