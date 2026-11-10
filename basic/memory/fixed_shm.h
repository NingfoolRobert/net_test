/**
 * @file fixed_shm.h
 * @brief
 * @author bfning
 * @version  0.1
 * @date 2025-02-16
 */
#pragma once

#include "../util/optimization.h"
#include <algorithm>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <error.h>
#include <fcntl.h>
#include <limits>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

namespace detail {

class fixed_shm {
public:
    fixed_shm(const char *file_path, uint64_t size) : size_(size) {
        if (file_path == nullptr || file_path[0] == '\0') {
            path_error_ = EINVAL;
            return;
        }
        const auto length = strnlen(file_path, sizeof(file_));
        if (length == sizeof(file_)) {
            path_error_ = ENAMETOOLONG;
            return;
        }
        memcpy(file_, file_path, length + 1);
    }

    ~fixed_shm() {
        fsync(size_);
        close();
    }

    fixed_shm(const fixed_shm &rhs) = delete;

    fixed_shm &operator=(const fixed_shm &) = delete;

    fixed_shm(fixed_shm &&rhs) noexcept
        : size_(rhs.size_), data_(rhs.data_), fd_(rhs.fd_), path_error_(rhs.path_error_) {
        strcpy(file_, rhs.file_);
        rhs.data_ = nullptr;
        rhs.fd_   = -1;
    }
    fixed_shm &operator=(fixed_shm &&rhs) noexcept {
        if (this != &rhs) {
            fsync(size_);
            size_ = rhs.size_;
            data_ = rhs.data_;
            fd_   = rhs.fd_;
            path_error_ = rhs.path_error_;
            strcpy(file_, rhs.file_);
            rhs.data_ = nullptr;
            rhs.fd_   = -1;
        }
        return *this;
    }

    bool open() {
        if (UNLIKELY(fd_ != -1)) {
            errno = EBUSY;
            return false;
        }
        if (path_error_ != 0) {
            errno = path_error_;
            return false;
        }
        if (size_ == 0) {
            errno = EINVAL;
            return false;
        }
        if (size_ > static_cast<uint64_t>(std::numeric_limits<off_t>::max()) ||
            size_ > static_cast<uint64_t>(std::numeric_limits<std::ptrdiff_t>::max()) ||
            size_ > static_cast<uint64_t>(std::numeric_limits<std::size_t>::max())) {
            errno = EOVERFLOW;
            return false;
        }
        // Only the exclusive creator may size the file. Existing files,
        // including the queues' pinned /proc/self/fd paths, are never resized.
        fd_ = ::open(file_, O_CREAT | O_EXCL | O_RDWR, 0666);
        const bool created = fd_ >= 0;
        if (fd_ < 0 && errno == EEXIST) {
            fd_ = ::open(file_, O_RDWR);
        }
        if (fd_ < 0) {
            return false;
        }
        if ((created && ::ftruncate(fd_, static_cast<off_t>(size_)) == -1) || !valid_file_size()) {
            const int code = errno;
            ::close(fd_);
            fd_ = -1;
            errno = code;
            return false;
        }

        return true;
    }

    char *mem(uint64_t offset) {
        return data_ + offset;
    }

    void close() {
        unmap();
        if (fd_ != -1) {
            ::close(fd_);
            fd_ = -1;
        }
    }

    char *map() {
        if (fd_ < 0) {
            errno = EBADF;
            return nullptr;
        }
        // Recheck the opened inode immediately before mapping and touching it.
        // The caller must still prevent concurrent truncation while in use.
        if (!valid_file_size()) {
            return nullptr;
        }
        // 将文件映射至进程的地址空间
        if ((data_ = static_cast<char *>(mmap(nullptr, size_, PROT_READ | PROT_WRITE, MAP_SHARED, fd_, 0))) ==
            (void *)-1) {
            data_          = nullptr;
            const int code = errno;
            perror("shm mmap fail");
            errno = code;
            return nullptr;
        }
#ifdef USE_MDADVISE
        // 提前预读数据
        if (madvise(data_, size_, MADV_WILLNEED) == -1) {
            perror("madvise fail");
            return nullptr;
        }
#else
        touch_pages(data_, size_);
#endif
        //
        return data_;
    }

    void unmap() {
        if (data_ != nullptr) {
            if ((munmap((void *)data_, size_)) == -1) {
                perror("shm munmap fail");
                return;
            }
        }
        data_ = nullptr;
    }

    uint64_t capacity() const {
        return size_;
    }

    //
    int writeto(uint64_t offset, const char *data, size_t size) {
        if (UNLIKELY(data_ == nullptr)) {
            return -1;
        }
        //
        if (UNLIKELY(size_ - offset < size)) {
            return -1;
        }
        //
        memmove(data_ + offset, data, size);
        return static_cast<int>(size);
    }
    //
    int readfrom(uint64_t offset, char *data, size_t size) {
        if (UNLIKELY(data_ == nullptr)) {
            errno = EBADF;
            return -1;
        }
        if (offset > size_ || (!data && size != 0)) {
            errno = EINVAL;
            return -1;
        }
        const auto available = size_ - offset;
        const auto count     = std::min<uint64_t>(available, size);
        if (count > static_cast<uint64_t>(std::numeric_limits<int>::max())) {
            errno = EOVERFLOW;
            return -1;
        }
        if (count != 0U) {
            memcpy(data, data_ + offset, count);
        }
        return static_cast<int>(count);
    }

    void fsync(uint64_t offset) {
        if (data_ != nullptr && msync(data_, offset, MS_SYNC) == -1) {
            perror("msync fail. ");
        }
    }

private:
    bool valid_file_size() const {
        struct stat status{};
        if (::fstat(fd_, &status) == -1) {
            return false;
        }
        if (status.st_size < 0 || static_cast<uint64_t>(status.st_size) < size_) {
            errno = EINVAL;
            return false;
        }
        return true;
    }

    void touch_pages(void *addr, uint64_t size) {
        if (data_ == nullptr) {
            return;
        }
        const size_t page_size = sysconf(_SC_PAGESIZE);
        // 触摸所有页面
        for (uint64_t i = 0; i < size_; i += page_size) {
            volatile char c = data_[i];
            (void)c;  // 防止编译器优化掉
        }
    }

private:
    char     file_[256]{};
    uint64_t size_;  // 映射文件大小
    char    *data_{nullptr};
    //
    int fd_{-1};
    int path_error_{0};
};

using shm_t = fixed_shm;
}  // namespace detail
