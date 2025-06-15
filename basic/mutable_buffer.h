/**
 * @file mutable_buffer.h
 * @brief
 * @author  bfning
 * @version 0.1
 * @date 2025-02-26
 */
#pragma once
#include <stdio.h>

namespace basic {
struct mutable_buffer_t {
    size_t size;
    char *data;
    void *alloctor;
};
}  // namespace basic
