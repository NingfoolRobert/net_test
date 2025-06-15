/**
 * @file object.h
 * @brief
 * @author bfning
 * @version 0.1
 * @date 2025-06-14
 */
#pragma once
#include <atomic>

namespace basic {

class Object {
public:
    Object() {
        ref_ = 1;
    }

    ~Object() = default;

    Object(const Object &other) = delete;
    Object(Object &&other) = delete;
    //
    void add_ref() {
        ref_++;
    }
    //
    void release() {
        if (--ref_ == 0) {
            delete this;
        }
    }
private:
    std::atomic<int> ref_;
};
}  // namespace basic
