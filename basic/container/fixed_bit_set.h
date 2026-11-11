/**
 * @file fixed_bit_map.h
 * @brief Fixed size bitmap implementation.
 * @details This file contains the declaration of a FixedBitMap class that provides a bitmap of fixed size.
 * @author bfning
 * @date  2025-11-13
 */

#pragma once
#include <cstddef>
#include <cstdint>

namespace detail {
template <size_t N>
class FixedBitSet {
public:
    using Self = FixedBitSet<N>;
    using Ptr = Self *;

    using pos_t = long long;
    const static pos_t npos = static_cast<pos_t>(-1);
    using SizeType = size_t;
    const static size_t cap_ = (N + 7) / 8;

    //
    FixedBitSet() : bits_{0} {
    }
    bool test(size_t index) const {
        if (index >= N)
            return false;
        size_t byte_index = index / 8;
        size_t bit_index = index % 8;
        return (bits_[byte_index] >> bit_index) & 1;
    }
    //
    bool test(size_t begin, size_t end) const {
        if (begin >= N || end > N || begin >= end)
            return false;
        // check all bits in range [begin, end)
        for (size_t i = begin; i < end; ++i) {
            if (!test(i))
                return false;
        }
        return true;
    }
    //
    // Search from offset inclusive.
    pos_t find_first_set(size_t offset = 0) const {
        if (offset >= N) {
            return npos;
        }
        const auto first_byte = offset / 8;
        for (size_t byte = first_byte; byte < cap_; ++byte) {
            if (bits_[byte] == 0) {
                continue;
            }
            const size_t first = byte == first_byte ? offset % 8 : 0;
            for (size_t bit = first; bit < 8; ++bit) {
                const auto pos = byte * 8 + bit;
                if (test(pos)) {
                    return pos;
                }
            }
        }
        return npos;
    }

    // Skip offset bits from the end, then search backwards.
    pos_t find_last_set(size_t offset = 0) const {
        if (offset >= N) {
            return npos;
        }
        const auto last = N - offset - 1;
        const auto last_byte = last / 8;
        for (size_t remaining = last_byte + 1; remaining != 0;) {
            const auto byte = --remaining;
            if (bits_[byte] == 0) {
                continue;
            }
            const size_t count = byte == last_byte ? last % 8 + 1 : 8;
            for (size_t bit = count; bit != 0;) {
                const auto pos = byte * 8 + --bit;
                if (test(pos)) {
                    return pos;
                }
            }
        }
        return npos;
    }

    // Set the bit at the given index to 1
    void set(size_t index) {
        if (index >= N)
            return;
        size_t byte_index = index / 8;
        size_t bit_index = index % 8;
        bits_[byte_index] |= (1 << bit_index);
    }
    // Reset the bit at the given index to 0
    void clear(size_t index) {
        if (index >= N) {
            return;
        }
        size_t byte_index = index / 8;
        size_t bit_index = index % 8;
        bits_[byte_index] &= ~(1 << bit_index);
    }

private:
    uint8_t bits_[cap_];
};

template <size_t N>
using fixed_bit_set_t = FixedBitSet<N>;
}  // namespace detail
