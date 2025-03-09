/**
 * @file rtx.h
 * @brief
 * @author bfning
 * @version 0.1
 * @date 2025-03-08
 */
#pragma once

namespace detail {
namespace io {
template <typename drived_t, typename opt_t>
class rtx {
public:
    ~rtx() {
        close();
    }

    bool open() {
        return subtype()->open();
    }

    void close() {
        subtype()->close();
    }

    int read(char *data, size_t len) {
        return subtype()->read(data, len);
    }

    int write(const void *data, size_t len);

    void set_opt(opt_t &opt) {
        opt_ = opt;
    }

    opt_t &get_opt() {
        return opt_;
    }

private:
    drived_t *subtype() {
        return static_cast<drived_t *>(this);
    }

protected:
    opt_t opt_;
};
}  // namespace io
}  // namespace detail
