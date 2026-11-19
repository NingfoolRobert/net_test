# Module layout

Headers, non-template implementations, tests, and benchmarks live together in
their module directories. Public includes use `basic/<module>/<header>`; there
are no root-level forwarding headers or legacy module entries.

| Directory | Responsibility |
| --- | --- |
| `base/` | RAII, scope cleanup, assertions, compiler helpers, and small language utilities |
| `memory/` | Process-local allocators, memory pools, and object storage |
| `buffer/` | Byte storage, borrowed views, and read/write positions |
| `container/` | Typed containers, fixed strings, and hash functions |
| `concurrency/` | In-process synchronization and concurrent queues |
| `ipc/` | Shared-memory lifecycle, inter-process locks, and message queues |
| `time/` | Time representation, conversion, and clock reads |
| `execution/` | Worker execution and timer callback scheduling; `SyncPair` lives here directly |
| `io/` | Generic I/O interfaces, error state, and file utilities |
| `config/` | Configuration nodes, validation, parsing, and serialization |
| `logging/` | Logging output |
| `test/` | Cross-module regression tests; its CMake file also registers module unit tests |
| `docs/` | Cross-module contracts, design notes, and historical drafts |

## Locality and implementation

- Keep a public header and its `.cpp` in the same directory. Templates and
  inline implementations stay in headers.
- Put module unit tests in `<module>/test/` and module benchmarks in
  `<module>/benchmark/`. Create these directories when they have content.
- Put implementation-only helpers in `<module>/detail/`. For example,
  `config/detail/format_util.h` is not a public configuration interface.
- Keep backend integration tests identifiable. The I/O tests still compile
  sibling `network/` backends, and `test/test_object_pool.cpp` exercises the
  sibling `core/` object pool. These tests require the surrounding repository.
- Third-party sources live in the repository-level `third_party/` directory.
  Configuration dependencies are resolved by `config/CMakeLists.txt`.
- Historical drafts in `docs/archive/` are retained as reference material and
  are not public headers or build inputs.

## Build dependencies

The header modules expose `basic::base`, `basic::memory`, `basic::buffer`,
`basic::container`, `basic::concurrency`, `basic::ipc`, `basic::time`,
`basic::io`, and `basic::logging`. The existing `basic` target aggregates these
header modules; `basic::basic` is its namespaced form.

`basic::config` is a separate compiled target. The polling executor is available
as `basic::execution` when `BASIC_BUILD_EXECUTION=ON`, or when `execution/` is
built independently. The time wheel remains header-based and can be used without
building the polling executor; applications using it link the system thread
library.

Bottom-level modules must not acquire dependencies on configuration parsers or
runtime execution. Existing dependencies are retained during this layout change:
in particular, `base/assert_macro.h` still includes the logging header, and
`buffer/staging_buffer.h` uses the concurrency memory-fence helper. Changing these
contracts is a separate implementation task.

## Performance contracts

Directory placement expresses responsibility, not a latency or lock-free
guarantee. Document allocation behavior, capacity, concurrency model, waiting,
overflow handling, ownership, and platform requirements for each public type.
Some existing pools and synchronization tools allocate or use locks.

Keep throughput and latency measurements separate. Record build options,
hardware, CPU placement, workload size, queue capacity, and allocation behavior
with benchmark results. This reorganization does not change algorithms or claim
a measured performance improvement.
