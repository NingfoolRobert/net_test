/**
 * @file ipc_lock.h
 * @brief Inter-process mutex implemented with an advisory file lock.
 */
#pragma once

#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>

#ifdef _WIN32
#include <Windows.h>
#else
#include <errno.h>
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>
#endif

/**
 * A named, process-shared exclusive lock.
 *
 * The lock file is deliberately kept on disk after destruction.  Its
 * contents are not used; the operating system lock on the open descriptor
 * provides the synchronization.  This avoids the unlink/recreate race that
 * can occur when multiple processes use the same lock name.
 *
 * The class models the BasicLockable and Lockable requirements, so it can be
 * used with std::lock_guard or std::unique_lock. Each process opens its own
 * instance. Threads within a process may share one instance. Move or destroy
 * only when no other thread is using it.
 */
class ipc_lock {
public:
    explicit ipc_lock(const std::string& path)
        : path_(path)
#ifdef _WIN32
        , handle_(INVALID_HANDLE_VALUE)
#else
        , fd_(-1)
#endif
    {
        if (path.empty()) {
            throw std::invalid_argument("ipc_lock path must not be empty");
        }

#ifdef _WIN32
        handle_ = ::CreateFileA(path.c_str(), GENERIC_READ | GENERIC_WRITE,
                                FILE_SHARE_READ | FILE_SHARE_WRITE,
                                NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        if (handle_ == INVALID_HANDLE_VALUE) {
            throw_last_error("CreateFileA");
        }
#else
        fd_ = ::open(path.c_str(), O_RDWR | O_CREAT
#ifdef O_CLOEXEC
                     | O_CLOEXEC
#endif
                     , 0666);
        if (fd_ == -1) {
            throw_errno("open");
        }
#endif
    }

    explicit ipc_lock(const char* path) : ipc_lock(std::string(path ? path : "")) {}

    ~ipc_lock() {
        if (owns_lock()) {
#ifdef _WIN32
            OVERLAPPED overlapped = {};
            ::UnlockFileEx(handle_, 0, MAXDWORD, MAXDWORD, &overlapped);
#else
            ::flock(fd_, LOCK_UN);
#endif
        }
#ifdef _WIN32
        if (handle_ != INVALID_HANDLE_VALUE) {
            ::CloseHandle(handle_);
        }
#else
        if (fd_ != -1) {
            ::close(fd_);
        }
#endif
    }

    ipc_lock(const ipc_lock&) = delete;
    ipc_lock& operator=(const ipc_lock&) = delete;

    ipc_lock(ipc_lock&& other) noexcept
        : path_(std::move(other.path_))
#ifdef _WIN32
        , handle_(other.handle_)
#else
        , fd_(other.fd_)
#endif
        , local_(std::move(other.local_)) {
#ifdef _WIN32
        other.handle_ = INVALID_HANDLE_VALUE;
#else
        other.fd_ = -1;
#endif
    }

    ipc_lock& operator=(ipc_lock&&) = delete;

    void lock() {
        auto& state = local_state();
        std::unique_lock<std::mutex> guard(state.mutex);
        state.available.wait(guard, [&state] { return !state.locked.load(); });
#ifdef _WIN32
        OVERLAPPED overlapped = {};
        if (!::LockFileEx(handle_, LOCKFILE_EXCLUSIVE_LOCK, 0,
                          MAXDWORD, MAXDWORD, &overlapped)) {
            throw_last_error("LockFileEx");
        }
#else
        while (::flock(fd_, LOCK_EX) == -1) {
            if (errno == EINTR) {
                continue;
            }
            throw_errno("flock");
        }
#endif
        state.locked.store(true);
    }

    bool try_lock() {
        auto& state = local_state();
        std::unique_lock<std::mutex> guard(state.mutex, std::try_to_lock);
        if (!guard.owns_lock() || state.locked.load()) {
            return false;
        }
#ifdef _WIN32
        OVERLAPPED overlapped = {};
        if (::LockFileEx(handle_, LOCKFILE_EXCLUSIVE_LOCK | LOCKFILE_FAIL_IMMEDIATELY,
                         0, MAXDWORD, MAXDWORD, &overlapped)) {
            state.locked.store(true);
            return true;
        }
        const DWORD error = ::GetLastError();
        if (error == ERROR_LOCK_VIOLATION || error == ERROR_IO_PENDING) {
            return false;
        }
        throw std::system_error(static_cast<int>(error), std::system_category(),
                                "LockFileEx");
#else
        if (::flock(fd_, LOCK_EX | LOCK_NB) == 0) {
            state.locked.store(true);
            return true;
        }
        if (errno == EWOULDBLOCK || errno == EAGAIN) {
            return false;
        }
        throw_errno("flock");
        return false;
#endif
    }

    void unlock() {
        if (!local_) {
            return;
        }
        std::lock_guard<std::mutex> guard(local_->mutex);
        if (!local_->locked.load()) {
            return;
        }
#ifdef _WIN32
        OVERLAPPED overlapped = {};
        if (!::UnlockFileEx(handle_, 0, MAXDWORD, MAXDWORD, &overlapped)) {
            throw_last_error("UnlockFileEx");
        }
#else
        if (::flock(fd_, LOCK_UN) == -1) {
            throw_errno("flock");
        }
#endif
        local_->locked.store(false);
        local_->available.notify_all();
    }

    // Snapshot of this instance, including ownership by another thread.
    bool owns_lock() const noexcept { return local_ && local_->locked.load(); }
    const std::string& path() const noexcept { return path_; }

private:
    // A file lock on one open descriptor does not serialize its local users.
    // Keep the wait state with the native handle across noexcept moves.
    struct LocalState {
        std::mutex mutex;
        std::condition_variable available;
        std::atomic<bool> locked{false};
    };

    LocalState& local_state() {
        if (!local_) {
            throw std::system_error(std::make_error_code(std::errc::bad_file_descriptor),
                                    "ipc_lock has been moved from");
        }
        return *local_;
    }

#ifndef _WIN32
    static void throw_errno(const char* operation) {
        const int error = errno;
        throw std::system_error(error, std::generic_category(), operation);
    }
#else
    static void throw_last_error(const char* operation) {
        const DWORD error = ::GetLastError();
        throw std::system_error(static_cast<int>(error), std::system_category(), operation);
    }
#endif

    std::string path_;
#ifdef _WIN32
    HANDLE handle_;
#else
    int fd_;
#endif
    std::unique_ptr<LocalState> local_ = std::make_unique<LocalState>();
};
