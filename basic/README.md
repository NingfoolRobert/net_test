# basic

C++17 foundation library. Headers, implementations, tests, and benchmarks live
in the same module directories; no separate include/src tree is used.

## Modules

| Directory | Responsibility | Documentation |
| --- | --- | --- |
| `base/` | RAII, scope cleanup, assertions, and compiler helpers | [Storage and ownership](docs/contracts/storage.md) |
| `memory/` | Allocators, memory pools, and object storage | [Storage and ownership](docs/contracts/storage.md) |
| `buffer/` | Byte buffers and borrowed storage | [Storage and ownership](docs/contracts/storage.md) |
| `container/` | Typed containers, fixed strings, and hash functions | [Storage and ownership](docs/contracts/storage.md) |
| `concurrency/` | In-process synchronization and queues | [Module layout](docs/architecture.md) |
| `ipc/` | Shared memory, inter-process locks, and message queues | [IPC guide](ipc/README.md) |
| `time/` | Date/time conversion and clock reads | [Module layout](docs/architecture.md) |
| `execution/` | Polling executor, time wheel, and SyncPair | [Execution guide](execution/README.md) |
| `io/` | Generic I/O interfaces, errors, and file utilities | [I/O guide](io/README.md) |
| `config/` | Configuration nodes and format parsers | [Configuration guide](config/README.md) |
| `logging/` | Logging output | [Module layout](docs/architecture.md) |

Module tests live in `<module>/test/`; module benchmarks live in
`<module>/benchmark/`. Root `test/` contains cross-module regressions.
Third-party sources are shared from the repository-level `third_party/`.
See [architecture and dependency rules](docs/architecture.md).

## Includes and targets

Include headers from their current module paths:

```cpp
#include "basic/memory/object_pool.h"
#include "basic/concurrency/queue/mpsc_queue.h"
#include "basic/execution/sync_pair.h"
```

The root forwarding headers and former `util/`, `threadpool/`,
`concurrency/ipc/`, and `container/map/` entries have been removed.
Configuration uses `basic/config/config_parser.h`; the historical `.hpp`
entry has been removed as well. Type names and runtime behavior are unchanged.

When used from CMake, link the required module targets:

```cmake
add_subdirectory(basic)
target_link_libraries(app PRIVATE basic::memory basic::concurrency)
```

The `basic` / `basic::basic` target aggregates the header modules.
`basic::config` is separate. Enable `BASIC_BUILD_EXECUTION` to build and link
`basic::execution` (`polling_thread_executor`). Header search paths support both
`basic/<module>/<header>` and `<module>/<header>`.

## Build and test

Run from the repository root:

```sh
cmake -S basic -B /tmp/basic-build -DCMAKE_BUILD_TYPE=Debug -DBASIC_BUILD_EXECUTION=ON
cmake --build /tmp/basic-build -j 4
ctest --test-dir /tmp/basic-build --output-on-failure
```

GoogleTest is required when `BUILD_TESTING=ON`. Configuration format dependencies
are described in the [configuration guide](config/README.md). Set
`BUILD_TESTING=OFF` to build without GoogleTest. The executor remains optional
and defaults to OFF in the main build; build it independently with:

```sh
cmake -S basic/execution -B /tmp/basic-execution-build -DCMAKE_BUILD_TYPE=Debug
cmake --build /tmp/basic-execution-build -j 4
ctest --test-dir /tmp/basic-execution-build --output-on-failure
```

The time-wheel tests run even when the optional polling executor is disabled.
I/O backend integration tests and the core object-pool regression require the
surrounding `net_test` repository.

## Benchmark

The MPMC queue benchmark is excluded from the default build:

```sh
cmake --build /tmp/basic-build --target benchmark_mpmc_queue -j 4
/tmp/basic-build/benchmark_mpmc_queue 100000
```

Its source lives in `concurrency/benchmark/`. Directory organization does not
imply lock-free operation or bounded latency; consult module contracts and
measure the workload of interest.
