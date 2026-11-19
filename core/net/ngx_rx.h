/**
 * @file ngx_rx.h
 * @brief
 * @author bfning
 * @version 0.1
 * @date 2025-02-20
 */
#pragma once

#include <stdio.h>
#include <stdlib.h>

template <typename T>
class ngx_rx {
public:
    bool open() {
        return subtype()->open();
    }

    void close() {
        subtype()->close();
    }

    template <typename F>
    int read(F &&f) {
        static_assert(std::is_same_v<f, [](char *, int) -> int { return 0 }>, "");
        return subtype()->read(std::forward<F>(f));
    }

private:
    T *subtype() {
        return this;
    }
};
