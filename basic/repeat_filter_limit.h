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

/** @brief A class to control repetition behavior based on thresholds and bars.
 * This class helps in determining whether an operation should be repeated
 * based on the number of times it has been invoked.
 */
class RepeatLimit {
public:
    RepeatLimit(size_t bar1, size_t threshold1, size_t bar2 = 50 * 1000, size_t threshold2 = 100 * 1000)
        : counter_(0), bar1_(bar1), bar2_(bar2), threshold1_(threshold1), threshold2_(threshold2) {
    }
    /**
     * @brief Determines whether to repeat based on the configured limits.
     * @return true if the operation should be repeated; false otherwise.
     */
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
    /**
     * @brief Retrieves the current count of repetitions.
     * @return The current repetition count.
     */
    size_t counter() const {
        return counter_;
    }

private:
    size_t counter_;
    size_t bar1_;
    size_t bar2_;
    size_t threshold1_;
    size_t threshold2_;
};
}  // namespace basic
