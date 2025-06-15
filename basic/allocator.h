/**
 * @file alloctor.h
 * @brief
 * @author bfning
 * @version 0.1
 * @date 2025-06-12
 */
#pragma once
#include <queue>
#include <stdio.h>
#include <stdlib.h>
#include <vector>

namespace basic {
template <typename T, size_t N = 2048>
class Allocator {
public:
    Allocator() {
        auto buffer = malloc(sizeof(T) * N);
        for (auto i = 0u; i < N; ++i) {
            mem_pool_.push(reinterpret_cast<char *>(buffer) + i * sizeof(T));
        }
        buffers_.push_back(buffer);
    }
    ~Allocator() {
        for (auto i = 0u; i < buffers_.size(); ++i) {
            free(buffers_[i]);
        }
        buffers_.clear();
    }

public:
    template <typename Args...>
    T *allocate(const Args &...args) {
        //
        if (mem_pool_.empty()) {
            return nullptr;
        }
        //
        //)      auto ptr = obj_pool_.try_pop();
        auto ptr = mem_pool_.front();
        mem_pool_.pop();
        auto obj = new (ptr) T(args...);
        return obj;
    }

    void deallocate(T *p) {
        if (nullptr == p) {
            return;
        }
        p->~T();
        mem_pool_.push(p);
    }

private:
    std::vector<void *> buffers_;

    std::queue<void *> mem_pool_;
};
}  // namespace basic
