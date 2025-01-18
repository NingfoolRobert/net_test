/**
 * @file fstring.h
 * @brief  string of fixed length
 * @author bfning
 * @version 0.1
 * @date 2025-01-01
 */
#pragma once

#include <memory>
#include <stdexcept>
#include <stdio.h>
#include <string.h>
#include <string>
#include <string_view>

namespace detail {

template <size_t N>
class fstring {
public:
    fstring() {
        data_[0] = 0;
        data_[N - 1] = 0;
    }

    fstring(const char *other) {
        if (other) {
            strncpy(data_, other, N - 1);
        }
        data_[N - 1] = 0;
    }

    fstring(const std::string &other) {
        strncpy(data_, other.c_str(), N - 1);
        data_[N - 1] = 0;
    }

    fstring(const std::string_view &other) {
        strncpy(data_, other.data(), N - 1);
        data_[N - 1] = 0;
    }

    fstring(const char ch, size_t n) {
        for (auto i = 0u; i < n && i < N; ++i) {
            data_[i] = ch;
        }
        data_[n == 0 ? N - 1 : n - 1] = 0;
    }

    fstring(const fstring &rhs) {
        memcpy(data_, rhs.data_, N);
    }
    //
    fstring(fstring &&rhs) {
        memmove(data_, rhs.data_, N);
    }

    fstring &operator=(const char *other) {
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

    bool operator==(const fstring &rhs) {
        return memcmp(data_, rhs.data_, N) == 0;
    }

    bool operator<(const fstring &rhs) {
        return memcmp(data_, rhs.data_, N) < 0;
    }

    bool operator>(const fstring &rhs) {
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
        if (idx >= N) {
            throw std::runtime_error("fstring::operator[] idx invalid(exceed capibility)");
        }
        return data_[idx];
    }

    const char &operator[](size_t idx) const {
        if (idx >= N) {
            throw std::runtime_error("fstring::operator[] idx invalid(exceed capibility)");
        }
        return data_[idx];
    }

    std::string str() {
        return data_;
    }

    size_t capacity() {
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

    fstring &append(const void *data, size_t size) {
        if (data && N) {
            size_t len = length();
            memcpy(data_ + len, data, size <= N - len ? size : N - len);
        }

        return *this;
    }

    char &at(size_t idx) {
        if (idx >= N) {
            throw std::runtime_error("fstring::at, idx invalid: exceed  size");
        }
        return data_[idx];
    }

    const char &at(size_t idx) const {
        if (idx >= N) {
            throw std::runtime_error("fstring::at, idx invalid: exceed  size");
        }
        return data_[idx];
    }

    std::string substr(size_t idx, size_t n) {
        if (idx > N) {
            throw std::runtime_error("fstring::substr, idx invalid(exceed size)");
        }
        return std::string(data_ + idx, idx + n - 1 <= N ? n : N - idx);
    }
    //
    static constexpr auto npos{static_cast<size_t>(-1)};

    size_t find_first_of(const char ch, size_t off = 0) {
        for (auto i = off; i < N; ++i) {
            if (data_[off] == ch) {
                return off;
            }
        }
        return npos;
    }
    
    size_t  find_first_not_of(const char ch, size_t off = 0) {
      for(auto i = off; i < N; ++i) {
        if(data_[off] != ch) {
          return off;
        }
      }
      return npos;
    }

    bool empty() const {
        return data_[0] == 0;
    }

    void swap(fstring &rhs) {
        int64_t *lhs_data = data_;
        int64_t *rhs_data = rhs.data_;
        for (auto i = 0; i < N / 8; ++i) {
            swap(lhs_data[i], rhs_data[i]);
        }
        //
        for (auto i = N / 8 * 8; i < N; ++i) {
            swap(data_[i], rhs.data_[i]);
        }
    }

private:
    char data_[N];
};

//
template <size_t N>
std::ostream &operator<<(std::ostream &out, fstring<N> &rhs) {
    out << rhs.str();
    return out;
}

template <size_t N1, size_t N2>
bool operator==(const fstring<N1> &lhs, const fstring<N2> &rhs) {
    return lhs.length() == rhs.length() && strcmp(lhs.data(), rhs.data()) == 0;
}

template <size_t N>
bool operator==(const std::string &lhs, const fstring<N> &rhs) {
    return lhs.length() == rhs.length() && strcmp(lhs.data(), rhs.data()) == 0;
}

template <size_t N>
bool operator==(const char *lhs, const fstring<N> &rhs) {
    return strcmp(lhs, rhs.c_str()) == 0;
}

}  // namespace detail
