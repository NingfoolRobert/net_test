/**
 * @file rx.h
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
class rx {
public:
    ~rx() {
        close();
    }

public:
    //
    bool open() {
        return impl()->open();
    }

    int read(char *data, int len) {
        return impl()->read(data, len);
    }

    void close() {
        return impl()->close();
    }

    void set_opt(opt_t opt) {
        opt_ = opt;
    }

    opt_t &get_opt() {
        return opt_;
    }

private:
    drived_t *impl() {
        return static_cast<drived_t *>(this);
    }

protected:
    opt_t opt_;
};
}  // namespace io
}  // namespace detail
