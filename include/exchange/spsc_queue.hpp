#pragma once
#include <atomic>
#include <cstddef>
#include <type_traits>

// GCC ≥14 may emit a false-positive -Wstringop-overflow when inlining
// buffer_[tail] = value across aligned atomic members. Suppress it.
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wstringop-overflow"
#endif

namespace exchange {



// Lock-free SPSC ring buffer.
// Capacity must be a power of 2 (enforced at compile time).
// T must be trivially copyable (enforced at compile time).
template <typename T, std::size_t Capacity>
    requires (std::is_trivially_copyable_v<T> &&
              Capacity > 0 &&
              (Capacity & (Capacity - 1)) == 0)
class SPSCQueue {
    static constexpr std::size_t kMask = Capacity - 1;
    static constexpr std::size_t kCacheLineSize = 128; // 128 bytes prevents adjacent-line prefetch false sharing

    // Producer's cache line: write tail_, read/write cached_head_
    alignas(kCacheLineSize) std::atomic<std::size_t> tail_{0};
    std::size_t cached_head_{0}; 

    // Consumer's cache line: write head_, read/write cached_tail_
    alignas(kCacheLineSize) std::atomic<std::size_t> head_{0};
    std::size_t cached_tail_{0}; 

    alignas(kCacheLineSize) T buffer_[Capacity];

public:
    SPSCQueue() = default;

    // Non-copyable, non-movable
    SPSCQueue(const SPSCQueue&) = delete;
    SPSCQueue& operator=(const SPSCQueue&) = delete;

    // Producer: push an element. Returns false if full.
    [[nodiscard]] bool try_push(const T& value) noexcept {
        const auto tail = tail_.load(std::memory_order_relaxed);
        const auto next = (tail + 1) & kMask;
        if (next == cached_head_) {
            cached_head_ = head_.load(std::memory_order_acquire);
            if (next == cached_head_) return false;  // Queue full
        }
        buffer_[tail] = value;
        tail_.store(next, std::memory_order_release);
        return true;
    }

    // Consumer: pop an element. Returns false if empty.
    [[nodiscard]] bool try_pop(T& value) noexcept {
        const auto head = head_.load(std::memory_order_relaxed);
        if (head == cached_tail_) {
            cached_tail_ = tail_.load(std::memory_order_acquire);
            if (head == cached_tail_) return false;  // Queue empty
        }
        value = buffer_[head];
        head_.store((head + 1) & kMask, std::memory_order_release);
        return true;
    }

    [[nodiscard]] bool empty() const noexcept {
        return head_.load(std::memory_order_acquire) ==
               tail_.load(std::memory_order_acquire);
    }

    [[nodiscard]] std::size_t size() const noexcept {
        const auto head = head_.load(std::memory_order_acquire);
        const auto tail = tail_.load(std::memory_order_acquire);
        return (tail - head) & kMask;
    }

    [[nodiscard]] static constexpr std::size_t capacity() noexcept {
        return Capacity - 1;  // One slot reserved to distinguish full from empty
    }
};

} // namespace exchange

#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic pop
#endif
