#pragma once
#include <cstddef>
#include <cstring>
#include <cstdint>
#include <type_traits>
#include <memory>
#include <sys/mman.h>
#include <cstdlib>

namespace exchange {

// Lock-free open-addressing hash map with linear probing and backward-shift deletion.
// Zero tombstones, zero degradation over time.
template <typename KeyT, typename ValueT, std::size_t Capacity>
    requires (std::is_integral_v<KeyT> && Capacity > 0 && (Capacity & (Capacity - 1)) == 0)
class FlatHashMap {
    static constexpr KeyT kEmpty = static_cast<KeyT>(0);
    static constexpr std::size_t kMask = Capacity - 1;

    struct Entry {
        KeyT   key{kEmpty};
        ValueT value{};
    };

    Entry* entries_;
    std::size_t size_{0};
    std::size_t max_probe_{0};
    bool mmaped_{false};

public:
    FlatHashMap() {
        std::size_t bytes = Capacity * sizeof(Entry);
        
        // 1. Try synchronous HugePages (requires OS configuration)
        void* ptr = mmap(nullptr, bytes, PROT_READ | PROT_WRITE, 
                         MAP_PRIVATE | MAP_ANONYMOUS | MAP_HUGETLB, -1, 0);
        
        if (ptr != MAP_FAILED) {
            entries_ = static_cast<Entry*>(ptr);
            mmaped_ = true;
        } else {
            // 2. Fallback to Transparent Huge Pages (THP)
            if (posix_memalign(reinterpret_cast<void**>(&entries_), 2 * 1024 * 1024, bytes) != 0) {
                entries_ = static_cast<Entry*>(std::malloc(bytes)); // 3. Absolute fallback
            } else {
                madvise(entries_, bytes, MADV_HUGEPAGE);
            }
        }
        
        // Pre-fault memory to prevent OS page faults on the hot path
        std::memset(static_cast<void*>(entries_), 0, bytes);
        for (std::size_t i = 0; i < Capacity; ++i) {
            entries_[i].key = kEmpty;
        }
    }

    ~FlatHashMap() {
        if (mmaped_) {
            munmap(entries_, Capacity * sizeof(Entry));
        } else {
            std::free(entries_);
        }
    }

    FlatHashMap(const FlatHashMap&) = delete;
    FlatHashMap& operator=(const FlatHashMap&) = delete;

    bool insert(KeyT key, const ValueT& value) noexcept {
        std::size_t idx = hash(key) & kMask;
        std::size_t probe_len = 0;
        while (entries_[idx].key != kEmpty) {
            if (entries_[idx].key == key) {
                entries_[idx].value = value;
                return true;
            }
            idx = (idx + 1) & kMask;
            probe_len++;
        }
        if (probe_len > max_probe_) max_probe_ = probe_len;
        
        entries_[idx].key = key;
        entries_[idx].value = value;
        ++size_;
        return true;
    }

    [[nodiscard]] ValueT* find(KeyT key) noexcept {
        return const_cast<ValueT*>(std::as_const(*this).find(key));
    }

    [[nodiscard]] const ValueT* find(KeyT key) const noexcept {
        std::size_t idx = hash(key) & kMask;
        std::size_t probes = 0;
        while (entries_[idx].key != kEmpty && probes <= max_probe_) {
            if (entries_[idx].key == key) return &entries_[idx].value;
            idx = (idx + 1) & kMask;
            probes++;
        }
        return nullptr;
    }

    [[nodiscard]] bool contains(KeyT key) const noexcept {
        return find(key) != nullptr;
    }

    void prefetch(KeyT key) const noexcept {
        std::size_t idx = hash(key) & kMask;
        __builtin_prefetch(&entries_[idx], 0, 1);
    }

    bool erase(KeyT key) noexcept {
        std::size_t hole = hash(key) & kMask;
        std::size_t probes = 0;
        
        // Find the element to erase
        while (entries_[hole].key != key) {
            if (entries_[hole].key == kEmpty || probes > max_probe_) return false;
            hole = (hole + 1) & kMask;
            probes++;
        }

        // Backward-shift deletion
        while (true) {
            entries_[hole].key = kEmpty;
            entries_[hole].value = ValueT{};
            
            std::size_t next = (hole + 1) & kMask;
            std::size_t shift_probes = 0;
            while (entries_[next].key != kEmpty && shift_probes <= max_probe_) {
                std::size_t ideal = hash(entries_[next].key) & kMask;
                bool move = (hole <= next) ? (ideal <= hole || ideal > next)
                                           : (ideal <= hole && ideal > next);
                if (move) {
                    entries_[hole] = entries_[next];
                    hole = next;
                    break;
                }
                next = (next + 1) & kMask;
                shift_probes++;
            }
            if (entries_[next].key == kEmpty || shift_probes > max_probe_) break;
        }
        --size_;
        return true;
    }

    [[nodiscard]] std::size_t size() const noexcept { return size_; }
    [[nodiscard]] bool empty() const noexcept { return size_ == 0; }
    [[nodiscard]] static constexpr std::size_t capacity() noexcept { return Capacity; }

private:
    [[nodiscard]] static constexpr std::size_t hash(KeyT key) noexcept {
        return static_cast<std::size_t>(key);
    }
};

} // namespace exchange
