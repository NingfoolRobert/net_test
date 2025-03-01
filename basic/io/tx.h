/**
 * @file tx.h
 * @brief
 * @author bfning
 * @version 0.1
 * @date 2025-02-24
 */
#pragma once
#include <stdio.h>
namespace detail {

namespace io {
template <typename drived_t, typename opt_t>
class tx {
public:
    bool open() {
        return impl()->open();
    }

    void close() {
        return impl()->close();
    }

    int write(const void *data, int len) {
        return impl()->write(data, len);
    }

private:
    drived_t *impl() {
        return static_cast<drived_t *>(this);
    }

private:
    opt_t opt_;
};
}  // namespace io
}  // namespace detail
