#include "../fixed_shared_mem.h"

#include <gtest/gtest.h>
#include <signal.h>  // NOLINT(modernize-deprecated-headers): POSIX kill.
#include <stdlib.h>  // NOLINT(modernize-deprecated-headers): POSIX extensions.
#include <sys/ipc.h>
#include <sys/resource.h>
#include <sys/shm.h>
#include <sys/wait.h>
#include <unistd.h>

#include <array>
#include <cerrno>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <string>
#include <system_error>
#include <thread>
#include <type_traits>
#include <utility>

// Assertion macro branches inflate complexity in straight-line tests.
// NOLINTBEGIN(readability-function-cognitive-complexity)

namespace {
constexpr std::uint64_t kCapacity = 4099;

template <typename Operation>
void ExpectError(Operation operation, int expected) {
    try {
        (void)operation();
        ADD_FAILURE() << "expected system_error " << expected;
    }
    catch (const std::system_error &error) {
        EXPECT_EQ(error.code().value(), expected) << error.what();
    }
}

template <typename Backend>
class SharedMemoryTest : public ::testing::Test {
protected:
    using Memory = basic::shm_t<Backend>;
    void SetUp() override {
        char        pattern[] = "/tmp/net-test-shm-XXXXXX";
        const char *directory = ::mkdtemp(pattern);
        ASSERT_NE(directory, nullptr);
        directory_ = directory;
        if constexpr (std::is_same_v<Backend, basic::MmapBackend>) {
            locator_.path = directory_ + "/region";
        }
        else {
            bool available = false;
            for (int project = 1; project < 256; ++project) {
                locator_.key = ::ftok(directory_.c_str(), project);
                ASSERT_NE(locator_.key, static_cast<key_t>(-1));
                if (::shmget(locator_.key, 0, 0) == -1 && errno == ENOENT) {
                    available = true;
                    break;
                }
            }
            ASSERT_TRUE(available);
        }
    }
    void TearDown() override {
        if (owned_) {
            EXPECT_FALSE(Memory::Remove(locator_));
        }
        if (!directory_.empty()) {
            EXPECT_EQ(::rmdir(directory_.c_str()), 0);
        }
    }
    Memory Create(basic::CreateOptions options = {}) {
        auto memory = Memory::Create(locator_, kCapacity, options);
        owned_      = true;
        return memory;
    }
    // GTest state is shared with generated derived test classes.
    // NOLINTBEGIN(cppcoreguidelines-non-private-member-variables-in-classes)
    typename Backend::Locator locator_;
    std::string               directory_;
    bool                      owned_ = false;
    // NOLINTEND(cppcoreguidelines-non-private-member-variables-in-classes)
};
using Backends = ::testing::Types<basic::MmapBackend, basic::SysVBackend>;
TYPED_TEST_SUITE(SharedMemoryTest, Backends);

TYPED_TEST(SharedMemoryTest, ExclusiveCreateAndAttachPreserveBytes) {
    using Memory = typename TestFixture::Memory;
    auto owner   = this->Create();
    ASSERT_TRUE(owner.valid());
    EXPECT_EQ(owner.capacity(), kCapacity);
    std::array<char, 7> initial{};
    ASSERT_FALSE(owner.Read(kCapacity - initial.size(), initial.data(), initial.size()));
    EXPECT_EQ(initial, (std::array<char, 7>{}));
    const std::string payload = "shared memory";
    ASSERT_FALSE(owner.Write(0, payload.c_str(), payload.size() + 1));
    ExpectError([&] { return Memory::Create(this->locator_, 1); }, EEXIST);
    auto peer = Memory::Attach(this->locator_, {kCapacity, {}});
    EXPECT_EQ(peer.capacity(), kCapacity);
    EXPECT_NE(peer.data(), owner.data());
    std::array<char, 32> buffer{};
    ASSERT_FALSE(peer.Read(0, buffer.data(), payload.size() + 1));
    EXPECT_STREQ(buffer.data(), payload.c_str());
    ASSERT_FALSE(peer.Write(kCapacity - 1, "x", 1));
    EXPECT_EQ(*owner.Mem(kCapacity - 1), std::byte{'x'});
    ExpectError([&] { return Memory::Attach(this->locator_, {kCapacity + 1, {}}); }, EINVAL);
    auto still_present = Memory::Attach(this->locator_);
    EXPECT_EQ(still_present.capacity(), kCapacity);
}

TYPED_TEST(SharedMemoryTest, CheckedAccessIsExactAndOverflowSafe) {
    auto owner = this->Create();
    EXPECT_EQ(owner.Mem(kCapacity), nullptr);
    EXPECT_EQ(owner.Mem(kCapacity, 0), owner.data() + kCapacity);
    EXPECT_EQ(owner.Mem(0, kCapacity), owner.data());
    EXPECT_EQ(owner.Mem(std::numeric_limits<std::uint64_t>::max(), 2), nullptr);
    EXPECT_EQ(owner.Mem(2, std::numeric_limits<std::uint64_t>::max()), nullptr);
    ASSERT_FALSE(owner.Write(kCapacity - 2, "ab", 2));
    EXPECT_EQ(owner.Write(kCapacity - 1, "cd", 2).value(), ERANGE);
    std::array<char, 3> buffer{'?', '?', '?'};
    EXPECT_EQ(owner.Read(kCapacity - 1, buffer.data(), 2).value(), ERANGE);
    EXPECT_EQ(buffer.front(), '?');
    ASSERT_FALSE(owner.Read(kCapacity - 2, buffer.data(), 2));
    EXPECT_EQ(buffer, (std::array<char, 3>{'a', 'b', '?'}));
}

TYPED_TEST(SharedMemoryTest, ZeroLengthNullAndOverlappingCopies) {
    auto owner = this->Create();
    EXPECT_FALSE(owner.Read(kCapacity, nullptr, 0));
    EXPECT_FALSE(owner.Write(kCapacity, nullptr, 0));
    EXPECT_EQ(owner.Read(kCapacity + 1, nullptr, 0).value(), ERANGE);
    EXPECT_EQ(owner.Write(0, nullptr, 1).value(), EINVAL);
    EXPECT_EQ(owner.Read(0, nullptr, 1).value(), EINVAL);
    ASSERT_FALSE(owner.Write(0, "abcdef", 7));
    ASSERT_FALSE(owner.Write(1, owner.Mem(0), 5));
    std::array<char, 7> buffer{};
    ASSERT_FALSE(owner.Read(0, buffer.data(), buffer.size()));
    EXPECT_STREQ(buffer.data(), "aabcde");
    ASSERT_FALSE(owner.Read(1, owner.Mem(0), 5));
    ASSERT_FALSE(owner.Read(0, buffer.data(), buffer.size()));
    EXPECT_STREQ(buffer.data(), "abcdee");
    const auto &immutable = owner;
    static_assert(std::is_same_v<decltype(immutable.Mem(0)), const std::byte *>);
    static_assert(std::is_same_v<decltype(immutable.view()), basic::ConstShmView>);
    EXPECT_EQ(immutable.view().Mem(1), immutable.data() + 1);
    EXPECT_FALSE(owner.view().Write(0, "v", 1));
}

TYPED_TEST(SharedMemoryTest, CloseDoesNotRemoveAndRemoveDoesNotInvalidateViews) {
    using Memory = typename TestFixture::Memory;
    auto owner   = this->Create();
    auto peer    = Memory::Attach(this->locator_);
    EXPECT_FALSE(owner.Close());
    EXPECT_FALSE(owner.Close());
    EXPECT_FALSE(owner.valid());
    EXPECT_EQ(owner.capacity(), 0);
    EXPECT_EQ(owner.data(), nullptr);
    EXPECT_EQ(owner.Write(0, nullptr, 0).value(), EBADF);
    EXPECT_EQ(owner.Read(0, nullptr, 0).value(), EBADF);
    EXPECT_EQ(owner.Mem(0, 0), nullptr);
    auto reattached = Memory::Attach(this->locator_);
    EXPECT_FALSE(Memory::Remove(this->locator_));
    EXPECT_FALSE(Memory::Remove(this->locator_));
    ExpectError([&] { return Memory::Attach(this->locator_); }, ENOENT);
    ASSERT_FALSE(peer.Write(0, "z", 1));
    EXPECT_EQ(*reattached.Mem(0), std::byte{'z'});
}

TYPED_TEST(SharedMemoryTest, DestructorDetachesWithoutRemovingResource) {
    using Memory = typename TestFixture::Memory;
    {
        auto temporary = this->Create();
        ASSERT_FALSE(temporary.Write(0, "p", 1));
    }
    auto peer = Memory::Attach(this->locator_);
    EXPECT_EQ(*peer.Mem(0), std::byte{'p'});
}

TYPED_TEST(SharedMemoryTest, MoveTransfersOwnershipAndClearsSource) {
    using Memory = typename TestFixture::Memory;
    static_assert(!std::is_copy_constructible_v<Memory>);
    static_assert(!std::is_copy_assignable_v<Memory>);
    static_assert(std::is_nothrow_move_constructible_v<Memory>);
    auto       original = this->Create();
    auto      *address  = original.data();
    const auto view     = original.view();
    Memory     moved(std::move(original));
    EXPECT_FALSE(original.valid());  // NOLINT(clang-analyzer-cplusplus.Move): moved-from contract.
    EXPECT_EQ(original.capacity(), 0);
    EXPECT_EQ(moved.data(), address);
    EXPECT_FALSE(view.Write(0, "m", 1));
    auto destination = Memory::Attach(this->locator_);
    destination      = std::move(moved);
    EXPECT_FALSE(moved.valid());  // NOLINT(clang-analyzer-cplusplus.Move): moved-from contract.
    EXPECT_EQ(destination.data(), address);
    auto *self  = &destination;
    destination = std::move(*self);
    EXPECT_EQ(destination.data(), address);
    EXPECT_EQ(*destination.Mem(0), std::byte{'m'});
}

TYPED_TEST(SharedMemoryTest, InvalidCreateAndMissingAttachLeaveNoResource) {
    using Memory = typename TestFixture::Memory;
    ExpectError([&] { return Memory::Attach(this->locator_); }, ENOENT);
    ExpectError([&] { return Memory::Create(this->locator_, 0); }, EINVAL);
    ExpectError([&] { return Memory::Create(this->locator_, std::numeric_limits<std::uint64_t>::max()); }, EOVERFLOW);
    basic::CreateOptions options;
    options.permissions = 01000;
    ExpectError([&] { return Memory::Create(this->locator_, kCapacity, options); }, EINVAL);
    ExpectError([&] { return Memory::Attach(this->locator_); }, ENOENT);
    auto owner = this->Create({0600, {false, false}});
    EXPECT_TRUE(owner.valid());
}

// Decreasing only the soft limit is reversible and forces mlock to fail for an
// unprivileged process. The fixture always restores the original limit.
class DisabledMemoryLock {
public:
    DisabledMemoryLock() {
        if (::getrlimit(RLIMIT_MEMLOCK, &previous_) != 0) {
            throw std::system_error(errno, std::generic_category(), "getrlimit");
        }
        auto disabled     = previous_;
        disabled.rlim_cur = 0;
        if (::setrlimit(RLIMIT_MEMLOCK, &disabled) != 0) {
            throw std::system_error(errno, std::generic_category(), "setrlimit");
        }
    }
    ~DisabledMemoryLock() {
        EXPECT_EQ(::setrlimit(RLIMIT_MEMLOCK, &previous_), 0);
    }
    DisabledMemoryLock(const DisabledMemoryLock &)            = delete;
    DisabledMemoryLock &operator=(const DisabledMemoryLock &) = delete;
    DisabledMemoryLock(DisabledMemoryLock &&)                 = delete;
    DisabledMemoryLock &operator=(DisabledMemoryLock &&)      = delete;

private:
    struct rlimit previous_{};
};

TYPED_TEST(SharedMemoryTest, LockFailureRollsBackCreateAndPreservesExistingResource) {
    using Memory = typename TestFixture::Memory;
    const DisabledMemoryLock disabled;
    basic::CreateOptions     options;
    options.mapping.lock_memory = true;
    try {
        auto unexpected = Memory::Create(this->locator_, kCapacity, options);
        this->owned_    = true;
        FAIL() << "test requires an unprivileged process without CAP_IPC_LOCK";
    }
    catch (const std::system_error &error) {
        EXPECT_TRUE(error.code().value() == EPERM || error.code().value() == ENOMEM);
    }
    ExpectError([&] { return Memory::Attach(this->locator_); }, ENOENT);
    auto owner = this->Create();
    ASSERT_FALSE(owner.Write(0, "safe", 5));
    basic::AttachOptions attach;
    attach.mapping.lock_memory = true;
    try {
        auto unexpected = Memory::Attach(this->locator_, attach);
        FAIL() << "expected mlock failure";
    }
    catch (const std::system_error &error) {
        EXPECT_TRUE(error.code().value() == EPERM || error.code().value() == ENOMEM);
    }
    auto                peer = Memory::Attach(this->locator_);
    std::array<char, 5> buffer{};
    ASSERT_FALSE(peer.Read(0, buffer.data(), buffer.size()));
    EXPECT_STREQ(buffer.data(), "safe");
}

// Re-exec loses inherited mappings: this child must attach through the locator.
// Exit status supplies the acknowledgment; the component needs no notification.
template <typename Backend>
int RunPeer(const typename Backend::Locator &locator) {
    auto                peer = basic::shm_t<Backend>::Attach(locator, {kCapacity, {}});
    std::array<char, 5> message{};
    if (peer.Read(0, message.data(), message.size()) || std::string(message.data()) != "ping") {
        return 1;
    }
    return peer.Write(0, "pong", 5) ? 2 : 0;
}

TEST(SharedMemoryProcessHelper, AttachAndReply) {
    const char *resource = std::getenv("NET_TEST_SHM_RESOURCE");
    const char *backend  = std::getenv("NET_TEST_SHM_BACKEND");
    if (resource == nullptr || backend == nullptr) {
        GTEST_SKIP() << "only invoked by the re-exec integration tests";
    }
    if (std::string(backend) == "mmap") {
        EXPECT_EQ(RunPeer<basic::MmapBackend>({resource}), 0);
    }
    else {
        EXPECT_EQ(RunPeer<basic::SysVBackend>({static_cast<key_t>(std::stol(resource))}), 0);
    }
}

TYPED_TEST(SharedMemoryTest, IndependentProcessAttachesAndExchangesData) {
    auto owner = this->Create();
    ASSERT_FALSE(owner.Write(0, "ping", 5));
    std::string resource;
    const char *backend = nullptr;
    if constexpr (std::is_same_v<TypeParam, basic::MmapBackend>) {
        resource = this->locator_.path;
        backend  = "mmap";
    }
    else {
        resource = std::to_string(this->locator_.key);
        backend  = "sysv";
    }
    const auto child = ::fork();
    ASSERT_NE(child, -1);
    if (child == 0) {
        if (::setenv("NET_TEST_SHM_RESOURCE", resource.c_str(), 1) == -1 ||
            ::setenv("NET_TEST_SHM_BACKEND", backend, 1) == -1) {
            ::_exit(126);
        }
        ::execl("/proc/self/exe", "shm-peer", "--gtest_filter=SharedMemoryProcessHelper.AttachAndReply", nullptr);
        ::_exit(127);
    }
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    int        status   = 0;
    auto       result   = 0;
    while (std::chrono::steady_clock::now() < deadline) {
        result = ::waitpid(child, &status, WNOHANG);
        if (result == child || (result == -1 && errno != EINTR)) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    if (result != child) {
        (void)::kill(child, SIGKILL);
        (void)::waitpid(child, &status, 0);
        FAIL() << "peer failed or timed out";
    }
    ASSERT_TRUE(WIFEXITED(status));
    ASSERT_EQ(WEXITSTATUS(status), 0);
    std::array<char, 5> message{};
    ASSERT_FALSE(owner.Read(0, message.data(), message.size()));
    EXPECT_STREQ(message.data(), "pong");
}

TEST(SharedMemoryValidation, RejectsInvalidLocators) {
    using Mmap = basic::shm_t<>;
    using SysV = basic::shm_t<basic::SysVBackend>;
    ExpectError([] { return Mmap::Create({""}, 1); }, EINVAL);
    ExpectError([] { return Mmap::Attach({std::string("a\0b", 3)}); }, EINVAL);
    EXPECT_EQ(Mmap::Remove({""}).value(), EINVAL);
    ExpectError([] { return SysV::Create({IPC_PRIVATE}, 1); }, EINVAL);
    ExpectError([] { return SysV::Attach({IPC_PRIVATE}); }, EINVAL);
    EXPECT_EQ(SysV::Remove({IPC_PRIVATE}).value(), EINVAL);
}

}  // namespace
// NOLINTEND(readability-function-cognitive-complexity)
