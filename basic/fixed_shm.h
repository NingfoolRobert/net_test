/**
 * @file fixed_shm.h
 * @brief
 * @author bfning
 * @version  0.1
 * @date 2025-02-16
 */
#pragma once

#include "memory_fence.h"
#include <error.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

namespace detail {

class fixed_shm {
public:
    fixed_shm(const char *file_path, uint64_t size) : size_(size) {
        if (file_path != nullptr) {
            strcpy(file_, file_path);
        }
    }

    ~fixed_shm() {
        close();
    }

    fixed_shm(const fixed_shm &rhs) = delete;
    fixed_shm &operator=(const fixed_shm &) = delete;

    bool open() {
        bool do_fill = false;
        if (access(file_, F_OK) == 0) {  // file exist
            do_fill = true;
        }
        //
        fd_ = ::open(file_, O_CREAT | O_RDWR, 0666);
        if (fd_ < 0) {
            perror("open file faile.");
            return false;
        }

        if (!do_fill) {
            lseek(fd_, size_ - 1, SEEK_SET);
            ::write(fd_, "", 1);
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
        }
    }

    char *map() {
        // 将文件映射至进程的地址空间
        if ((data_ = (char *)mmap(nullptr, size_, PROT_READ | PROT_WRITE, MAP_SHARED, fd_, 0)) == (void *)-1) {
            perror("shm mmap fail");
            return nullptr;
        }
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

private:
    char file_[256];
    uint64_t size_;  // 映射文件大小
    char *data_{nullptr};
    //
    int fd_{-1};
};

using shm_t = fixed_shm;
}  // namespace detail
