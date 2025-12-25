/**
 * @file repeat_limit.h
 * @brief
 * @author bfning
 * @version 0.1
 * @date 2025-12-25
 */
#pragma once
#include <cstddef>
#include <cstdint>

namespace basic {

class RepeatLimit {
public:
    RepeatLimit(size_t bar1, size_t threshold1, size_t bar2, size_t threshold2)
        : counter_(0), bar1_(bar1), bar2_(bar2), threshold1_(threshold1), threshold2_(threshold2) {
    }

    bool repeat() {
        size_t counter = ++counter_;
        if (counter < bar1_) {
            return true;
        }

        if (counter < bar2_) {
            if ((counter - bar1_) % threshold1_ == 0) {
                return true;
            }
            return false;
        }

        if ((counter - bar2_) % threshold2_ == 0) {
            return true;
        }

        return false;
    }

private:
    size_t counter_;
    size_t bar1_;
    size_t bar2_;
    size_t threshold1_;
    size_t threshold2_;
};
}  // namespace basic
