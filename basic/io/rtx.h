/**
 * @file rtx.h
 * @brief
 * @author bfning
 * @version 0.1
 * @date 2025-03-08
 */
#pragma once

#include "io.h"
namespace detail {
namespace io {
template <typename Derived, typename Options>
class rtx : public io<Derived, Options> {
public:
    int recv(char *data, std::size_t len) {
        return this->call_transfer(data, len, [&] { return this->impl()->recv_impl(data, len); });
    }

    int recv_from(char *data, std::size_t len, void *&opt) {
        return this->call_transfer(data, len, [&] { return this->impl()->recv_from_impl(data, len, opt); });
    }

    int send(const void *data, std::size_t len) {
        return this->call_transfer(data, len, [&] { return this->impl()->send_impl(data, len); });
    }

protected:
    rtx()  = default;
    ~rtx() = default;
};
}  // namespace io
}  // namespace detail
