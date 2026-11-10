#pragma once

#include <cstddef>
#include <algorithm>
#include <memory>
#include <new>
#include <stdexcept>
#include <unordered_map>
#include <utility>
#include <vector>

namespace basic {

// Single-threaded pool. Acquire constructs a fresh object in reusable storage;
// release destroys it. Destruction also destroys outstanding objects.
template <typename T>
class ObjectPool {
    struct Slot {
        alignas(T) unsigned char storage[sizeof(T)];
        bool live = false;
        T *object() noexcept { return std::launder(reinterpret_cast<T *>(storage)); }
        ~Slot() { if (live) object()->~T(); }
    };
public:
    ObjectPool() = default;
    ObjectPool(const ObjectPool &) = delete;
    ObjectPool &operator=(const ObjectPool &) = delete;
    template <typename... Args>
    T *acquire(Args &&...args) {
        if (free_.empty()) {
            if (free_.capacity() <= slots_.size()) {
                const auto required = slots_.size() + 1;
                const auto grown = free_.capacity() > free_.max_size() / 2 ?
                    free_.max_size() : std::max<std::size_t>(4, free_.capacity() * 2);
                free_.reserve(std::max(required, grown));
            }
            auto slot = std::make_unique<Slot>();
            Slot *raw = slot.get();
            auto key = reinterpret_cast<T *>(raw->storage);
            slots_.emplace(key, std::move(slot));
            free_.push_back(raw);
        }
        Slot *slot = free_.back();
        T *object = new (slot->storage) T(std::forward<Args>(args)...);
        slot->live = true;
        free_.pop_back();
        return object;
    }
    void release(T *&ptr) {
        if (!ptr) return;
        auto it = slots_.find(ptr);
        if (it == slots_.end() || !it->second->live)
            throw std::invalid_argument("Object is not an active member of this pool");
        Slot *slot = it->second.get();
        slot->object()->~T();
        slot->live = false;
        free_.push_back(slot);
        ptr = nullptr;
    }
    std::size_t capacity() const noexcept { return slots_.size(); }
    std::size_t available() const noexcept { return free_.size(); }
    std::size_t in_use() const noexcept { return capacity() - available(); }
private:
    std::unordered_map<T *, std::unique_ptr<Slot>> slots_;
    std::vector<Slot *> free_;
};
}  // namespace basic
