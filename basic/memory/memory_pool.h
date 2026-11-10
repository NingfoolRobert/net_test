#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <list>
#include <memory>
#include <mutex>
#include <new>
#include <stdexcept>
#include <unordered_map>
#include <utility>
#include <vector>

namespace basic {

// Thread-safe byte allocator. Small allocations share chunks; returned ranges
// are coalesced for reuse. All storage is released on pool destruction.
template <std::size_t ChunkSize = 1 << 21, std::size_t CacheLineSize = 64>
class MemoryPool {
    static_assert(ChunkSize > 0, "ChunkSize must be positive");
    static_assert(CacheLineSize > 0 && (CacheLineSize & (CacheLineSize - 1)) == 0,
                  "CacheLineSize must be a power of two");
    struct Span { std::size_t offset, size; };
    struct Chunk {
        std::unique_ptr<std::byte[]> data{new std::byte[ChunkSize]};
        std::list<Span> free{{0, ChunkSize}};
    };
    struct Allocation {
        Chunk *chunk;
        std::size_t alignment;
        // Preallocate the free-list node so deallocation cannot allocate.
        std::list<Span> span;
        Allocation(Chunk *c, std::size_t a, std::size_t offset, std::size_t size)
            : chunk(c), alignment(a), span{{offset, size}} {}
    };
public:
    MemoryPool() = default;
    MemoryPool(const MemoryPool &) = delete;
    MemoryPool &operator=(const MemoryPool &) = delete;
    ~MemoryPool() {
        for (auto &entry : allocations_)
            if (!entry.second.chunk)
                ::operator delete(entry.first, std::align_val_t(entry.second.alignment));
    }
    void *allocate(std::size_t size, std::size_t alignment = CacheLineSize,
                   std::size_t cacheline_size = CacheLineSize) {
        if (size == 0) return nullptr;
        validate_alignment(alignment);
        validate_alignment(cacheline_size);
        alignment = std::max({alignment, cacheline_size, alignof(std::max_align_t)});
        std::lock_guard<std::mutex> lock(mutex_);
        if (size > ChunkSize || alignment > ChunkSize) return allocate_large(size, alignment);
        for (auto &chunk : chunks_)
            if (void *ptr = allocate_from(*chunk, size, alignment)) return ptr;
        chunks_.push_back(std::make_unique<Chunk>());
        if (void *ptr = allocate_from(*chunks_.back(), size, alignment)) return ptr;
        // The unaligned base may leave insufficient room for a near-full request.
        return allocate_large(size, alignment);
    }
    // Foreign pointers and duplicate returns are rejected. Successful returns
    // null the caller's pointer; the original size need not be supplied.
    void deallocate(void *&ptr) {
        if (!ptr) return;
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = allocations_.find(ptr);
        if (it == allocations_.end()) throw std::invalid_argument("Pointer does not belong to MemoryPool");
        auto &allocation = it->second;
        if (!allocation.chunk) {
            ::operator delete(ptr, std::align_val_t(allocation.alignment));
        } else {
            auto &free = allocation.chunk->free;
            auto next = free.begin();
            while (next != free.end() && next->offset < allocation.span.front().offset) ++next;
            auto returned = allocation.span.begin();
            free.splice(next, allocation.span);
            if (next != free.end() && returned->offset + returned->size == next->offset) {
                returned->size += next->size;
                free.erase(next);
            }
            if (returned != free.begin()) {
                auto previous = std::prev(returned);
                if (previous->offset + previous->size == returned->offset) {
                    previous->size += returned->size;
                    free.erase(returned);
                }
            }
        }
        allocations_.erase(it);
        ptr = nullptr;
    }
private:
    static void validate_alignment(std::size_t alignment) {
        if (!alignment || (alignment & (alignment - 1)))
            throw std::invalid_argument("Alignment must be a power of two");
    }
    void *allocate_large(std::size_t size, std::size_t alignment) {
        void *ptr = ::operator new(size, std::align_val_t(alignment));
        try {
            allocations_.emplace(std::piecewise_construct, std::forward_as_tuple(ptr),
                                 std::forward_as_tuple(nullptr, alignment, 0, size));
        } catch (...) {
            ::operator delete(ptr, std::align_val_t(alignment));
            throw;
        }
        return ptr;
    }
    void *allocate_from(Chunk &chunk, std::size_t size, std::size_t alignment) {
        for (auto it = chunk.free.begin(); it != chunk.free.end(); ++it) {
            auto address = reinterpret_cast<std::uintptr_t>(chunk.data.get() + it->offset);
            const std::size_t padding = (alignment - address % alignment) % alignment;
            if (padding > it->size || size > it->size - padding) continue;
            const std::size_t offset = it->offset + padding;
            void *ptr = chunk.data.get() + offset;
            std::list<Span> replacement;
            if (padding) replacement.push_back({it->offset, padding});
            const auto suffix = it->size - padding - size;
            if (suffix) replacement.push_back({offset + size, suffix});
            allocations_.emplace(std::piecewise_construct, std::forward_as_tuple(ptr),
                                 std::forward_as_tuple(&chunk, alignment, offset, size));
            chunk.free.splice(it, replacement);
            chunk.free.erase(it);
            return ptr;
        }
        return nullptr;
    }
    std::mutex mutex_;
    std::vector<std::unique_ptr<Chunk>> chunks_;
    std::unordered_map<void *, Allocation> allocations_;
};
}  // namespace basic
