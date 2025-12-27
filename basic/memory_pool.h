/**
 * @file memory_pool.h
 * @brief Memory pool management utilities.
 * @details This file contains declarations for functions and structures used to manage memory pools in the application.
 * @author bfning
 * @date  2025-11-13
 */

#pragma once

#include <cstddef>

namespace basic {
template <size_t ChunkSize = 1 << 21, size_t CacheLineSize = 64>
class MemoryPool {
public:
    /**
     * @brief Allocates a block of memory from the pool.
     * @param size The size of the memory block to allocate.
     * @return Pointer to the allocated memory block.
     * @note The caller is responsible for ensuring that the allocated memory is properly released back to the pool.
     */
    void *allocate(std::size_t size, size_t alignment = 64, size_t cacheline_size = 64) {
        if (size > ChunkSize) {
            return malloc(size);
        }
        // Expand pool if necessary
        if (nullptr == current_chunk_) {
            expand_pool();
        }
        // Align size
        size = align_up(size, alignment);
        //
        current_chunk_->allocate(size, cacheline_size);
        return nullptr;
    }

    /**
     * @brief Releases a block of memory back to the pool.
     * @param ptr Pointer to the memory block to release.
     * @note The pointer must have been previously allocated from this memory pool.
     */
    void deallocate(void *&ptr) {
        if (nullptr == ptr) {
            return;
        }
        //
        auto chunk_ptr = reinterpret_cast<Chunk *>(reinterpret_cast<char *>(ptr) - offsetof(Chunk, data_));
        chunk_ptr->deallocate(reinterpret_cast<char *>(ptr) - sizeof(Chunk *));
    }

    MemoryPool() {
        expand_pool();
        current_chunk_ = pool_.front();
    }

private:
    /**
     *
     */
    size_t align_up(size_t size, size_t alignment) {
        return (size + alignment - 1) & ~(alignment - 1);
    }

private:
    template <size_t BitSize = ChunkSize / CacheLineSize>
    class Chunk {
    public:
        Chunk() : data_(new char[ChunkSize]), next_(nullptr) {
        }
        ~Chunk() {
            delete[] data_;
        }

    public:
        void *allocate(size_t size, size_t cacheline_size) {
            void *ptr = nullptr;
            if (data_ + size + sizeof(Chunk *) <= end_) {
                ptr = calc_next_cache_line_begin(cacheline_size);
                data_ += size + sizeof(Chunk *);
                auto chunk_ptr = reinterpret_cast<Chunk *>(ptr);
                chunk_ptr = this;
                ptr = reinterpret_cast<void *>(reinterpret_cast<char *>(ptr) + sizeof(Chunk *));
                // add ptr => bit map flag set to allocated
                bitmap_.set((reinterpret_cast<size_t>(ptr) - reinterpret_cast<size_t>(begin_)) / cacheline_size);
            }
            // if no enough memory, return nullptr
            return ptr;
        }

        void deallocate(void *&ptr) {
            // Implementation for deallocating memory within the chunk would go here.
            if (ptr >= begin_ && ptr < end_) {
                // Logic to mark the memory as free would go here.
                // update bitmap
                size_t cacheline_index = (reinterpret_cast<size_t>(ptr) - reinterpret_cast<size_t>(begin_)) / CacheLineSize;
                bitmap_.reset(cacheline_index);
                // move data_ pointer back if possible
                auto current_index = (data_ - begin_) / CacheLineSize;
                if(bitmap_.test(cacheline_index, current_index) == true) {
                    data_ = ptr;
                }
                //
                while(bitmap_.test(current_index -1) == false) {
                    --current_index;
                    data_ -= CacheLineSize;
                }
                // nullify ptr
                ptr = nullptr;
            }
        }

    private:
        // calc the next ptr  begin address
        void *calc_next_cache_line_begin(szie_t cacheline_size) {
            size_t offset = reinterpret_cast<size_t>(data_) % cacheline_size;
            if (offset == 0) {
                return data_;
            }
            size_t adjustment = cacheline_size - offset;
            return data_ + adjustment;
        }

        size_t char *begin_;
        char *end_;
        char *data_;
        Chunk *next_;

        FixedBitSet<BitSize> bitmap_;
    };
    // Disable copy semantics
    Chunk(const Chunk &) = delete;
    Chunk &operator=(const Chunk &) = delete;
    // Move semantics can be implemented if needed.

private:
    // Expand the memory pool by adding a new chunk
    void expand_pool() {
        Chunk *new_chunk = new Chunk();
        pool_.push_back(new_chunk);
        if (current_chunk_) {
            current_chunk_->next_ = new_chunk;
        }
        current_chunk_ = new_chunk;
    }
    // Disable copy semantics
    MemoryPool(const MemoryPool &) = delete;
    MemoryPool &operator=(const MemoryPool &) = delete;
    // Move semantics can be implemented if needed.

private:
    // Internal data structures and methods for managing the memory pool would go here.
    std::vector<Chunk *> pool_;
    Chunk *current_chunk_ = nullptr;
};

}  // namespace basic
