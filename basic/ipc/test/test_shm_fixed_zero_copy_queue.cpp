#include "../shm_fixed_queue.h"
#include "../shm_fixed_zero_copy_queue.h"

#include <fcntl.h>
#include <gtest/gtest.h>
#include <signal.h>  // NOLINT(modernize-deprecated-headers): POSIX kill.
#include <stdlib.h>  // NOLINT(modernize-deprecated-headers): POSIX process helpers.
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include <array>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <limits>
#include <stdexcept>
#include <string>
#include <system_error>
#include <thread>
#include <type_traits>
#include <utility>

// GTest assertion macros inflate complexity in straight-line test bodies.
// NOLINTBEGIN(readability-function-cognitive-complexity)
namespace {
struct Message {
    std::uint64_t             sequence;
    std::uint64_t             checksum;
    std::array<std::byte, 48> payload;
    friend bool               operator==(const Message &left, const Message &right) {
        return left.sequence == right.sequence && left.checksum == right.checksum && left.payload == right.payload;
    }
};
static_assert(sizeof(Message) == 64);
using Queue = basic::ShmFixedZeroCopyQueue<Message, 8>;
using Role  = Queue::Role;

bool IsAligned(const void *address, std::size_t alignment) {
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast): inspect mapped address alignment.
    return reinterpret_cast<std::uintptr_t>(address) % alignment == 0;
}

// Compatibility helpers are confined to common protocol regression tests.
// Zero-copy-specific tests below write/read the shared fields directly.
template <typename Q, typename Value>
int Send(Q &queue, const Value &value) {
    return queue.send([&](auto &slot) { slot = value; });
}
template <typename Q, typename Value>
int Receive(Q &queue, Value &value) {
    return queue.recv([&](const auto &slot) { value = slot; });
}
Message MakeMessage(std::uint64_t sequence) {
    Message message{};
    message.sequence = sequence;
    for (std::size_t index = 0; index < message.payload.size(); ++index) {
        const auto value          = (sequence + index) & 0xffU;
        message.payload.at(index) = static_cast<std::byte>(value);
        message.checksum += value;
    }
    return message;
}
class Descriptor {
public:
    explicit Descriptor(int value) : value_(value) {
    }
    ~Descriptor() {
        if (value_ != -1) {
            (void)::close(value_);
        }
    }
    Descriptor(const Descriptor &)             = delete;
    Descriptor &operator=(const Descriptor &)  = delete;
    Descriptor(Descriptor &&)                  = delete;
    Descriptor       &operator=(Descriptor &&) = delete;
    [[nodiscard]] int get() const {
        return value_;
    }

private:
    int value_;
};
class ZeroCopyQueueTest : public ::testing::Test {
protected:
    void SetUp() override {
        char        pattern[] = "/tmp/shm-zero-copy-test-XXXXXX";
        const char *directory = ::mkdtemp(pattern);
        ASSERT_NE(directory, nullptr);
        directory_ = directory;
        path_      = directory_ + "/queue";
    }
    void TearDown() override {
        for (const auto &name : {path_, path_ + "-other"}) {
            if (::unlink(name.c_str()) == -1) {
                EXPECT_EQ(errno, ENOENT);
            }
        }
        EXPECT_EQ(::rmdir(directory_.c_str()), 0);
    }
    [[nodiscard]] const std::string &path() const {
        return path_;
    }
    [[nodiscard]] Queue Producer() const {
        return Queue::open(path_, Role::kProducer);
    }
    [[nodiscard]] Queue Consumer() const {
        return Queue::open(path_, Role::kConsumer);
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
TEST_F(ZeroCopyQueueTest, ConsumerCreatesAndFixedMessagesRoundTrip) {
    auto consumer = Consumer();
    auto producer = Producer();
    EXPECT_EQ(producer.capacity(), 8);
    for (std::uint64_t sequence = 0; sequence < 2000; ++sequence) {
        const auto expected = MakeMessage(sequence);
        ASSERT_EQ(Send(producer, expected), sizeof(Message));
        Message received{};
        ASSERT_EQ(Receive(consumer, received), sizeof(Message));
        ASSERT_EQ(received, expected);
    }
    struct stat status{};
    ASSERT_EQ(::stat(path().c_str(), &status), 0);
    EXPECT_EQ(static_cast<std::size_t>(status.st_size), 192 + (8 * sizeof(Message)));
}
TEST_F(ZeroCopyQueueTest, AllSlotsAreUsableAndWrapKeepsFifoOrder) {
    auto producer = Producer();
    auto consumer = Consumer();
    for (std::uint64_t sequence = 0; sequence < 8; ++sequence) {
        ASSERT_EQ(Send(producer, MakeMessage(sequence)), sizeof(Message));
    }
    EXPECT_EQ(Send(producer, MakeMessage(999)), -1);
    EXPECT_EQ(errno, EAGAIN);
    Message received{};
    for (std::uint64_t sequence = 0; sequence < 3; ++sequence) {
        ASSERT_EQ(Receive(consumer, received), sizeof(Message));
        EXPECT_EQ(received, MakeMessage(sequence));
    }
    for (std::uint64_t sequence = 8; sequence < 11; ++sequence) {
        ASSERT_EQ(Send(producer, MakeMessage(sequence)), sizeof(Message));
    }
    for (std::uint64_t sequence = 3; sequence < 11; ++sequence) {
        ASSERT_EQ(Receive(consumer, received), sizeof(Message));
        EXPECT_EQ(received, MakeMessage(sequence));
    }
    EXPECT_EQ(Receive(consumer, received), -1);
    EXPECT_EQ(errno, EAGAIN);
    EXPECT_EQ(received, MakeMessage(10));
}
TEST_F(ZeroCopyQueueTest, RolesCloseAndMovesPreserveOwnership) {
    auto    producer = Producer();
    auto    consumer = Consumer();
    Message message  = MakeMessage(7);
    EXPECT_EQ(Send(consumer, message), -1);
    EXPECT_EQ(errno, EPERM);
    EXPECT_EQ(Receive(producer, message), -1);
    EXPECT_EQ(errno, EPERM);
    auto moved = std::move(producer);
    EXPECT_FALSE(producer.valid());  // NOLINT(clang-analyzer-cplusplus.Move): documented source state.
    EXPECT_EQ(producer.capacity(), 0);
    auto destination = Queue::open(path() + "-other", Role::kProducer);
    destination      = std::move(moved);
    ASSERT_EQ(Send(destination, message), sizeof(Message));
    Message received{};
    ASSERT_EQ(Receive(consumer, received), sizeof(Message));
    EXPECT_EQ(received, message);
    auto *self  = &destination;
    destination = std::move(*self);
    EXPECT_TRUE(destination.valid());
    (void)destination.close();
    (void)destination.close();
    EXPECT_EQ(Send(destination, message), -1);
    EXPECT_EQ(errno, EBADF);
    Queue closed;
    EXPECT_EQ(Receive(closed, received), -1);
    EXPECT_EQ(errno, EBADF);
    static_assert(!std::is_copy_constructible_v<Queue>);
    static_assert(std::is_move_constructible_v<Queue>);
}
TEST_F(ZeroCopyQueueTest, ReopeningPreservesUnreadMessages) {
    {
        auto producer = Producer();
        ASSERT_EQ(Send(producer, MakeMessage(77)), sizeof(Message));
    }
    auto    consumer = Consumer();
    Message received{};
    ASSERT_EQ(Receive(consumer, received), sizeof(Message));
    EXPECT_EQ(received, MakeMessage(77));
}
TEST_F(ZeroCopyQueueTest, CapacityAndMessageLayoutMismatchesDoNotResetQueue) {
    auto producer = Producer();
    ASSERT_EQ(Send(producer, MakeMessage(9)), sizeof(Message));
    using OtherCapacity = basic::ShmFixedZeroCopyQueue<Message, 16>;
    ExpectError([&] { return OtherCapacity::open(path(), OtherCapacity::Role::kConsumer); }, EINVAL);
    struct alignas(64) OtherAlignment {
        std::array<std::byte, 64> bytes;
    };
    using AlignedQueue = basic::ShmFixedZeroCopyQueue<OtherAlignment, 8>;
    ExpectError([&] { return AlignedQueue::open(path(), AlignedQueue::Role::kConsumer); }, EPROTO);
    // Same total data bytes but a different slot count and message size.
    using OtherSize = basic::ShmFixedZeroCopyQueue<std::array<std::byte, 128>, 4>;
    ExpectError([&] { return OtherSize::open(path(), OtherSize::Role::kConsumer); }, EPROTO);
    auto    consumer = Consumer();
    Message received{};
    ASSERT_EQ(Receive(consumer, received), sizeof(Message));
    EXPECT_EQ(received, MakeMessage(9));
}
TEST_F(ZeroCopyQueueTest, UnalignedSlotsAndLargeStructuresUseExactObjectSize) {
    using Packed                            = basic::ShmFixedZeroCopyQueue<std::array<std::byte, 3>, 8>;
    auto                           sender   = Packed::open(path(), Packed::Role::kProducer);
    auto                           receiver = Packed::open(path(), Packed::Role::kConsumer);
    const std::array<std::byte, 3> value{std::byte{1}, std::byte{2}, std::byte{3}};
    for (int count = 0; count < 20; ++count) {
        EXPECT_EQ(Send(sender, value), 3);
        std::array<std::byte, 3> received{};
        EXPECT_EQ(Receive(receiver, received), 3);
        EXPECT_EQ(received, value);
    }
    struct alignas(128) Large {
        std::array<std::byte, 2048> bytes;
    };
    using LargeQueue     = basic::ShmFixedZeroCopyQueue<Large, 2>;
    auto  large_sender   = LargeQueue::open(path() + "-other", LargeQueue::Role::kProducer);
    auto  large_receiver = LargeQueue::open(path() + "-other", LargeQueue::Role::kConsumer);
    Large input{};
    input.bytes.fill(std::byte{0x5a});
    Large output{};
    EXPECT_EQ(Send(large_sender, input), sizeof(Large));
    EXPECT_EQ(Receive(large_receiver, output), sizeof(Large));
    EXPECT_EQ(output.bytes, input.bytes);
}
TEST_F(ZeroCopyQueueTest, AbandonedInitializationAndLockHaveBoundedWait) {
    const Descriptor file(::open(path().c_str(), O_RDWR | O_CREAT | O_EXCL, 0600));
    ASSERT_NE(file.get(), -1);
    ExpectError([&] { return Queue::open(path(), Role::kProducer, std::chrono::milliseconds(5)); }, ETIMEDOUT);
    ASSERT_EQ(::ftruncate(file.get(), 192 + (8 * sizeof(Message))), 0);
    ExpectError([&] { return Queue::open(path(), Role::kConsumer, std::chrono::milliseconds(5)); }, ETIMEDOUT);
    ASSERT_EQ(::flock(file.get(), LOCK_EX | LOCK_NB), 0);
    ExpectError([&] { return Queue::open(path(), Role::kProducer, std::chrono::milliseconds(5)); }, ETIMEDOUT);
    ASSERT_EQ(::flock(file.get(), LOCK_UN), 0);
    struct stat status{};
    ASSERT_EQ(::fstat(file.get(), &status), 0);
    EXPECT_EQ(static_cast<std::size_t>(status.st_size), 192 + (8 * sizeof(Message)));
}
TEST_F(ZeroCopyQueueTest, InvalidOpenArgumentsDoNotCreateFiles) {
    ExpectError([] { return Queue::open("", Role::kProducer); }, EINVAL);
    ExpectError([&] { return Queue::open(path(), Role::kProducer, std::chrono::milliseconds(-1)); }, EINVAL);
    ExpectError(
        [&] {
            return Queue::open(
                path(), Role::kConsumer, std::chrono::milliseconds(std::numeric_limits<std::int64_t>::max()));
        },
        EOVERFLOW);
    // NOLINTNEXTLINE(clang-analyzer-optin.core.EnumCastOutOfRange)
    const auto invalid_role = static_cast<Role>(9);
    ExpectError([&] { return Queue::open(path(), invalid_role); }, EINVAL);
    EXPECT_EQ(::access(path().c_str(), F_OK), -1);
}
TEST_F(ZeroCopyQueueTest, UnsignedCursorWrapStillUsesEverySlot) {
    (void)Producer().close();
    const Descriptor file(::open(path().c_str(), O_RDWR));
    const auto       start = std::numeric_limits<std::uint64_t>::max() - 3;
    ASSERT_EQ(::pwrite(file.get(), &start, sizeof(start), 64), sizeof(start));
    ASSERT_EQ(::pwrite(file.get(), &start, sizeof(start), 128), sizeof(start));
    auto producer = Producer();
    auto consumer = Consumer();
    for (std::uint64_t index = 0; index < 8; ++index) {
        ASSERT_EQ(Send(producer, MakeMessage(index)), sizeof(Message));
    }
    EXPECT_EQ(Send(producer, MakeMessage(8)), -1);
    EXPECT_EQ(errno, EAGAIN);
    for (std::uint64_t index = 0; index < 8; ++index) {
        Message received{};
        ASSERT_EQ(Receive(consumer, received), sizeof(Message));
        EXPECT_EQ(received, MakeMessage(index));
    }
}
TEST_F(ZeroCopyQueueTest, InvalidCursorDistanceFailsAttachment) {
    (void)Producer().close();
    const Descriptor    file(::open(path().c_str(), O_RDWR));
    const std::uint64_t invalid = 9;
    ASSERT_EQ(::pwrite(file.get(), &invalid, sizeof(invalid), 64), sizeof(invalid));
    ExpectError([&] { return Consumer(); }, EPROTO);
}
TEST_F(ZeroCopyQueueTest, InvalidReadCursorPreventsSendingWithoutInvokingCallback) {
    auto producer = Producer();
    for (std::uint64_t sequence = 0; sequence < producer.capacity(); ++sequence) {
        ASSERT_EQ(Send(producer, MakeMessage(sequence)), sizeof(Message));
    }
    const Descriptor    file(::open(path().c_str(), O_RDWR));
    const std::uint64_t invalid = producer.capacity() + 1;
    ASSERT_NE(file.get(), -1);
    ASSERT_EQ(::pwrite(file.get(), &invalid, sizeof(invalid), 128), sizeof(invalid));
    EXPECT_EQ(producer.send([](Message &) { ADD_FAILURE() << "invalid read cursor"; }), -1);
    EXPECT_EQ(errno, EPROTO);
    EXPECT_EQ(producer.send([](Message &) { ADD_FAILURE() << "cached invalid read cursor"; }), -1);
    EXPECT_EQ(errno, EPROTO);
}
TEST_F(ZeroCopyQueueTest, InvalidWriteCursorPreventsReceivingWithoutInvokingCallback) {
    auto                consumer = Consumer();
    const Descriptor    file(::open(path().c_str(), O_RDWR));
    const std::uint64_t invalid = consumer.capacity() + 1;
    ASSERT_NE(file.get(), -1);
    ASSERT_EQ(::pwrite(file.get(), &invalid, sizeof(invalid), 64), sizeof(invalid));
    EXPECT_EQ(consumer.recv([](const Message &) { ADD_FAILURE() << "invalid write cursor"; }), -1);
    EXPECT_EQ(errno, EPROTO);
    EXPECT_EQ(consumer.recv([](const Message &) { ADD_FAILURE() << "cached invalid write cursor"; }), -1);
    EXPECT_EQ(errno, EPROTO);
}
TEST_F(ZeroCopyQueueTest, ConcurrentOpenKeepsAlreadyPublishedRecords) {
    for (int attempt = 0; attempt < 12; ++attempt) {
        Queue              producer;
        Queue              consumer;
        std::exception_ptr sender_error;
        std::exception_ptr receiver_error;
        std::atomic<int>   ready{0};
        std::atomic<bool>  start{false};
        int                sent = -1;
        const auto         open = [&](Role role, Queue *queue, std::exception_ptr *error) {
            ready.fetch_add(1, std::memory_order_release);
            while (!start.load(std::memory_order_acquire)) {
                std::this_thread::yield();
            }
            try {
                *queue = Queue::open(path(), role);
                if (role == Role::kProducer) {
                    sent = Send(*queue, MakeMessage(123));
                }
            }
            catch (...) {
                *error = std::current_exception();
            }
        };
        std::thread sender(open, Role::kProducer, &producer, &sender_error);
        std::thread receiver(open, Role::kConsumer, &consumer, &receiver_error);
        while (ready.load(std::memory_order_acquire) != 2) {
            std::this_thread::yield();
        }
        start.store(true, std::memory_order_release);
        sender.join();
        receiver.join();
        if (sender_error != nullptr) {
            std::rethrow_exception(sender_error);
        }
        if (receiver_error != nullptr) {
            std::rethrow_exception(receiver_error);
        }
        ASSERT_EQ(sent, sizeof(Message));
        Message received{};
        ASSERT_EQ(Receive(consumer, received), sizeof(Message));
        EXPECT_EQ(received, MakeMessage(123));
        (void)producer.close();
        (void)consumer.close();
        ASSERT_EQ(::unlink(path().c_str()), 0);
    }
}
constexpr std::uint64_t kProcessMessages = 20000;
int                     Exchange(Queue &queue, Role role) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    for (std::uint64_t sequence = 0; sequence < kProcessMessages; ++sequence) {
        const auto expected = MakeMessage(sequence);
        bool       correct  = true;
        for (;;) {
            const int result = role == Role::kProducer
                                   ? queue.send([&](Message &slot) {
                                         slot.sequence = sequence;
                                         slot.checksum = 0;
                                         for (std::size_t index = 0; index < slot.payload.size(); ++index) {
                                             const auto value       = (sequence + index) & 0xffU;
                                             slot.payload.at(index) = static_cast<std::byte>(value);
                                             slot.checksum += value;
                                         }
                                     })
                                   : queue.recv([&](const Message &slot) { correct = slot == expected; });
            if (result >= 0) {
                if (result != sizeof(Message) || !correct) {
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
TEST(ShmZeroCopyProcessHelper, Exchange) {
    const char *path = std::getenv("NET_TEST_ZERO_COPY_QUEUE_PATH");
    const char *mode = std::getenv("NET_TEST_ZERO_COPY_QUEUE_ROLE");
    if (path == nullptr || mode == nullptr) {
        GTEST_SKIP() << "only used by independent-process tests";
    }
    const auto role  = std::string(mode) == "producer" ? Role::kProducer : Role::kConsumer;
    auto       queue = Queue::open(path, role);
    EXPECT_EQ(Exchange(queue, role), 0);
}
void RunPeer(const std::string &path, Role parent_role, bool create_first) {
    Queue parent;
    if (create_first) {
        parent = Queue::open(path, parent_role);
    }
    const auto child = ::fork();
    ASSERT_NE(child, -1);
    if (child == 0) {
        const char *mode = parent_role == Role::kProducer ? "consumer" : "producer";
        if (::setenv("NET_TEST_ZERO_COPY_QUEUE_PATH", path.c_str(), 1) == -1 ||
            ::setenv("NET_TEST_ZERO_COPY_QUEUE_ROLE", mode, 1) == -1) {
            ::_exit(126);
        }
        ::execl("/proc/self/exe", "zero-copy-queue-peer", "--gtest_filter=ShmZeroCopyProcessHelper.Exchange", nullptr);
        ::_exit(127);
    }
    try {
        if (!create_first) {
            parent = Queue::open(path, parent_role);
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
    int  status = 0;
    auto waited = 0;
    while ((waited = ::waitpid(child, &status, 0)) == -1 && errno == EINTR) {
    }
    ASSERT_EQ(waited, child);
    ASSERT_EQ(result, 0);
    ASSERT_TRUE(WIFEXITED(status));
    EXPECT_EQ(WEXITSTATUS(status), 0);
}
TEST_F(ZeroCopyQueueTest, IndependentProcessesExchangeFixedStructures) {
    RunPeer(path(), Role::kProducer, false);
}
TEST_F(ZeroCopyQueueTest, IndependentConsumerStartsBeforeProducer) {
    RunPeer(path(), Role::kConsumer, true);
}

TEST_F(ZeroCopyQueueTest, CallbacksAccessSharedBytesAndObservePublicationBoundary) {
    auto             producer = Producer();
    auto             consumer = Consumer();
    const Descriptor file(::open(path().c_str(), O_RDWR));
    ASSERT_NE(file.get(), -1);
    const std::uint64_t initial = 123;
    EXPECT_EQ(producer.send([&](Message &slot) {
        slot.sequence = initial;
        slot.checksum = 0;
        slot.payload.fill(std::byte{0});
        std::uint64_t stored = 0;
        ASSERT_EQ(::pread(file.get(), &stored, sizeof(stored), 192), sizeof(stored));
        EXPECT_EQ(stored, initial);
        EXPECT_EQ(consumer.recv([](const Message &) { ADD_FAILURE() << "unpublished slot"; }), -1);
        EXPECT_EQ(errno, EAGAIN);
    }),
              sizeof(Message));
    EXPECT_EQ(consumer.recv([&](const Message &slot) {
        EXPECT_EQ(slot.sequence, initial);
        // A change through the file must be visible through the callback's
        // reference. A callback receiving a private copy would still see 123.
        const std::uint64_t changed = 456;
        ASSERT_EQ(::pwrite(file.get(), &changed, sizeof(changed), 192), sizeof(changed));
        EXPECT_EQ(slot.sequence, changed);
    }),
              sizeof(Message));
}
TEST_F(ZeroCopyQueueTest, ExceptionsKeepCursorsAndAllowRetry) {
    auto producer = Producer();
    auto consumer = Consumer();
    EXPECT_THROW((void)producer.send([](Message &slot) {
        slot.sequence = 9;
        throw std::runtime_error("writer failed");
    }),
                 std::runtime_error);
    EXPECT_EQ(consumer.recv([](const Message &) { ADD_FAILURE(); }), -1);
    EXPECT_EQ(errno, EAGAIN);
    ASSERT_EQ(producer.send([](Message &slot) {
        slot.sequence = 42;
        slot.checksum = 0;
        slot.payload.fill(std::byte{0x2a});
    }),
              sizeof(Message));
    EXPECT_THROW((void)consumer.recv([](const Message &slot) {
        EXPECT_EQ(slot.sequence, 42);
        throw std::runtime_error("reader failed");
    }),
                 std::runtime_error);
    ASSERT_EQ(consumer.recv([](const Message &slot) {
        EXPECT_EQ(slot.sequence, 42);
        EXPECT_EQ(slot.payload.front(), std::byte{0x2a});
    }),
              sizeof(Message));
    EXPECT_EQ(consumer.recv([](const Message &) { ADD_FAILURE(); }), -1);
    EXPECT_EQ(errno, EAGAIN);
}
TEST_F(ZeroCopyQueueTest, ActiveCallbacksRejectReentryCloseAndMoves) {
    auto       producer = Producer();
    auto       consumer = Consumer();
    Queue      destination;
    const auto write = [&](Message &slot) {
        EXPECT_EQ(producer.send([](Message &) { ADD_FAILURE(); }), -1);
        EXPECT_EQ(errno, EBUSY);
        EXPECT_EQ(producer.close(), -1);
        EXPECT_EQ(errno, EBUSY);
        ExpectError([&] { return Queue(std::move(producer)); }, EBUSY);
        // Rejected move construction leaves the source intact.
        // NOLINTNEXTLINE(clang-analyzer-cplusplus.Move)
        ExpectError([&] { destination = std::move(producer); }, EBUSY);
        slot = MakeMessage(7);
    };
    EXPECT_EQ(producer.send(write), sizeof(Message));
    EXPECT_EQ(consumer.recv([&](const Message &slot) {
        EXPECT_EQ(slot, MakeMessage(7));
        EXPECT_EQ(consumer.recv([](const Message &) { ADD_FAILURE(); }), -1);
        EXPECT_EQ(errno, EBUSY);
        EXPECT_EQ(consumer.close(), -1);
        EXPECT_EQ(errno, EBUSY);
        ExpectError([&] { consumer = std::move(destination); }, EBUSY);
    }),
              sizeof(Message));
    EXPECT_TRUE(producer.valid());
    EXPECT_TRUE(consumer.valid());
    EXPECT_FALSE(destination.valid());
}
TEST_F(ZeroCopyQueueTest, HeldReadSlotCannotBeReusedUntilCallbackReturns) {
    auto producer = Producer();
    auto consumer = Consumer();
    for (std::uint64_t sequence = 0; sequence < 8; ++sequence) {
        ASSERT_EQ(Send(producer, MakeMessage(sequence)), sizeof(Message));
    }
    ASSERT_EQ(consumer.recv([&](const Message &slot) {
        EXPECT_EQ(slot.sequence, 0);
        EXPECT_EQ(producer.send([](Message &) { ADD_FAILURE(); }), -1);
        EXPECT_EQ(errno, EAGAIN);
        EXPECT_EQ(slot.sequence, 0);
    }),
              sizeof(Message));
    ASSERT_EQ(producer.send([](Message &slot) { slot = MakeMessage(8); }), sizeof(Message));
    for (std::uint64_t sequence = 1; sequence <= 8; ++sequence) {
        ASSERT_EQ(consumer.recv([&](const Message &slot) { EXPECT_EQ(slot, MakeMessage(sequence)); }), sizeof(Message));
    }
}
TEST_F(ZeroCopyQueueTest, OverAlignedSlotsAreDirectlyAccessibleWithoutPerSlotPadding) {
    struct alignas(128) Aligned {
        std::array<std::uint64_t, 16> values;
    };
    using AlignedQueue = basic::ShmFixedZeroCopyQueue<Aligned, 2>;
    auto producer      = AlignedQueue::open(path(), AlignedQueue::Role::kProducer);
    auto consumer      = AlignedQueue::open(path(), AlignedQueue::Role::kConsumer);
    for (std::uint64_t sequence = 0; sequence < 20; ++sequence) {
        ASSERT_EQ(producer.send([&](Aligned &slot) {
            EXPECT_TRUE(IsAligned(&slot, alignof(Aligned)));
            slot.values.fill(sequence);
        }),
                  sizeof(Aligned));
        ASSERT_EQ(consumer.recv([&](const Aligned &slot) {
            EXPECT_TRUE(IsAligned(&slot, alignof(Aligned)));
            for (const auto value : slot.values) {
                EXPECT_EQ(value, sequence);
            }
        }),
                  sizeof(Aligned));
    }
    struct stat status{};
    ASSERT_EQ(::stat(path().c_str(), &status), 0);
    EXPECT_EQ(status.st_size, 256 + (2 * sizeof(Aligned)));
}
TEST_F(ZeroCopyQueueTest, AlignmentAbovePageSizeIsRejectedBeforeCreation) {
    struct alignas(65536) TooAligned {
        std::uint64_t sequence;
    };
    if (::sysconf(_SC_PAGESIZE) >= alignof(TooAligned)) {
        GTEST_SKIP() << "requires pages smaller than 64 KiB";
    }
    using AlignedQueue = basic::ShmFixedZeroCopyQueue<TooAligned, 2>;
    ExpectError([&] { return AlignedQueue::open(path(), AlignedQueue::Role::kProducer); }, EINVAL);
    EXPECT_EQ(::access(path().c_str(), F_OK), -1);
    EXPECT_EQ(errno, ENOENT);
}
TEST_F(ZeroCopyQueueTest, CopyingQueueFileIsRejectedWithoutResettingRecords) {
    using CopyQueue = basic::ShmFixedQueue<Message, 8>;
    auto copying    = CopyQueue::open(path(), CopyQueue::Role::kProducer);
    ASSERT_EQ(copying.sendto(MakeMessage(99)), sizeof(Message));
    ExpectError([&] { return Consumer(); }, EPROTO);
    auto    receiver = CopyQueue::open(path(), CopyQueue::Role::kConsumer);
    Message received{};
    ASSERT_EQ(receiver.recvfrom(received), sizeof(Message));
    EXPECT_EQ(received, MakeMessage(99));
}
TEST_F(ZeroCopyQueueTest, ZeroCopyFormatRejectsCopyingEndpointWithoutLosingRecord) {
    auto producer = Producer();
    ASSERT_EQ(producer.send([](Message &slot) { slot = MakeMessage(55); }), sizeof(Message));
    using CopyQueue = basic::ShmFixedQueue<Message, 8>;
    ExpectError([&] { return CopyQueue::open(path(), CopyQueue::Role::kConsumer); }, EPROTO);
    auto consumer = Consumer();
    ASSERT_EQ(consumer.recv([](auto &slot) {
        static_assert(std::is_const_v<std::remove_reference_t<decltype(slot)>>);
        EXPECT_EQ(slot, MakeMessage(55));
    }),
              sizeof(Message));
}
}  // namespace
// NOLINTEND(readability-function-cognitive-complexity)
