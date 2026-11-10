#pragma once

#include <algorithm>
#include <cstddef>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <new>
#include <stdexcept>
#include <unordered_map>
#include <utility>
#include <vector>

namespace detail {

// Thread-safe size-class allocator, growing each class by Unit_Size aligned
// blocks. Requests above BlockMax are released immediately on deallocation.
template <std::size_t BlockMin = 64, std::size_t BlockMax = 1 << 17,
          std::size_t Unit_Size = 4096 / 64>
class FixedMemoryPool {
    static_assert(BlockMin > 0 && BlockMax >= BlockMin && Unit_Size > 0,
                  "Invalid pool size limits");
    struct Bucket;
    struct Slot {
        void *data;
        std::size_t size, alignment;
        std::size_t requested_size = 0;
        Bucket *bucket;
        bool in_use = false;
        Slot(std::size_t s, std::size_t a, Bucket *b)
            : data(::operator new(s, std::align_val_t(a))), size(s), alignment(a), bucket(b) {}
        ~Slot() { ::operator delete(data, std::align_val_t(alignment)); }
    };
    struct Bucket { std::vector<Slot *> free; std::size_t count = 0; };
public:
    FixedMemoryPool() = default;
    ~FixedMemoryPool() = default;
    FixedMemoryPool(const FixedMemoryPool &) = delete;
    FixedMemoryPool &operator=(const FixedMemoryPool &) = delete;
    char *allocate(std::size_t size, std::size_t alignment = alignof(std::max_align_t)) {
        if (!size) return nullptr;
        if (!alignment || (alignment & (alignment - 1)))
            throw std::invalid_argument("Alignment must be a power of two");
        alignment = std::max(alignment, alignof(std::max_align_t));
        std::lock_guard<std::mutex> lock(mutex_);
        if (size > BlockMax) {
            auto slot = std::make_unique<Slot>(size, alignment, nullptr);
            slot->in_use = true;
            slot->requested_size = size;
            char *ptr = static_cast<char *>(slot->data);
            slots_.emplace(ptr, std::move(slot));
            return ptr;
        }
        if (size > std::numeric_limits<std::size_t>::max() - (BlockMin - 1)) throw std::bad_alloc();
        const auto rounded = ((size + BlockMin - 1) / BlockMin) * BlockMin;
        auto &bucket = buckets_[{rounded, alignment}];
        if (bucket.free.empty()) {
            if (Unit_Size > bucket.free.max_size() || bucket.count > bucket.free.max_size() - Unit_Size)
                throw std::bad_alloc();
            bucket.free.reserve(bucket.count + Unit_Size);
            for (std::size_t i = 0; i < Unit_Size; ++i) {
                auto slot = std::make_unique<Slot>(rounded, alignment, &bucket);
                Slot *raw = slot.get();
                slots_.emplace(raw->data, std::move(slot));
                bucket.free.push_back(raw);
                ++bucket.count;
            }
        }
        Slot *slot = bucket.free.back();
        bucket.free.pop_back();
        slot->in_use = true;
        slot->requested_size = size;
        return static_cast<char *>(slot->data);
    }
    // size is the original requested size, not the rounded class size.
    void deallocate(void *&ptr, std::size_t size) {
        if (!ptr) return;
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = slots_.find(ptr);
        if (it == slots_.end() || !it->second->in_use)
            throw std::invalid_argument("Pointer does not belong to an active allocation");
        Slot &slot = *it->second;
        if (size != slot.requested_size || !size || size > slot.size || (slot.bucket &&
            (size > BlockMax || (size - 1) / BlockMin != (slot.size - 1) / BlockMin)))
            throw std::invalid_argument("Allocation size does not match its size class");
        if (slot.bucket) {
            slot.in_use = false;
            slot.bucket->free.push_back(&slot);
        } else {
            slots_.erase(it);
        }
        ptr = nullptr;
    }
private:
    std::mutex mutex_;
    std::map<std::pair<std::size_t, std::size_t>, Bucket> buckets_;
    std::unordered_map<void *, std::unique_ptr<Slot>> slots_;
};
}  // namespace detail
