/**
 * @file tx.h
 * @brief
 * @author bfning
 * @version 0.1
 * @date 2025-02-24
 */
#pragma once
#include "io.h"
namespace detail {

namespace io {
template <typename Derived, typename Options>
class tx : public io<Derived, Options> {
public:
    int send(const void *data, std::size_t len) {
        return this->call_transfer(data, len, [&] { return this->impl()->send_impl(data, len); });
    }

protected:
    tx()  = default;
    ~tx() = default;
};
}  // namespace io
}  // namespace detail