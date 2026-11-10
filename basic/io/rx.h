/**
 * @file rx.h
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
class rx : public io<Derived, Options> {
public:
    int recv(char *data, std::size_t len) {
        return this->call_transfer(data, len, [&] { return this->impl()->recv_impl(data, len); });
    }

    int recv_from(char *data, std::size_t len, void *&opt) {
        return this->call_transfer(data, len, [&] { return this->impl()->recv_from_impl(data, len, opt); });
    }

protected:
    rx()  = default;
    ~rx() = default;
};
}  // namespace io
}  // namespace detail
