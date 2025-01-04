/**
 * @file fstring.h
 * @brief  string of fixed length 
 * @author bfning
 * @version 0.1
 * @date 2025-01-01
 */
#pragma once

#include <memory>
#include <stdio.h>
#include <string.h>
#include <string>
#include <string_view>

namespace detail {

template <size_t N>
class fstring_t {
public:
    fstring_t() {
        memset(data_, 0, N);
    }

    fstring_t(const char *other) {
        if (other) {
            strncpy(data_, other, N - 1);
        }
        data_[N - 1] = 0;
    }

    fstring_t(const std::string &other) {
        strncpy(data_, other.c_str(), N - 1);
        data_[N - 1] = 0;
    }

    fstring_t(const std::string_view &other) {
        strncpy(data_, other.data(), N - 1);
        data_[N - 1] = 0;
    }

    fstring_t(const char ch, size_t n) {
        for (auto i = 0u; i < n && i < N; ++i) {
            data_[i] = ch;
        }
        data_[N - 1] = 0;
    }

    fstring_t(const fstring_t &rhs) {
        memcpy(data_, rhs.data_, N);
    }
    //
    fstring_t(fstring_t &&rhs) {
        memmove(data_, rhs.data_, N);
    }

    fstring_t &operator=(const char *other) {
        if (other) {
            strncpy(data_, other, N - 1);
        }
        return *this;
    }

    bool operator==(const char *other) {
        if (nullptr == other) {
            return false;
        }
        return strcmp(data_, other) == 0;
    }
    //
    bool operator<(const char *other) {
        if (nullptr == other) {
            return true;
        }
        return strcmp(data_, other) < 0;
    }
    //
    bool operator>(const char *other) {
        if (nullptr == other) {
            return false;
        }
        return strcmp(data_, other) > 0;
    }

    bool operator==(const fstring_t &rhs) {
        return memcmp(data_, rhs.data_, N) == 0;
    }

    bool operator<(const fstring_t &rhs) {
        return memcmp(data_, rhs.data_, N) < 0;
    }

    bool operator>(const fstring_t &rhs) {
        return memcmp(data_, rhs.data_, N);
    }

    bool operator==(const std::string &rhs) {
        if (rhs.length() != N) {
            return false;
        }
        return strncmp(data_, rhs.data(), N) == 0;
    }

    bool operator<(const std::string &rhs) {
        return strncmp(data_, rhs.c_str(), N) < 0;
    }

    bool operator>(const std::string &rhs) {
        return strncmp(data_, rhs.c_str(), N) > 0;
    }

    char &operator[](size_t idx) {
        return data_[idx];
    }

    std::string to_string() {
        return data_;
    }

    size_t cap() {
        return N;
    }
    //
    size_t length() {
        return strlen(data_);
    }

    char *data() {
        return data_;
    }

    const char *data() const {
        return data_;
    }

    const char *c_str() {
        return data_;
    }

    fstring_t &append(const void *data, size_t size) {
        if (data && N) {
            size_t len = length();
            memcpy(data_ + len, data, size <= N - len ? size : N - len);
        }
        return *this;
    }

    char at(size_t idx) {
        if (idx >= N) {
            return {};
        }
        return data_[idx];
    }

    std::string substr(size_t idx, size_t n) {
        if (idx > N) {
            return {};
        }
        return std::string(data_ + idx, idx + n - 1 <= N ? n : N - idx);
    }

private:
    char data_[N];
};

//
template<size_t N>
std::ostream &operator<<(std::ostream &out, fstring_t<N> &rhs) {
    out << rhs.to_string();
    return out;
}
}  // namespace detail
