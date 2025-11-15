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

namespace basic {
template <size_t Size = 1024>
class FixedBitMap {
public:
    using Self = FixedBitMap<Size>;
    using Ptr = Self *;

    using pos_t = long long;
    const static pos_t npos = static_cast<pos_t>(-1);
    using SizeType = size_t;
    const static size_t cap_ = (Size + 7) / 8;

    //
    FixedBitMap() : bits_{0} {
    }
    bool test(size_t index) const {
        if (index >= Size)
            return false;
        size_t byte_index = index / 8;
        size_t bit_index = index % 8;
        return (bits_[byte_index] >> bit_index) & 1;
    }
    //
    bool test(size_t begin, size_t end) const {
        if (begin >= Size || end > Size || begin >= end)
            return false;
        // check all bits in range [begin, end)
        for (size_t i = begin; i < end; ++i) {
            if (!test(i))
                return false;
        }
        return true;
    }
    //
    pos_t find_first_set(size_t offset = 0) const {
        if (offset >= Size) {
            return npos;
        }
        //
        auto idx = offset / 8;
        for (auto i = offset % 8; i < 8; ++i) {
            auto pos = idx * 8 + i;
            if (pos >= Size) {
                return npos;
            }
            if (test(pos)) {
                return pos;
            }
        }
        // Iterate through each byte starting from the byte containing the offset
        for (auto idx = offset / 8; idx < (Size + 7) / 8; ++idx) {
            if (bits_[idx] != 0) {
                for (auto bit = 0; bit < 8; ++bit) {
                    auto pos = idx * 8 + bit;
                    if (pos >= Size) {
                        return npos;
                    }
                    //
                    if (test(pos)) {
                        return pos;
                    }
                }
            }
        }
        //
        return npos;
    }
    //
    pos_t find_last_set(size_t offset = 0) const {
        if (offset >= Size) {
            return npos;
        }
        //
        auto idx = (Size - offset) % 8;
        if (bits_[(Size - offset) / 8] != 0) {
            for (auto bit = idx - 1; bit >= 0; --bit) {
                auto pos = (Size - offset) / 8 * 8 + bit;
                if (test(pos)) {
                    return pos;
                }
            }
        }
        //
        for (int i = (Size - offset) / 8; i >= 0; --i) {
            if (bits_[i] != 0) {
                for (auto bit = 7; bit >= 0; --bit) {
                    auto pos = i * 8 + bit;
                    //
                    if (test(pos)) {
                        return pos;
                    }
                }
            }
        }
        //
        return npos;
    }

    // Set the bit at the given index to 1
    void set(size_t index) {
        if (index >= Size)
            return;
        size_t byte_index = index / 8;
        size_t bit_index = index % 8;
        bits_[byte_index] |= (1 << bit_index);
    }
    // Reset the bit at the given index to 0
    void clear(size_t index) {
        if (index >= Size)
            return;
        size_t byte_index = index / 8;
        size_t bit_index = index % 8;
        bits_[byte_index] &= ~(1 << bit_index);
    }

private:
    uint8_t bits_[cap_];
};
}  // namespace basic