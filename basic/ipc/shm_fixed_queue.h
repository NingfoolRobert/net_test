/** @file shm_fixed_queue.h
 * Linux SPSC fixed-size message queue backed by the original detail::shm_t.
 */
#pragma once

#include "fixed_shm.h"

#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <exception>
#include <limits>
#include <memory>
#include <string>
#include <system_error>
#include <thread>
#include <type_traits>
#include <utility>

namespace basic {

// Exactly one producer and one consumer per file, each on one thread. Startup
// uses a file lock; sendto/recvfrom use only shared-memory atomics and copies.
// Do not unlink, replace, truncate, or reinitialize a file while opening/using it.
template <typename T, std::size_t Capacity>
class ShmFixedQueue {
    static_assert(std::is_trivially_copyable_v<T> && std::is_standard_layout_v<T>,
                  "shared messages must be trivially copyable and standard-layout");
    static_assert(std::is_same_v<T, std::remove_cv_t<T>> && !std::is_pointer_v<T> && !std::is_array_v<T>,
                  "use an unqualified non-pointer, non-array value type");
    static_assert(sizeof(T) <= static_cast<std::size_t>(std::numeric_limits<int>::max()),
                  "message size must fit the transfer return type");
    static_assert(Capacity >= 2 && (Capacity & (Capacity - 1)) == 0,
                  "capacity must be a power of two and at least two slots");

public:
    enum class Role : std::uint8_t
    {
        kProducer,
        kConsumer
    };

    ShmFixedQueue() noexcept                        = default;
    ~ShmFixedQueue()                                = default;
    ShmFixedQueue(const ShmFixedQueue &)            = delete;
    ShmFixedQueue &operator=(const ShmFixedQueue &) = delete;
    ShmFixedQueue(ShmFixedQueue &&other) noexcept {
        TakeFrom(other);
    }
    ShmFixedQueue &operator=(ShmFixedQueue &&other) noexcept {
        if (this != &other) {
            close();
            TakeFrom(other);
        }
        return *this;
    }

    // Capacity counts message slots, excluding the 192-byte control area. Only the
    // exclusive creator initializes. Abandoned initialization times out without
    // clearing/recreating the file. Timeout covers waiting, not filesystem I/O.
    [[nodiscard]] static ShmFixedQueue
    open(const std::string &path, Role role, std::chrono::milliseconds timeout = std::chrono::seconds(5)) {
        ValidateArguments(path, role, timeout);
        const auto deadline   = MakeDeadline(timeout);
        int        descriptor = ::open(path.c_str(), O_RDWR | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
        const bool created    = descriptor != -1;
        if (!created && errno == EEXIST) {
            descriptor = ::open(path.c_str(), O_RDWR | O_CLOEXEC);
        }
        if (descriptor == -1) {
            Fail(errno, "open shared queue");
        }
        const Descriptor file(descriptor);
        try {
            const auto total = MappingSize();
            PrepareFile(file.get(), created, total, deadline);
            // Pin the checked inode. The old class only sees a short procfs
            // path, and opens its own descriptor before this one is released.
            const auto pinned_path = std::string("/proc/self/fd/") + std::to_string(file.get());
            auto       memory      = std::make_unique<detail::shm_t>(pinned_path.c_str(), total);
            if (!memory->open() || memory->map() == nullptr) {
                Fail(errno, "map shared queue");
            }
            // Linux/GCC shared-memory ABI: fixed-width fields, no process-local
            // objects or pointers in the shared format.
            // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
            auto *header = reinterpret_cast<Header *>(memory->mem(0));
            PrepareHeader(header, created);
            return {std::move(memory), header, role};
        }
        catch (...) {
            if (created) {
                // Roll back only this creator's file; attachment never removes
                // an existing queue. Preserve the original exception on success.
                if (::unlink(path.c_str()) == -1 && errno != ENOENT) {
                    std::throw_with_nested(std::system_error(errno, std::generic_category(), "queue rollback"));
                }
            }
            throw;
        }
    }

    [[nodiscard]] int sendto(const T &message) noexcept {
        if (!valid()) {
            return Error(EBADF);
        }
        if (role_ != Role::kProducer) {
            return Error(EPERM);
        }
        auto used = position_ - cached_peer_;
        if (used > Capacity) {
            return Error(EPROTO);
        }
        if (used == Capacity) {
            cached_peer_ = __atomic_load_n(&header_->read_pos, __ATOMIC_ACQUIRE);
            used         = position_ - cached_peer_;
            if (used > Capacity) {
                return Error(EPROTO);
            }
            if (used == Capacity) {
                return Error(EAGAIN);
            }
        }
        std::memcpy(Slot(position_), std::addressof(message), sizeof(T));
        ++position_;
        __atomic_store_n(&header_->write_pos, position_, __ATOMIC_RELEASE);
        return static_cast<int>(sizeof(T));
    }

    [[nodiscard]] int recvfrom(T &message) noexcept {
        if (!valid()) {
            return Error(EBADF);
        }
        if (role_ != Role::kConsumer) {
            return Error(EPERM);
        }
        if (cached_peer_ == position_) {
            cached_peer_ = __atomic_load_n(&header_->write_pos, __ATOMIC_ACQUIRE);
        }
        const auto available = cached_peer_ - position_;
        if (available == 0) {
            return Error(EAGAIN);
        }
        if (available > Capacity) {
            return Error(EPROTO);
        }
        std::memcpy(std::addressof(message), Slot(position_), sizeof(T));
        ++position_;
        __atomic_store_n(&header_->read_pos, position_, __ATOMIC_RELEASE);
        return static_cast<int>(sizeof(T));
    }

    [[nodiscard]] bool valid() const noexcept {
        return memory_ != nullptr;
    }
    [[nodiscard]] std::uint64_t capacity() const noexcept {
        return valid() ? Capacity : 0;
    }
    void close() noexcept {
        // Preserve the original class's destructor flush/close behavior.
        memory_.reset();
        header_      = nullptr;
        data_        = nullptr;
        position_    = 0;
        cached_peer_ = 0;
    }

private:
    static constexpr std::size_t   kCacheLine = 64;
    static constexpr std::uint64_t kMagic     = 0x31514650534d4853ULL;
    static constexpr std::uint32_t kVersion   = 1;
    static constexpr std::uint32_t kReady     = 1;
    struct alignas(kCacheLine) Header {
        std::uint64_t                                             magic;
        std::uint32_t                                             version;
        std::uint32_t                                             header_size;
        std::uint64_t                                             capacity;
        std::uint64_t                                             total_size;
        std::uint64_t                                             message_size;
        std::uint32_t                                             message_alignment;
        std::uint32_t                                             ready;
        std::array<std::byte, kCacheLine - 48>                    metadata_padding;
        std::uint64_t                                             write_pos;
        std::array<std::byte, kCacheLine - sizeof(std::uint64_t)> write_padding;
        std::uint64_t                                             read_pos;
        std::array<std::byte, kCacheLine - sizeof(std::uint64_t)> read_padding;
    };
    static_assert(sizeof(Header) == 3 * kCacheLine);
    static constexpr auto kMaximumMapping =
        std::min<std::uint64_t>(std::numeric_limits<std::ptrdiff_t>::max(), std::numeric_limits<off_t>::max());
    static_assert(Capacity <= (kMaximumMapping - sizeof(Header)) / sizeof(T),
                  "fixed queue mapping exceeds pointer/file offset limits");
    static_assert(offsetof(Header, write_pos) == kCacheLine);
    static_assert(offsetof(Header, read_pos) == 2 * kCacheLine);
    static_assert(__atomic_always_lock_free(sizeof(std::uint64_t), nullptr));
    static_assert(__atomic_always_lock_free(sizeof(std::uint32_t), nullptr));
    class Descriptor {
    public:
        explicit Descriptor(int value) noexcept : value_(value) {
        }
        ~Descriptor() {
            (void)::close(value_);
        }
        Descriptor(const Descriptor &)             = delete;
        Descriptor &operator=(const Descriptor &)  = delete;
        Descriptor(Descriptor &&)                  = delete;
        Descriptor       &operator=(Descriptor &&) = delete;
        [[nodiscard]] int get() const noexcept {
            return value_;
        }

    private:
        int value_;
    };
    using Deadline = std::chrono::steady_clock::time_point;

    ShmFixedQueue(std::unique_ptr<detail::shm_t> memory, Header *header, Role role)
        : memory_(std::move(memory)), header_(header), data_(static_cast<void *>(memory_->mem(sizeof(Header)))),
          role_(role) {
        const auto write = __atomic_load_n(&header_->write_pos, __ATOMIC_ACQUIRE);
        const auto read  = __atomic_load_n(&header_->read_pos, __ATOMIC_ACQUIRE);
        if (write - read > Capacity) {
            Fail(EPROTO, "invalid shared queue cursors");
        }
        position_    = role == Role::kProducer ? write : read;
        cached_peer_ = role == Role::kProducer ? read : write;
    }
    [[noreturn]] static void Fail(int code, const char *operation) {
        throw std::system_error(code, std::generic_category(), operation);
    }
    static int Error(int code) noexcept {
        errno = code;
        return -1;
    }
    static void ValidateArguments(const std::string &path, Role role, std::chrono::milliseconds timeout) {
        if (path.empty() || path.find('\0') != std::string::npos || timeout.count() < 0 ||
            (role != Role::kProducer && role != Role::kConsumer)) {
            Fail(EINVAL, "shared queue arguments");
        }
    }
    static Deadline MakeDeadline(std::chrono::milliseconds timeout) {
        const auto now = std::chrono::steady_clock::now();
        if (timeout > std::chrono::duration_cast<std::chrono::milliseconds>(Deadline::max() - now)) {
            Fail(EOVERFLOW, "shared queue initialization timeout");
        }
        return now + timeout;
    }
    static bool ReadReady(int descriptor) {
        std::uint32_t ready = 0;
        const auto    count = ::pread(descriptor, &ready, sizeof(ready), offsetof(Header, ready));
        if (count == -1) {
            Fail(errno, "read shared queue initialization state");
        }
        if (count != static_cast<ssize_t>(sizeof(ready)) || (ready != 0 && ready != kReady)) {
            Fail(EPROTO, "invalid shared queue initialization state");
        }
        return ready == kReady;
    }
    static void PrepareFile(int descriptor, bool created, std::uint64_t total, Deadline deadline) {
        for (;;) {
            Lock(descriptor, deadline);
            struct stat status{};
            if (::fstat(descriptor, &status) == -1) {
                Fail(errno, "fstat shared queue");
            }
            if (!S_ISREG(status.st_mode)) {
                Fail(EINVAL, "shared queue requires a regular file");
            }
            if (created) {
                if (::ftruncate(descriptor, static_cast<off_t>(total)) == -1) {
                    Fail(errno, "size shared queue");
                }
                return;
            }
            // A noncreator may acquire the lock before the creator. It yields
            // rather than initializing. Crashed initialization is never reset.
            if (status.st_size != 0) {
                if (status.st_size < 0 || static_cast<std::uint64_t>(status.st_size) != total) {
                    Fail(EINVAL, "shared queue capacity mismatch");
                }
                if (ReadReady(descriptor)) {
                    return;
                }
            }
            Unlock(descriptor);
            Wait(deadline);
        }
    }
    static void PrepareHeader(Header *header, bool created) {
        const auto total = MappingSize();
        if (created) {
            header->magic             = kMagic;
            header->version           = kVersion;
            header->header_size       = sizeof(Header);
            header->capacity          = Capacity;
            header->total_size        = total;
            header->message_size      = sizeof(T);
            header->message_alignment = alignof(T);
            __atomic_store_n(&header->write_pos, 0, __ATOMIC_RELAXED);
            __atomic_store_n(&header->read_pos, 0, __ATOMIC_RELAXED);
            __atomic_store_n(&header->ready, kReady, __ATOMIC_RELEASE);
        }
        else if (__atomic_load_n(&header->ready, __ATOMIC_ACQUIRE) != kReady || header->magic != kMagic ||
                 header->version != kVersion || header->header_size != sizeof(Header) || header->capacity != Capacity ||
                 header->total_size != total || header->message_size != sizeof(T) ||
                 header->message_alignment != alignof(T)) {
            Fail(EPROTO, "incompatible shared queue format");
        }
    }
    static void Wait(Deadline deadline) {
        if (std::chrono::steady_clock::now() >= deadline) {
            Fail(ETIMEDOUT, "shared queue initialization");
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    static void Lock(int descriptor, Deadline deadline) {
        while (::flock(descriptor, LOCK_EX | LOCK_NB) == -1) {
            const int error = errno;
            if (error != EWOULDBLOCK && error != EAGAIN && error != EINTR) {
                Fail(error, "lock shared queue initialization");
            }
            Wait(deadline);
        }
    }
    static void Unlock(int descriptor) {
        if (::flock(descriptor, LOCK_UN) == -1) {
            Fail(errno, "unlock shared queue initialization");
        }
    }
    [[nodiscard]] void *Slot(std::uint64_t position) const noexcept {
        const auto offset = (position & (Capacity - 1)) * sizeof(T);
        return static_cast<std::byte *>(data_) + offset;
    }
    static constexpr std::uint64_t MappingSize() noexcept {
        return sizeof(Header) + (Capacity * sizeof(T));
    }
    void TakeFrom(ShmFixedQueue &other) noexcept {
        memory_      = std::move(other.memory_);
        header_      = std::exchange(other.header_, nullptr);
        data_        = std::exchange(other.data_, nullptr);
        position_    = std::exchange(other.position_, 0);
        cached_peer_ = std::exchange(other.cached_peer_, 0);
        role_        = other.role_;
    }
    std::unique_ptr<detail::shm_t> memory_;
    Header                        *header_      = nullptr;
    void                          *data_        = nullptr;
    std::uint64_t                  position_    = 0;
    std::uint64_t                  cached_peer_ = 0;
    Role                           role_        = Role::kProducer;
};

template <typename T, std::size_t Capacity>
using shm_spsc_queue_t = ShmFixedQueue<T, Capacity>;
}  // namespace basic
