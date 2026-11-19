#include <gtest/gtest.h>

#include <array>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <new>
#include <optional>
#include <system_error>

#include "tcp_stream_reassembly.h"

namespace {

struct allocation_trace {
  size_t calls = 0;
  size_t frees = 0;
  size_t fail_at = 0;
  int error = ENOMEM;
  std::array<void*, 2> owned{};
  std::array<size_t, 2> alignment{};
  std::array<size_t, 2> size{};
};

// C linker wrappers need a trace enabled only by trace_scope.
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
thread_local allocation_trace* active_trace = nullptr;

class trace_scope {
 public:
  explicit trace_scope(allocation_trace& trace) { active_trace = &trace; }
  ~trace_scope() { active_trace = nullptr; }
  trace_scope(const trace_scope&) = delete;
  trace_scope& operator=(const trace_scope&) = delete;
  trace_scope(trace_scope&&) = delete;
  trace_scope& operator=(trace_scope&&) = delete;
};

}  // namespace

// GNU linker wrapping only affects this test binary; production has no hooks.
// These symbol names are required by --wrap.
// NOLINTBEGIN(cert-dcl37-c,cert-dcl51-cpp)
extern "C" int __real_posix_memalign(void** memory, size_t alignment,
                                     size_t size);
extern "C" void __real_free(void* memory);

extern "C" int __wrap_posix_memalign(void** memory, size_t alignment,
                                     size_t size) {
  if (active_trace == nullptr) {
    return __real_posix_memalign(memory, alignment, size);
  }
  auto& trace = *active_trace;
  ++trace.calls;
  if (trace.calls == trace.fail_at) {
    return trace.error;
  }
  const int result = __real_posix_memalign(memory, alignment, size);
  if (result == 0 && trace.calls <= trace.owned.size()) {
    const size_t index = trace.calls - 1;
    trace.owned.at(index) = *memory;
    trace.alignment.at(index) = alignment;
    trace.size.at(index) = size;
  }
  return result;
}

extern "C" void __wrap_free(void* memory) {
  if (active_trace != nullptr && memory != nullptr) {
    for (auto& owned : active_trace->owned) {
      if (owned == memory) {
        ++active_trace->frees;
        owned = nullptr;
      }
    }
  }
  __real_free(memory);
}
// NOLINTEND(cert-dcl37-c,cert-dcl51-cpp)

namespace {

TEST(TcpStreamAllocation,
     FreesBothAlignedAllocationsAndDoesNotReallocateOnReceive) {
  allocation_trace trace;
  {
    const trace_scope tracing(trace);
    net::tcp_stream_reassembly stream(128);
    const std::array<uint8_t, 32> payload{};
    for (uint32_t iteration = 0; iteration < 4096; ++iteration) {
      stream.insert({{100 + (iteration * 32), {payload.data(), payload.size()}},
                     std::nullopt});
      stream.process([](const net::readable_buffers&,
                        net::tcp_stream_status) { return size_t{0}; },
                     [](const net::tcp_stream_state&) {});
    }
    stream.clear();
  }
  EXPECT_EQ(trace.calls, 2U);
  EXPECT_EQ(trace.frees, 2U);
  EXPECT_EQ(trace.alignment.at(0), 64U);
  EXPECT_EQ(trace.alignment.at(1), 64U);
  EXPECT_EQ(trace.size.at(0), 128U);
  EXPECT_EQ(trace.size.at(1), 16U);
}

TEST(TcpStreamAllocation, HandlesFailureOfEitherAllocationWithoutLeaking) {
  for (const size_t fail_at : {size_t{1}, size_t{2}}) {
    allocation_trace trace;
    trace.fail_at = fail_at;
    bool caught = false;
    {
      const trace_scope tracing(trace);
      try {
        const net::tcp_stream_reassembly stream(128);
      } catch (const std::bad_alloc&) {
        caught = true;
      }
    }
    EXPECT_TRUE(caught);
    EXPECT_EQ(trace.calls, fail_at);
    EXPECT_EQ(trace.frees, fail_at - 1);
  }
}

TEST(TcpStreamAllocation, ReportsPosixReturnCodeRatherThanErrno) {
  allocation_trace trace;
  trace.fail_at = 2;
  trace.error = EINVAL;
  int reported_error = 0;
  {
    const trace_scope tracing(trace);
    errno = ENOMEM;
    try {
      const net::tcp_stream_reassembly stream(128);
    } catch (const std::system_error& error) {
      reported_error = error.code().value();
    }
  }
  EXPECT_EQ(reported_error, EINVAL);
  EXPECT_EQ(trace.frees, 1U);
}

}  // namespace
