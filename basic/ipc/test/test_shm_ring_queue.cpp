#include "../shm_ring_queue.h"

#include <fcntl.h>
#include <gtest/gtest.h>
#include <signal.h>  // NOLINT(modernize-deprecated-headers): POSIX kill.
#include <stdlib.h>  // NOLINT(modernize-deprecated-headers): POSIX test helpers.
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <limits>
#include <string>
#include <system_error>
#include <thread>
#include <type_traits>
#include <utility>

// GTest assertions expand into branches in otherwise straight-line tests.
// NOLINTBEGIN(readability-function-cognitive-complexity)
namespace {
using Queue                           = basic::ShmRingQueue;
using Role                            = Queue::Role;
constexpr std::uint64_t kCapacity     = 2048;
constexpr std::uint64_t kControlBytes = 192;

class Descriptor {
public:
    explicit Descriptor(int descriptor) : descriptor_(descriptor) {
    }
    ~Descriptor() {
        if (descriptor_ != -1) {
            (void)::close(descriptor_);
        }
    }
    Descriptor(const Descriptor &)             = delete;
    Descriptor &operator=(const Descriptor &)  = delete;
    Descriptor(Descriptor &&)                  = delete;
    Descriptor       &operator=(Descriptor &&) = delete;
    [[nodiscard]] int get() const {
        return descriptor_;
    }

private:
    int descriptor_;
};
class QueueTest : public ::testing::Test {
protected:
    void SetUp() override {
        char        pattern[] = "/tmp/shm-ring-test-XXXXXX";
        const char *directory = ::mkdtemp(pattern);
        ASSERT_NE(directory, nullptr);
        directory_ = directory;
        path_      = directory_ + "/queue";
    }
    void TearDown() override {
        if (::unlink(path_.c_str()) == -1) {
            EXPECT_EQ(errno, ENOENT);
        }
        EXPECT_EQ(::rmdir(directory_.c_str()), 0);
    }
    [[nodiscard]] const std::string &path() const {
        return path_;
    }
    [[nodiscard]] Queue Producer() const {
        return Queue::open(path_, kCapacity, Role::kProducer);
    }
    [[nodiscard]] Queue Consumer() const {
        return Queue::open(path_, kCapacity, Role::kConsumer);
    }

private:
    std::string directory_;
    std::string path_;
};

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
std::array<std::byte, 1023> Payload(std::size_t sequence) {
    std::array<std::byte, 1023> payload{};
    for (std::size_t index = 0; index < payload.size(); ++index) {
        payload.at(index) = static_cast<std::byte>((sequence + index) & 0xffU);
    }
    return payload;
}
void RoundTrip(Queue &producer, Queue &consumer, std::size_t length) {
    const auto expected = Payload(length);
    ASSERT_EQ(producer.sendto(expected.data(), length), static_cast<int>(length));
    std::array<std::byte, 1023> received{};
    ASSERT_EQ(consumer.recvfrom(received.data(), received.size()), static_cast<int>(length));
    EXPECT_TRUE(std::equal(expected.begin(), expected.begin() + static_cast<std::ptrdiff_t>(length), received.begin()));
}

TEST_F(QueueTest, ConsumerCanCreateAndAllLengthsRoundTrip) {
    auto consumer = Consumer();
    auto producer = Producer();
    EXPECT_TRUE(producer.valid());
    EXPECT_EQ(producer.capacity(), kCapacity);
    for (std::size_t length = 0; length < 1024; ++length) {
        RoundTrip(producer, consumer, length);
        ASSERT_FALSE(HasFatalFailure());
    }
}
TEST_F(QueueTest, HeaderCrossesTailWithoutPadding) {
    auto producer = Producer();
    auto consumer = Consumer();
    RoundTrip(producer, consumer, 1023);
    RoundTrip(producer, consumer, 1020);  // Record starts at byte 2047.
    RoundTrip(producer, consumer, 3);
}
TEST_F(QueueTest, BodyCrossesTailWithoutPadding) {
    auto producer = Producer();
    auto consumer = Consumer();
    RoundTrip(producer, consumer, 1023);
    RoundTrip(producer, consumer, 1018);  // Header at 2045; body starts at 2047.
    RoundTrip(producer, consumer, 100);
}
TEST_F(QueueTest, EveryDataByteCanBeUsedAndFullSendLeavesQueueIntact) {
    auto producer = Producer();
    auto consumer = Consumer();
    for (std::size_t index = 0; index < kCapacity / 2; ++index) {
        ASSERT_EQ(producer.sendto(nullptr, 0), 0);
    }
    EXPECT_EQ(producer.sendto(nullptr, 0), -1);
    EXPECT_EQ(errno, EAGAIN);
    for (std::size_t index = 0; index < kCapacity / 2; ++index) {
        ASSERT_EQ(consumer.recvfrom(nullptr, 0), 0);
    }
    EXPECT_EQ(consumer.recvfrom(nullptr, 0), -1);
    EXPECT_EQ(errno, EAGAIN);
    const auto payload = Payload(1);
    ASSERT_EQ(producer.sendto(payload.data(), payload.size()), 1023);
    EXPECT_EQ(producer.sendto(payload.data(), payload.size()), -1);
    EXPECT_EQ(errno, EAGAIN);
    std::array<std::byte, 1023> received{};
    ASSERT_EQ(consumer.recvfrom(received.data(), received.size()), 1023);
    EXPECT_EQ(received, payload);
    RoundTrip(producer, consumer, 1023);
}
TEST_F(QueueTest, SmallBufferAndInvalidArgumentsDoNotConsumeOrPublish) {
    auto       producer = Producer();
    auto       consumer = Consumer();
    const auto payload  = Payload(2);
    ASSERT_EQ(producer.sendto(payload.data(), payload.size()), 1023);
    std::array<std::byte, 1023> received{};
    EXPECT_EQ(consumer.recvfrom(received.data(), received.size() - 1), -1);
    EXPECT_EQ(errno, EMSGSIZE);
    EXPECT_EQ(received, (std::array<std::byte, 1023>{}));
    ASSERT_EQ(consumer.recvfrom(received.data(), received.size()), 1023);
    EXPECT_EQ(received, payload);
    EXPECT_EQ(producer.sendto(payload.data(), 1024), -1);
    EXPECT_EQ(errno, EMSGSIZE);
    EXPECT_EQ(producer.sendto(nullptr, 1), -1);
    EXPECT_EQ(errno, EINVAL);
    EXPECT_EQ(consumer.recvfrom(nullptr, 1), -1);
    EXPECT_EQ(errno, EINVAL);
    EXPECT_EQ(consumer.recvfrom(received.data(), received.size()), -1);
    EXPECT_EQ(errno, EAGAIN);
}
TEST_F(QueueTest, RolesClosedStateAndMoveOwnership) {
    auto producer = Producer();
    auto consumer = Consumer();
    EXPECT_EQ(consumer.sendto(nullptr, 0), -1);
    EXPECT_EQ(errno, EPERM);
    EXPECT_EQ(producer.recvfrom(nullptr, 0), -1);
    EXPECT_EQ(errno, EPERM);
    static_assert(!std::is_copy_constructible_v<Queue>);
    auto moved = std::move(producer);
    EXPECT_FALSE(producer.valid());  // NOLINT(clang-analyzer-cplusplus.Move): moved-from contract.
    EXPECT_EQ(producer.capacity(), 0);
    Queue destination;
    destination = std::move(moved);
    auto *self  = &destination;
    destination = std::move(*self);
    RoundTrip(destination, consumer, 31);
    destination.close();
    destination.close();
    EXPECT_EQ(destination.sendto(nullptr, 0), -1);
    EXPECT_EQ(errno, EBADF);
    Queue closed;
    EXPECT_EQ(closed.recvfrom(nullptr, 0), -1);
    EXPECT_EQ(errno, EBADF);
}
TEST_F(QueueTest, ReopenPreservesQueuedMessagesAndRejectsCapacityMismatch) {
    {
        auto producer = Producer();
        ASSERT_EQ(producer.sendto("persist", 8), 8);
    }
    ExpectError([&] { return Queue::open(path(), 4096, Role::kConsumer); }, EINVAL);
    auto                consumer = Consumer();
    std::array<char, 8> message{};
    ASSERT_EQ(consumer.recvfrom(message.data(), message.size()), 8);
    EXPECT_STREQ(message.data(), "persist");
    auto producer = Producer();
    RoundTrip(producer, consumer, 71);
}
TEST_F(QueueTest, InvalidOpenArgumentsDoNotCreateAFile) {
    for (const auto capacity : {0ULL, 1024ULL, 3000ULL}) {
        ExpectError([&] { return Queue::open(path(), capacity, Role::kProducer); }, EINVAL);
    }
    ExpectError([&] { return Queue::open(path(), 1ULL << 63U, Role::kProducer); }, EOVERFLOW);
    // Exercise the public validation of a deliberately invalid role value.
    // NOLINTNEXTLINE(clang-analyzer-optin.core.EnumCastOutOfRange)
    const auto invalid_role = static_cast<Role>(9);
    ExpectError([&] { return Queue::open(path(), kCapacity, invalid_role); }, EINVAL);
    ExpectError([&] { return Queue::open(path(), kCapacity, Role::kProducer, std::chrono::milliseconds(-1)); }, EINVAL);
    ExpectError([] { return Queue::open("", kCapacity, Role::kProducer); }, EINVAL);
    ExpectError(
        [&] {
            return Queue::open(path(),
                               kCapacity,
                               Role::kProducer,
                               std::chrono::milliseconds(std::numeric_limits<std::int64_t>::max()));
        },
        EOVERFLOW);
    EXPECT_NE(::access(path().c_str(), F_OK), 0);
}
TEST_F(QueueTest, AbandonedInitializationTimesOutWithoutChangingFile) {
    const Descriptor descriptor(::open(path().c_str(), O_RDWR | O_CREAT | O_EXCL, 0600));
    ASSERT_NE(descriptor.get(), -1);
    ExpectError([&] { return Queue::open(path(), kCapacity, Role::kConsumer, std::chrono::milliseconds(5)); },
                ETIMEDOUT);
    ASSERT_EQ(::ftruncate(descriptor.get(), static_cast<std::int64_t>(kCapacity + kControlBytes)), 0);
    ExpectError([&] { return Queue::open(path(), kCapacity, Role::kProducer, std::chrono::milliseconds(5)); },
                ETIMEDOUT);
    ASSERT_EQ(::flock(descriptor.get(), LOCK_EX | LOCK_NB), 0);
    ExpectError([&] { return Queue::open(path(), kCapacity, Role::kProducer, std::chrono::milliseconds(5)); },
                ETIMEDOUT);
    ASSERT_EQ(::flock(descriptor.get(), LOCK_UN), 0);
    struct stat status{};
    ASSERT_EQ(::fstat(descriptor.get(), &status), 0);
    EXPECT_EQ(static_cast<std::uint64_t>(status.st_size), kCapacity + kControlBytes);
}
TEST_F(QueueTest, UnsignedCursorsWrapAcrossUint64Limit) {
    Producer().close();
    const Descriptor descriptor(::open(path().c_str(), O_RDWR));
    ASSERT_NE(descriptor.get(), -1);
    const auto start = std::numeric_limits<std::uint64_t>::max();
    ASSERT_EQ(::pwrite(descriptor.get(), &start, sizeof(start), 64), static_cast<std::int64_t>(sizeof(start)));
    ASSERT_EQ(::pwrite(descriptor.get(), &start, sizeof(start), 128), static_cast<std::int64_t>(sizeof(start)));
    auto producer = Producer();
    auto consumer = Consumer();
    RoundTrip(producer, consumer, 3);
    RoundTrip(producer, consumer, 1023);
}
TEST_F(QueueTest, CorruptLengthIsReportedWithoutTouchingOutput) {
    auto producer = Producer();
    ASSERT_EQ(producer.sendto("x", 1), 1);
    const Descriptor                   descriptor(::open(path().c_str(), O_RDWR));
    const std::array<unsigned char, 2> invalid_length{0xff, 0xff};
    ASSERT_EQ(::pwrite(descriptor.get(), invalid_length.data(), invalid_length.size(), kControlBytes), 2);
    auto                consumer = Consumer();
    std::array<char, 8> received{};
    EXPECT_EQ(consumer.recvfrom(received.data(), received.size()), -1);
    EXPECT_EQ(errno, EPROTO);
    EXPECT_EQ(received, (std::array<char, 8>{}));
}

TEST_F(QueueTest, ConcurrentOpenPreservesMessagesPublishedDuringAttachment) {
    for (int attempt = 0; attempt < 16; ++attempt) {
        Queue              producer;
        Queue              consumer;
        std::exception_ptr producer_error;
        std::exception_ptr consumer_error;
        std::atomic<int>   ready{0};
        std::atomic<bool>  start{false};
        int                sent          = -1;
        const auto         open_endpoint = [&](Role role, Queue *queue, std::exception_ptr *error) {
            ready.fetch_add(1, std::memory_order_release);
            while (!start.load(std::memory_order_acquire)) {
                std::this_thread::yield();
            }
            try {
                *queue = Queue::open(path(), kCapacity, role);
                if (role == Role::kProducer) {
                    sent = queue->sendto("race", 5);
                }
            }
            catch (...) {
                *error = std::current_exception();
            }
        };
        std::thread sender(open_endpoint, Role::kProducer, &producer, &producer_error);
        std::thread receiver(open_endpoint, Role::kConsumer, &consumer, &consumer_error);
        while (ready.load(std::memory_order_acquire) != 2) {
            std::this_thread::yield();
        }
        start.store(true, std::memory_order_release);
        sender.join();
        receiver.join();
        if (producer_error != nullptr) {
            std::rethrow_exception(producer_error);
        }
        if (consumer_error != nullptr) {
            std::rethrow_exception(consumer_error);
        }
        ASSERT_EQ(sent, 5);
        std::array<char, 5> message{};
        ASSERT_EQ(consumer.recvfrom(message.data(), message.size()), 5);
        EXPECT_STREQ(message.data(), "race");
        producer.close();
        consumer.close();
        ASSERT_EQ(::unlink(path().c_str()), 0);
    }
}

constexpr std::size_t kProcessMessages = 12000;
int                   Exchange(Queue &queue, Role role) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    for (std::size_t sequence = 0; sequence < kProcessMessages; ++sequence) {
        const auto                  length   = (sequence * 37) % 1024;
        const auto                  expected = Payload(sequence);
        std::array<std::byte, 1023> received{};
        for (;;) {
            const int result = role == Role::kProducer ? queue.sendto(expected.data(), length)
                                                       : queue.recvfrom(received.data(), received.size());
            if (result >= 0) {
                if (result != static_cast<int>(length) ||
                    (role == Role::kConsumer &&
                     !std::equal(expected.begin(), expected.begin() + result, received.begin()))) {
                    return 1;
                }
                break;
            }
            if (errno != EAGAIN || std::chrono::steady_clock::now() >= deadline) {
                return 2;
            }
            std::this_thread::yield();
        }
    }
    return 0;
}
TEST(ShmRingProcessHelper, Exchange) {
    const char *resource = std::getenv("NET_TEST_RING_PATH");
    const char *mode     = std::getenv("NET_TEST_RING_ROLE");
    if (resource == nullptr || mode == nullptr) {
        GTEST_SKIP() << "executed only by independent-process tests";
    }
    const auto role  = std::string(mode) == "producer" ? Role::kProducer : Role::kConsumer;
    auto       queue = Queue::open(resource, kCapacity, role);
    EXPECT_EQ(Exchange(queue, role), 0);
}
void RunPeer(const std::string &path, Role parent_role, bool create_before_fork) {
    Queue parent;
    if (create_before_fork) {
        parent = Queue::open(path, kCapacity, parent_role);
    }
    const auto child = ::fork();
    ASSERT_NE(child, -1);
    if (child == 0) {
        const char *role = parent_role == Role::kProducer ? "consumer" : "producer";
        if (::setenv("NET_TEST_RING_PATH", path.c_str(), 1) == -1 || ::setenv("NET_TEST_RING_ROLE", role, 1) == -1) {
            ::_exit(126);
        }
        ::execl("/proc/self/exe", "ring-peer", "--gtest_filter=ShmRingProcessHelper.Exchange", nullptr);
        ::_exit(127);
    }
    try {
        if (!create_before_fork) {
            parent = Queue::open(path, kCapacity, parent_role);
        }
    }
    catch (...) {
        (void)::kill(child, SIGKILL);
        while (::waitpid(child, nullptr, 0) == -1 && errno == EINTR) {
        }
        throw;
    }
    const int result = Exchange(parent, parent_role);
    if (result != 0) {
        (void)::kill(child, SIGKILL);
    }
    int status = 0;
    while (::waitpid(child, &status, 0) == -1 && errno == EINTR) {
    }
    ASSERT_EQ(result, 0);
    ASSERT_TRUE(WIFEXITED(status));
    EXPECT_EQ(WEXITSTATUS(status), 0);
}
TEST_F(QueueTest, IndependentProcessesStartTogetherAndExchangeVariableMessages) {
    RunPeer(path(), Role::kProducer, false);
}
TEST_F(QueueTest, IndependentConsumerCreatesBeforeProducerStarts) {
    RunPeer(path(), Role::kConsumer, true);
}
}  // namespace
// NOLINTEND(readability-function-cognitive-complexity)
