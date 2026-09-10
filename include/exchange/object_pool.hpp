#pragma once
#include <cstddef>
#include <cstring>
#include <cassert>
#include <new>
#include <type_traits>
#include <utility>
#include <memory>

namespace exchange {

// Fixed-size, pre-allocated object pool using a free-list.
// Storage is heap-allocated at construction time - zero allocation thereafter.
// O(1) allocate and deallocate on the hot path.
// Non-copyable, non-movable - owns raw memory.
template <typename T, std::size_t Capacity>
class ObjectPool {
    static_assert(Capacity > 0, "ObjectPool capacity must be > 0");

    union Slot {
        alignas(T) char storage[sizeof(T)];
        Slot* next_free;
    };

    std::unique_ptr<Slot[]> pool_;
    Slot* free_head_;
    std::size_t allocated_{0};

public:
    ObjectPool()
        : pool_(std::make_unique<Slot[]>(Capacity))
        , free_head_(pool_.get()) {
        // Pre-fault memory to prevent OS page faults on the hot path
        std::memset(pool_.get(), 0, Capacity * sizeof(Slot));
        for (std::size_t i = 0; i < Capacity - 1; ++i)
            pool_[i].next_free = &pool_[i + 1];
        pool_[Capacity - 1].next_free = nullptr;
    }

    // Non-copyable, non-movable
    ObjectPool(const ObjectPool&) = delete;
    ObjectPool& operator=(const ObjectPool&) = delete;
    ObjectPool(ObjectPool&&) = delete;
    ObjectPool& operator=(ObjectPool&&) = delete;

    // Allocate a raw slot and construct T in-place
    template <typename... Args>
    [[nodiscard]] T* create(Args&&... args) {
        Slot* slot = allocate_slot();
        if (!slot) return nullptr;
        return new (slot->storage) T{std::forward<Args>(args)...};
    }

    // Destroy T and return slot to pool
    void destroy(T* ptr) noexcept {
        if (!ptr) return;
        ptr->~T();
        auto* slot = reinterpret_cast<Slot*>(ptr);
        slot->next_free = free_head_;
        free_head_ = slot;
        --allocated_;
    }

    [[nodiscard]] std::size_t allocated() const noexcept { return allocated_; }
    [[nodiscard]] std::size_t available() const noexcept { return Capacity - allocated_; }
    [[nodiscard]] static constexpr std::size_t capacity() noexcept { return Capacity; }

private:
    Slot* allocate_slot() noexcept {
        if (!free_head_) return nullptr;
        Slot* slot = free_head_;
        free_head_ = slot->next_free;
        ++allocated_;
        return slot;
    }
};

} // namespace exchange
