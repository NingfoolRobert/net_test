/** @file fixed_shared_mem.h
 * Fixed-capacity Linux shared memory. Application protocols own synchronization
 * and initialization; attachment only makes the bytes accessible.
 */
#pragma once

#include <fcntl.h>
#include <sys/ipc.h>
#include <sys/mman.h>
#include <sys/shm.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <exception>
#include <limits>
#include <string>
#include <system_error>
#include <type_traits>
#include <utility>

namespace basic {
struct MappingOptions {
    bool prefault    = true;
    bool lock_memory = false;
};
struct CreateOptions {
    mode_t         permissions = 0600;
    MappingOptions mapping;
};
struct AttachOptions {
    // Zero accepts the existing capacity; nonzero requires an exact match.
    std::uint64_t  expected_capacity = 0;
    MappingOptions mapping;
};
struct MmapLocator {
    std::string path;
};
struct SysVLocator {
    key_t key = IPC_PRIVATE;
};

namespace shm_detail {
inline std::error_code Error(int code) noexcept {
    return {code, std::generic_category()};
}
[[noreturn]] inline void Fail(int code, const char *operation) {
    throw std::system_error(Error(code), operation);
}
inline void ValidateCapacity(std::uint64_t capacity) {
    if (capacity == 0) {
        Fail(EINVAL, "shared memory capacity must be positive");
    }
    // Pointer arithmetic and file offsets must represent the whole region.
    constexpr auto max_capacity =
        std::min<std::uint64_t>(std::numeric_limits<std::ptrdiff_t>::max(), std::numeric_limits<off_t>::max());
    if (capacity > max_capacity) {
        Fail(EOVERFLOW, "shared memory capacity");
    }
}
inline void ValidatePath(const std::string &path) {
    if (path.empty() || path.find('\0') != std::string::npos) {
        Fail(EINVAL, "shared memory path");
    }
}
inline void ValidatePermissions(mode_t permissions) {
    if ((permissions & ~static_cast<mode_t>(0777)) != 0) {
        Fail(EINVAL, "shared memory permissions");
    }
}
class FileDescriptor {
public:
    explicit FileDescriptor(int descriptor = -1) noexcept : descriptor_(descriptor) {
    }
    ~FileDescriptor() {
        (void)Close();
    }
    FileDescriptor(const FileDescriptor &)            = delete;
    FileDescriptor &operator=(const FileDescriptor &) = delete;
    FileDescriptor(FileDescriptor &&other) noexcept : descriptor_(std::exchange(other.descriptor_, -1)) {
    }
    FileDescriptor &operator=(FileDescriptor &&other) noexcept {
        if (this != &other) {
            (void)Close();
            descriptor_ = std::exchange(other.descriptor_, -1);
        }
        return *this;
    }
    [[nodiscard]] int get() const noexcept {
        return descriptor_;
    }
    std::error_code Close() noexcept {
        // Linux releases the fd even on EINTR; retrying could close a reused fd.
        const int descriptor = std::exchange(descriptor_, -1);
        return descriptor != -1 && ::close(descriptor) == -1 ? Error(errno) : std::error_code{};
    }

private:
    int descriptor_;
};
struct Mapping {
    std::byte    *data     = nullptr;
    std::uint64_t capacity = 0;
};
inline std::uint64_t FileCapacity(int descriptor) {
    struct stat status{};
    if (::fstat(descriptor, &status) == -1) {
        Fail(errno, "fstat shared memory");
    }
    if (!S_ISREG(status.st_mode) || status.st_size <= 0) {
        Fail(EINVAL, "shared memory must be a nonempty regular file");
    }
    const auto capacity = static_cast<std::uint64_t>(status.st_size);
    ValidateCapacity(capacity);
    return capacity;
}
inline Mapping MapFile(int descriptor, std::uint64_t capacity) {
    void *address =
        ::mmap(nullptr, static_cast<std::size_t>(capacity), PROT_READ | PROT_WRITE, MAP_SHARED, descriptor, 0);
    if (address == MAP_FAILED) {
        Fail(errno, "mmap shared memory");
    }
    return {static_cast<std::byte *>(address), capacity};
}
inline void Prepare(Mapping mapping, MappingOptions options) {
    const auto length = static_cast<std::size_t>(mapping.capacity);
    if (options.prefault) {
#ifdef MADV_POPULATE_WRITE
        if (::madvise(mapping.data, length, MADV_POPULATE_WRITE) == -1) {
            Fail(errno, "madvise MADV_POPULATE_WRITE");
        }
#else
        Fail(ENOTSUP, "MADV_POPULATE_WRITE unavailable in Linux headers");
#endif
    }
    if (options.lock_memory && ::mlock(mapping.data, length) == -1) {
        Fail(errno, "mlock shared memory");
    }
}
inline void CheckExpectedCapacity(std::uint64_t actual, std::uint64_t expected) {
    if (expected != 0 && actual != expected) {
        Fail(EINVAL, "shared memory capacity mismatch");
    }
}
inline void RecordError(std::error_code candidate, std::error_code *first) noexcept {
    if (!*first) {
        *first = candidate;
    }
}
// Retain the original exception as a nested cause if rollback also fails.
[[noreturn]] inline void RethrowAfterRollback(std::error_code cleanup_error) {
    if (cleanup_error) {
        std::throw_with_nested(std::system_error(cleanup_error, "shared memory rollback failed"));
    }
    throw;
}
}  // namespace shm_detail

struct MmapBackend {
    using Locator = MmapLocator;
    static std::error_code Unmap(shm_detail::Mapping mapping) noexcept {
        return ::munmap(mapping.data, static_cast<std::size_t>(mapping.capacity)) == -1 ? shm_detail::Error(errno)
                                                                                        : std::error_code{};
    }
    static shm_detail::Mapping Create(const Locator &locator, std::uint64_t capacity, CreateOptions options) {
        shm_detail::ValidatePath(locator.path);
        shm_detail::ValidateCapacity(capacity);
        shm_detail::ValidatePermissions(options.permissions);
        shm_detail::FileDescriptor descriptor(
            ::open(locator.path.c_str(), O_RDWR | O_CREAT | O_EXCL | O_CLOEXEC, options.permissions));
        if (descriptor.get() == -1) {
            shm_detail::Fail(errno, "create shared memory file");
        }
        shm_detail::Mapping mapping;
        try {
            if (::ftruncate(descriptor.get(), static_cast<off_t>(capacity)) == -1) {
                shm_detail::Fail(errno, "ftruncate shared memory");
            }
            mapping = shm_detail::MapFile(descriptor.get(), capacity);
            shm_detail::Prepare(mapping, options.mapping);
            if (const auto error = descriptor.Close()) {
                throw std::system_error(error, "close shared memory file");
            }
            return mapping;
        }
        catch (...) {
            std::error_code cleanup_error;
            if (mapping.data != nullptr) {
                shm_detail::RecordError(Unmap(mapping), &cleanup_error);
            }
            shm_detail::RecordError(descriptor.Close(), &cleanup_error);
            if (::unlink(locator.path.c_str()) == -1) {
                shm_detail::RecordError(shm_detail::Error(errno), &cleanup_error);
            }
            shm_detail::RethrowAfterRollback(cleanup_error);
        }
    }
    static shm_detail::Mapping Attach(const Locator &locator, AttachOptions options) {
        shm_detail::ValidatePath(locator.path);
        shm_detail::FileDescriptor descriptor(::open(locator.path.c_str(), O_RDWR | O_CLOEXEC));
        if (descriptor.get() == -1) {
            shm_detail::Fail(errno, "open shared memory file");
        }
        shm_detail::Mapping mapping;
        try {
            const auto capacity = shm_detail::FileCapacity(descriptor.get());
            shm_detail::CheckExpectedCapacity(capacity, options.expected_capacity);
            mapping = shm_detail::MapFile(descriptor.get(), capacity);
            shm_detail::Prepare(mapping, options.mapping);
            if (const auto error = descriptor.Close()) {
                throw std::system_error(error, "close shared memory file");
            }
            return mapping;
        }
        catch (...) {
            std::error_code cleanup_error;
            if (mapping.data != nullptr) {
                shm_detail::RecordError(Unmap(mapping), &cleanup_error);
            }
            shm_detail::RecordError(descriptor.Close(), &cleanup_error);
            shm_detail::RethrowAfterRollback(cleanup_error);
        }
    }
    static std::error_code Remove(const Locator &locator) noexcept {
        if (locator.path.empty() || locator.path.find('\0') != std::string::npos) {
            return shm_detail::Error(EINVAL);
        }
        return ::unlink(locator.path.c_str()) == -1 && errno != ENOENT ? shm_detail::Error(errno) : std::error_code{};
    }
};

struct SysVBackend {
    using Locator = SysVLocator;
    static std::error_code Unmap(shm_detail::Mapping mapping) noexcept {
        return ::shmdt(mapping.data) == -1 ? shm_detail::Error(errno) : std::error_code{};
    }
    static shm_detail::Mapping Create(const Locator &locator, std::uint64_t capacity, CreateOptions options) {
        ValidateLocator(locator);
        shm_detail::ValidateCapacity(capacity);
        shm_detail::ValidatePermissions(options.permissions);
        const int identifier = ::shmget(locator.key,
                                        static_cast<std::size_t>(capacity),
                                        IPC_CREAT | IPC_EXCL | static_cast<int>(options.permissions));
        if (identifier == -1) {
            shm_detail::Fail(errno, "shmget create");
        }
        shm_detail::Mapping mapping;
        try {
            mapping = Map(identifier, capacity);
            shm_detail::Prepare(mapping, options.mapping);
            return mapping;
        }
        catch (...) {
            std::error_code cleanup_error;
            if (mapping.data != nullptr) {
                shm_detail::RecordError(Unmap(mapping), &cleanup_error);
            }
            if (::shmctl(identifier, IPC_RMID, nullptr) == -1) {
                shm_detail::RecordError(shm_detail::Error(errno), &cleanup_error);
            }
            shm_detail::RethrowAfterRollback(cleanup_error);
        }
    }
    static shm_detail::Mapping Attach(const Locator &locator, AttachOptions options) {
        ValidateLocator(locator);
        const int       identifier = Find(locator);
        struct shmid_ds status{};
        if (::shmctl(identifier, IPC_STAT, &status) == -1) {
            shm_detail::Fail(errno, "shmctl IPC_STAT");
        }
        const auto capacity = static_cast<std::uint64_t>(status.shm_segsz);
        shm_detail::ValidateCapacity(capacity);
        shm_detail::CheckExpectedCapacity(capacity, options.expected_capacity);
        const auto mapping = Map(identifier, capacity);
        try {
            shm_detail::Prepare(mapping, options.mapping);
            return mapping;
        }
        catch (...) {
            shm_detail::RethrowAfterRollback(Unmap(mapping));
        }
    }
    static std::error_code Remove(const Locator &locator) noexcept {
        if (locator.key == IPC_PRIVATE) {
            return shm_detail::Error(EINVAL);
        }
        const int identifier = ::shmget(locator.key, 0, 0);
        if (identifier == -1) {
            return errno == ENOENT ? std::error_code{} : shm_detail::Error(errno);
        }
        return ::shmctl(identifier, IPC_RMID, nullptr) == -1 ? shm_detail::Error(errno) : std::error_code{};
    }

private:
    static void ValidateLocator(const Locator &locator) {
        if (locator.key == IPC_PRIVATE) {
            shm_detail::Fail(EINVAL, "IPC_PRIVATE is not a named shared memory key");
        }
    }
    static int Find(const Locator &locator) {
        const int identifier = ::shmget(locator.key, 0, 0);
        if (identifier == -1) {
            shm_detail::Fail(errno, "shmget attach");
        }
        return identifier;
    }
    static shm_detail::Mapping Map(int identifier, std::uint64_t capacity) {
        void *address = ::shmat(identifier, nullptr, 0);
        // shmat specifies (void*)-1, unlike mmap's MAP_FAILED macro.
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast,performance-no-int-to-ptr)
        if (address == reinterpret_cast<void *>(-1)) {
            shm_detail::Fail(errno, "shmat");
        }
        return {static_cast<std::byte *>(address), capacity};
    }
};

// Non-owning process-local view. Closing/replacing the owner invalidates views.
// Byte copies provide no interprocess synchronization.
template <typename Byte>
class BasicShmView {
    static_assert(std::is_same_v<std::remove_const_t<Byte>, std::byte>);

public:
    constexpr BasicShmView() noexcept = default;
    constexpr BasicShmView(Byte *data, std::uint64_t capacity) noexcept : data_(data), capacity_(capacity) {
    }
    [[nodiscard]] bool valid() const noexcept {
        return data_ != nullptr;
    }
    [[nodiscard]] Byte *data() const noexcept {
        return data_;
    }
    [[nodiscard]] std::uint64_t capacity() const noexcept {
        return capacity_;
    }
    [[nodiscard]] Byte *Mem(std::uint64_t offset, std::uint64_t length = 1) const noexcept {
        return CheckRange(offset, length) ? nullptr : data_ + offset;
    }
    [[nodiscard]] std::error_code Read(std::uint64_t offset, void *destination, std::size_t length) const noexcept {
        if (const auto error = CheckRange(offset, length)) {
            return error;
        }
        if (length != 0) {
            if (destination == nullptr) {
                return shm_detail::Error(EINVAL);
            }
            std::memmove(destination, data_ + offset, length);
        }
        return {};
    }
    template <typename B = Byte, std::enable_if_t<std::is_same_v<B, Byte> && !std::is_const_v<B>, int> = 0>
    [[nodiscard]] std::error_code Write(std::uint64_t offset, const void *source, std::size_t length) const noexcept {
        if (const auto error = CheckRange(offset, length)) {
            return error;
        }
        if (length != 0) {
            if (source == nullptr) {
                return shm_detail::Error(EINVAL);
            }
            std::memmove(data_ + offset, source, length);
        }
        return {};
    }

private:
    [[nodiscard]] std::error_code CheckRange(std::uint64_t offset, std::uint64_t length) const noexcept {
        if (!valid()) {
            return shm_detail::Error(EBADF);
        }
        return offset > capacity_ || length > capacity_ - offset ? shm_detail::Error(ERANGE) : std::error_code{};
    }
    Byte         *data_     = nullptr;
    std::uint64_t capacity_ = 0;
};
using ShmView      = BasicShmView<std::byte>;
using ConstShmView = BasicShmView<const std::byte>;

template <typename Backend = MmapBackend>
class FixedSharedMem {
public:
    using Locator             = typename Backend::Locator;
    FixedSharedMem() noexcept = default;
    ~FixedSharedMem() {
        (void)Close();
    }
    FixedSharedMem(const FixedSharedMem &)            = delete;
    FixedSharedMem &operator=(const FixedSharedMem &) = delete;
    FixedSharedMem(FixedSharedMem &&other) noexcept : mapping_(std::exchange(other.mapping_, {})) {
    }
    // A failed detach must leave both mappings owned and report its error.
    // NOLINTNEXTLINE(cppcoreguidelines-noexcept-move-operations,performance-noexcept-move-constructor)
    FixedSharedMem &operator=(FixedSharedMem &&other) {
        if (this != &other) {
            if (const auto error = Close()) {
                throw std::system_error(error, "close shared memory before move assignment");
            }
            mapping_ = std::exchange(other.mapping_, {});
        }
        return *this;
    }
    [[nodiscard]] static FixedSharedMem
    Create(const Locator &locator, std::uint64_t capacity, CreateOptions options = {}) {
        return FixedSharedMem(Backend::Create(locator, capacity, options));
    }
    [[nodiscard]] static FixedSharedMem Attach(const Locator &locator, AttachOptions options = {}) {
        return FixedSharedMem(Backend::Attach(locator, options));
    }
    [[nodiscard]] static std::error_code Remove(const Locator &locator) noexcept {
        return Backend::Remove(locator);
    }
    [[nodiscard]] bool valid() const noexcept {
        return mapping_.data != nullptr;
    }
    [[nodiscard]] std::uint64_t capacity() const noexcept {
        return mapping_.capacity;
    }
    [[nodiscard]] std::byte *data() noexcept {
        return mapping_.data;
    }
    [[nodiscard]] const std::byte *data() const noexcept {
        return mapping_.data;
    }
    [[nodiscard]] ShmView view() noexcept {
        return {data(), capacity()};
    }
    [[nodiscard]] ConstShmView view() const noexcept {
        return {data(), capacity()};
    }
    [[nodiscard]] std::byte *Mem(std::uint64_t offset, std::uint64_t length = 1) noexcept {
        return view().Mem(offset, length);
    }
    [[nodiscard]] const std::byte *Mem(std::uint64_t offset, std::uint64_t length = 1) const noexcept {
        return view().Mem(offset, length);
    }
    [[nodiscard]] std::error_code Read(std::uint64_t offset, void *destination, std::size_t length) const noexcept {
        return view().Read(offset, destination, length);
    }
    [[nodiscard]] std::error_code Write(std::uint64_t offset, const void *source, std::size_t length) noexcept {
        return view().Write(offset, source, length);
    }
    [[nodiscard]] std::error_code Close() noexcept {
        if (valid()) {
            if (const auto error = Backend::Unmap(mapping_)) {
                return error;
            }
            mapping_ = {};
        }
        return {};
    }

private:
    explicit FixedSharedMem(shm_detail::Mapping mapping) noexcept : mapping_(mapping) {
    }
    shm_detail::Mapping mapping_;
};
template <typename Backend = MmapBackend>
using shm_t = FixedSharedMem<Backend>;
}  // namespace basic
