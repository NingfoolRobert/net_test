#include <gtest/gtest.h>

#include <array>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <new>

#include "tcp_stream_manager.h"
#include "tcp_stream_reassembly.h"

namespace {

struct allocation_trace {
  size_t calls = 0;
  size_t frees = 0;
  size_t fail_at = 0;
  std::array<void*, 2> owned{};
};
// GNU linker wrappers need a trace enabled only by trace_scope.
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

// GNU --wrap requires these C symbol names; production has no allocation hooks.
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
    return ENOMEM;
  }
  const int result = __real_posix_memalign(memory, alignment, size);
  if (result == 0 && trace.calls <= trace.owned.size()) {
    trace.owned.at(trace.calls - 1) = *memory;
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

net::tcp_packet_view packet(uint8_t flags) {
  net::tcp_packet_view result;
  result.tuple = {{1, 10}, {2, 20}};
  result.sequence = 100;
  result.flags = flags;
  return result;
}

// Keep GTest assertions together with each allocation lifecycle.
// NOLINTBEGIN(readability-function-cognitive-complexity)
TEST(TcpStreamManagerAllocation,
     FailedStreamConstructionReleasesPartialAllocations) {
  for (size_t fail_at = 1; fail_at <= 2; ++fail_at) {
    allocation_trace trace;
    trace.fail_at = fail_at;
    net::tcp_stream_manager manager({1, 64});
    manager.replace_rules({{1, {{1, 10}, {2, 20}}}});
    bool caught = false;
    {
      const trace_scope scope(trace);
      try {
        manager.insert(packet(net::tcp_syn));
      } catch (const std::bad_alloc&) {
        caught = true;
      }
    }
    EXPECT_TRUE(caught);
    EXPECT_EQ(trace.calls, fail_at);
    EXPECT_EQ(trace.frees, fail_at - 1);
    EXPECT_EQ(manager.stats().connections, 0U);
    EXPECT_EQ(manager.stats().allocated_slots, 0U);
    // Failed allocation does not consume the free slot or publish a key.
    EXPECT_TRUE(manager.insert(packet(net::tcp_syn)).created);
  }
}

TEST(TcpStreamManagerAllocation, SlotReuseRetainsBothAlignedAllocations) {
  allocation_trace trace;
  {
    const trace_scope scope(trace);
    net::tcp_stream_manager manager({1, 64});
    manager.replace_rules({{1, {{1, 10}, {2, 20}}}});
    EXPECT_EQ(trace.calls, 0U);
    for (size_t iteration = 0; iteration != 1000; ++iteration) {
      const auto inserted = manager.insert(packet(net::tcp_syn));
      ASSERT_TRUE(inserted.created);
      manager.insert(packet(net::tcp_rst));
      manager.process(
          *inserted.handle,
          [](const net::tcp_stream_context&, const net::readable_buffers&,
             net::tcp_stream_status) { return size_t{0}; },
          [](const net::tcp_stream_context&,
             const net::tcp_managed_stream_state&) {});
      EXPECT_EQ(manager.release(*inserted.handle).status,
                net::tcp_release_status::released);
    }
    EXPECT_EQ(trace.calls, 2U);
    EXPECT_EQ(trace.frees, 0U);
  }
  EXPECT_EQ(trace.frees, 2U);
}

TEST(TcpStreamManagerAllocation,
     FailedReplacementLeavesOriginalSessionAndMappingIntact) {
  net::tcp_stream_manager manager({2, 64});
  manager.replace_rules({{1, {{1, 10}, {2, 20}}}});
  const auto first = manager.insert(packet(net::tcp_syn));
  auto replacement = packet(net::tcp_syn);
  replacement.sequence = 1000;
  allocation_trace trace;
  trace.fail_at = 2;  // Bitmap allocation of the replacement stream.
  {
    const trace_scope scope(trace);
    EXPECT_THROW(manager.insert(replacement), std::bad_alloc);
  }
  EXPECT_EQ(manager.stats().connections, 1U);
  EXPECT_EQ(manager.stats().allocated_slots, 1U);
  EXPECT_EQ(manager.state(*first.handle)->phase,
            net::tcp_connection_phase::tracking);
  EXPECT_EQ(manager.insert(packet(net::tcp_syn)).handle, first.handle);
}
// NOLINTEND(readability-function-cognitive-complexity)

}  // namespace
