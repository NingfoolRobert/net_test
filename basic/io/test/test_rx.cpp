#include <gtest/gtest.h>

#include <cerrno>
#include <cstring>
#include <limits>
#include <string>
#include <type_traits>

#include "../io.h"
#include "../rtx.h"
#include "../rx.h"
#include "../tx.h"

namespace {
struct Options {
  int* closes = nullptr;
};
struct Receiver : detail::io::rx<Receiver, Options> {
  ~Receiver() { close(); }
  bool open_impl() { return true; }
  void close_impl() {
    if (opt_.closes) ++*opt_.closes;
  }
  int recv_impl(char* data, std::size_t len) {
    if (len < 3) return -1;
    std::memcpy(data, "abc", 3);
    return 3;
  }
  int recv_from_impl(char* data, std::size_t len, void*& source) {
    ++recv_from_calls;
    source = this;
    return recv_impl(data, len);
  }
  int recv_from_calls = 0;
};
struct Sender : detail::io::tx<Sender, Options> {
  bool open_impl() { return true; }
  void close_impl() {
    if (opt_.closes) ++*opt_.closes;
  }
  int send_impl(const void* data, std::size_t len) {
    sent.assign(static_cast<const char*>(data), len);
    return static_cast<int>(len);
  }
  std::string sent;
};
struct Duplex : detail::io::rtx<Duplex, Options> {
  ~Duplex() { close(); }
  bool open_impl() { return true; }
  void close_impl() {
    if (opt_.closes) ++*opt_.closes;
  }
  int recv_impl(char* data, std::size_t len) {
    if (len < sent.size()) return -1;
    std::memcpy(data, sent.data(), sent.size());
    return static_cast<int>(sent.size());
  }
  int send_impl(const void* data, std::size_t len) {
    sent.assign(static_cast<const char*>(data), len);
    return static_cast<int>(len);
  }
  std::string sent;
};
}  // namespace
TEST(BasicIo, ReceiverForwardsAndCloses) {
  int closes = 0;
  {
    Receiver receiver;
    detail::io::rx<Receiver, Options>& base = receiver;
    base.set_opt({&closes});
    EXPECT_TRUE(base.open());
    char data[3];
    EXPECT_EQ(base.recv(data, sizeof(data)), 3);
    EXPECT_EQ(std::string(data, 3), "abc");
  }
  EXPECT_EQ(closes, 1);
}
TEST(BasicIo, SenderForwardsSend) {
  Sender sender;
  detail::io::tx<Sender, Options>& base = sender;
  EXPECT_TRUE(base.open());
  EXPECT_EQ(base.send("hello", 5), 5);
  EXPECT_EQ(sender.sent, "hello");
}
TEST(BasicIo, DuplexForwardsSendAndRecv) {
  int closes = 0;
  {
    Duplex duplex;
    detail::io::rtx<Duplex, Options>& base = duplex;
    Options options{&closes};
    base.set_opt(options);
    EXPECT_TRUE(base.open());
    EXPECT_EQ(base.send("hello", 5), 5);
    char data[5];
    EXPECT_EQ(base.recv(data, sizeof(data)), 5);
    EXPECT_EQ(std::string(data, sizeof(data)), "hello");
  }
  EXPECT_EQ(closes, 1);
}

namespace {
struct Faults {
  bool fail_open = true, fail_io = true, fail_close = false;
  int calls = 0;
  bool open_impl() {
    errno = fail_open ? EACCES : EBUSY;
    return !fail_open;
  }
  void close_impl() { errno = fail_close ? EBADF : 0; }
  int operation() {
    ++calls;
    errno = fail_io ? EAGAIN : EBUSY;
    return fail_io ? -1 : 0;
  }
};
struct FaultyRx : detail::io::rx<FaultyRx, Options>, Faults {
  int recv_impl(char*, std::size_t) { return operation(); }
};
struct FaultyTx : detail::io::tx<FaultyTx, Options>, Faults {
  int send_impl(const void*, std::size_t) { return operation(); }
};
struct FaultyRtx : detail::io::rtx<FaultyRtx, Options>, Faults {
  int send_impl(const void*, std::size_t) { return operation(); }
  int recv_impl(char*, std::size_t) { return operation(); }
};
template <typename Io, typename Operation>
void check_error_snapshot(Io& io, Operation operation) {
  static_assert(
      std::is_same_v<decltype(io.error_code()), detail::io::ErrorCode>);
  EXPECT_EQ(io.last_error(), 0);
  errno = ENOENT;
  EXPECT_FALSE(io.open());
  EXPECT_EQ(io.last_error(), EACCES);
  io.fail_open = false;
  EXPECT_TRUE(io.open());
  EXPECT_EQ(io.last_error(), 0);
  EXPECT_EQ(operation(), -1);
  EXPECT_EQ(io.last_error(), EAGAIN);
  errno = ENOENT;  // Unrelated errno updates do not affect the saved error.
  EXPECT_EQ(io.last_error(), EAGAIN);
  const auto snapshot = io.error_code();
  EXPECT_EQ(snapshot, detail::io::ErrorCode(EAGAIN));
  EXPECT_FALSE(io.error_code().message().empty());
  io.fail_io = false;
  EXPECT_EQ(operation(), 0);  // EOF/zero bytes is a successful operation.
  EXPECT_EQ(io.last_error(), 0);
  EXPECT_EQ(snapshot.value(), EAGAIN);
  io.fail_close = true;
  io.close();
  EXPECT_EQ(io.last_error(), EBADF);
  io.fail_close = false;
  io.close();
  EXPECT_EQ(io.last_error(), 0);
}
}  // namespace
TEST(BasicIoErrors, RxRecordsAndClearsPerInstanceError) {
  FaultyRx io;
  check_error_snapshot(io, [&] { return io.recv(nullptr, 0); });
  EXPECT_EQ(io.recv(nullptr, -1), -1);
  EXPECT_EQ(io.last_error(), EOVERFLOW);
}
TEST(BasicIoErrors, TxRecordsAndClearsPerInstanceError) {
  FaultyTx io;
  check_error_snapshot(io, [&] { return io.send(nullptr, 0); });
  EXPECT_EQ(io.send(nullptr, -1), -1);
  EXPECT_EQ(io.last_error(), EOVERFLOW);
}
TEST(BasicIoErrors, RtxRecordsSendRecvAndDoesNotShareState) {
  FaultyRtx io, other;
  check_error_snapshot(io, [&] { return io.recv(nullptr, 0); });
  io.fail_io = true;
  EXPECT_EQ(io.send(nullptr, 0), -1);
  EXPECT_EQ(io.last_error(), EAGAIN);
  EXPECT_EQ(other.last_error(), 0);
  io.fail_io = false;
  EXPECT_EQ(io.send(nullptr, 0), 0);
  EXPECT_EQ(io.last_error(), 0);
}
TEST(BasicIoErrors, FailureWithoutErrnoUsesEio) {
  Receiver io;
  char data[1];
  errno = ENOENT;
  EXPECT_EQ(io.recv(data, 1), -1);
  EXPECT_EQ(io.last_error(), EIO);
}

TEST(BasicIoErrors, RecvFromValidatesBeforeDispatchAndReturnsSource) {
  Receiver receiver;
  detail::io::rx<Receiver, Options>& base = receiver;
  void* source = nullptr;
  char data[3];
  EXPECT_EQ(base.recv_from(nullptr, 1, source), -1);
  EXPECT_EQ(base.last_error(), EINVAL);
  const auto too_large =
      static_cast<std::size_t>(std::numeric_limits<int>::max()) + 1;
  EXPECT_EQ(base.recv_from(data, too_large, source), -1);
  EXPECT_EQ(base.last_error(), EOVERFLOW);
  EXPECT_EQ(receiver.recv_from_calls, 0);
  EXPECT_EQ(source, nullptr);

  EXPECT_EQ(base.recv_from(data, sizeof(data), source), 3);
  EXPECT_EQ(std::string(data, sizeof(data)), "abc");
  EXPECT_EQ(source, static_cast<void*>(&receiver));
  EXPECT_EQ(receiver.recv_from_calls, 1);
  EXPECT_EQ(base.last_error(), 0);
}

namespace {
template <typename Derived>
void check_common_interface(Derived& derived) {
  using CommonIo = detail::io::io<Derived, Options>;
  static_assert(std::is_base_of_v<CommonIo, Derived>);
  static_assert(!std::is_default_constructible_v<CommonIo>);
  static_assert(!std::is_destructible_v<CommonIo>);
  CommonIo& base = derived;
  int closes = 0;
  const Options options{&closes};
  base.set_opt(options);
  const CommonIo& const_base = base;
  static_assert(std::is_same_v<decltype(const_base.get_opt()), const Options&>);
  EXPECT_EQ(&base.get_opt(), &const_base.get_opt());
  EXPECT_EQ(base.get_opt().closes, &closes);
  base.set_opt(Options{&closes});
  EXPECT_TRUE(base.open());
  base.close();
  EXPECT_EQ(closes, 1);
  // Clear the borrowed counter before the derived destructor runs.
  base.set_opt(Options{});
}

template <typename Derived, typename Operation>
void check_transfer_bounds(Derived& io, Operation operation) {
  char byte = 0;
  const auto max_length =
      static_cast<std::size_t>(std::numeric_limits<int>::max());
  EXPECT_EQ(operation(&byte, max_length + 1), -1);
  EXPECT_EQ(io.last_error(), EOVERFLOW);
  EXPECT_EQ(operation(nullptr, 1), -1);
  EXPECT_EQ(io.last_error(), EINVAL);
  EXPECT_EQ(io.calls, 0);
  io.fail_io = false;
  EXPECT_EQ(operation(nullptr, 0), 0);
  EXPECT_EQ(io.last_error(), 0);
  // The fake backend does not access memory; this checks the exact int limit.
  EXPECT_EQ(operation(&byte, max_length), 0);
  EXPECT_EQ(io.last_error(), 0);
  EXPECT_EQ(io.calls, 2);
}
}  // namespace

TEST(BasicIo, AllDirectionsShareConfigurationAndLifecycleInterface) {
  Receiver receiver;
  Sender sender;
  Duplex duplex;
  check_common_interface(receiver);
  check_common_interface(sender);
  check_common_interface(duplex);
}

TEST(BasicIoErrors, AllDirectionsValidateBuffersAndLengthsBeforeDispatch) {
  FaultyRx receiver;
  check_transfer_bounds(receiver, [&](char* data, std::size_t len) {
    return receiver.recv(data, len);
  });
  FaultyTx sender;
  check_transfer_bounds(sender, [&](char* data, std::size_t len) {
    return sender.send(data, len);
  });
  FaultyRtx duplex;
  check_transfer_bounds(duplex, [&](char* data, std::size_t len) {
    return duplex.recv(data, len);
  });
  duplex.calls = 0;
  check_transfer_bounds(duplex, [&](char* data, std::size_t len) {
    return duplex.send(data, len);
  });
}
