/**
 * @file file_util.h
 * @brief
 * @author bfning
 * @version 0.1
 * @date 2025-04-12
 */
#pragma once

#include "../base/optimization.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>

namespace detail {
//
static FORCE_INLINE const char *path_base_name(const char *file_name) {
    if (nullptr == file_name) {
        return "";
    }
    //
    const char* p = strrchr(file_name, '/');
    return nullptr == p ? file_name : p + 1;
}
//
static FORCE_INLINE std::string path_dir_name(const char *file_name) {
    if (nullptr == file_name) {
        return "";
    }
    //
    const char* p = strrchr(file_name, '/');
    if(nullptr == p) {
      return "";
    }
    // Collapse the separator run at the parent boundary, retaining root.
    while (p > file_name && p[-1] == '/') {
        --p;
    }
    if (p == file_name) {
        return "/";
    }
    return std::string(file_name, static_cast<size_t>(p - file_name));
}
}  // namespace detail
