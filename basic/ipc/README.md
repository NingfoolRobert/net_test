## Fixed shared memory (Linux, C++17)

`fixed_shared_mem.h` implements `basic::FixedSharedMem<Backend>` and its alias
`basic::shm_t<Backend>`. The default backend maps a regular file with `MAP_SHARED`;
`basic::SysVBackend` uses `shmget`/`shmat`. The complete implementation is in this
one header. Both backends expose the same byte-access and ownership interface.

```cpp
#include "basic/ipc/fixed_shared_mem.h"

const basic::MmapLocator locator{"/dev/shm/my-app-region"};
auto writer = basic::shm_t<>::Create(locator, 1024 * 1024);
auto reader = basic::shm_t<>::Attach(locator, {1024 * 1024, {}});

if (auto error = writer.Write(128, "hello", 6)) {
    throw std::system_error(error);
}
char value[6]{};
if (auto error = reader.Read(128, value, sizeof(value))) {
    throw std::system_error(error);
}

// Call after the application's peers have finished using this name.
if (auto error = basic::shm_t<>::Remove(locator)) {
    throw std::system_error(error);
}
```

Select System V at compile time:

```cpp
using Memory = basic::shm_t<basic::SysVBackend>;
const basic::SysVLocator locator{0x123456}; // Application-coordinated key.
auto memory = Memory::Create(locator, 1024 * 1024);
// After all peers have attached and the application is ready to remove the key:
if (auto error = Memory::Remove(locator)) {
    throw std::system_error(error);
}
```

The locator identifies the resource: an owning path string for mmap, a `key_t`
for SysV. `IPC_PRIVATE` is rejected because it cannot identify a named resource
for another process to attach. Capacity is the exact number of accessible bytes;
page rounding stays internal to Linux. Capacity is positive, fixed, and must fit
both pointer differences and file offsets. No component header consumes any of
these bytes.

`Create` is exclusive and fails with `EEXIST` for an existing resource. Default
permissions are `0600` (mmap creation also respects `umask`). `Attach` opens an
existing resource without truncating or clearing it. `AttachOptions` can require
an exact capacity; its default of zero accepts the discovered capacity. A failed
create rolls back the newly created resource; a failed attach leaves the existing
resource intact. Creation/attachment errors throw `std::system_error`.

`Mem(offset, length)` returns a checked pointer or null. Its default length is
one byte; `Mem(capacity(), 0)` permits a one-past pointer. `Read` and `Write` copy
the complete requested range with `memmove`, including overlapping ranges, or
return an error without copying. Zero-length operations accept null buffers at a
valid offset. Errors use `std::generic_category()`: `EBADF` for a closed mapping,
`ERANGE` for a range outside capacity, and `EINVAL` for a nonempty null buffer.
`data()` and `view()` provide mutable or const access according to the owner's
constness. A view borrows the mapping and does not extend its lifetime. Views
remain valid when the mapping is transferred by move construction, but expire
when that mapping is closed or replaced. A manually constructed view requires a
valid byte range from its caller.

Ownership is move-only. Move construction is `noexcept`; move assignment first
closes the destination and throws on detach failure, keeping both owners intact.
`Close()` returns an error and preserves ownership on failure; repeated successful
closes do nothing. Destruction detaches without removing the resource or flushing
to storage. Use explicit `Close()` when a detach error must be observed. `Remove`
returns an error code and treats an already absent name as success. Existing
mappings remain usable after removal; SysV frees the segment after its final
attachment is detached. The application must coordinate creation, removal, and
reuse of names/keys. Do not truncate mapped files or change their capacity.

Both creation and attachment default to `MADV_POPULATE_WRITE` to move writable
page faults into initialization, without clearing existing bytes. This requires
Linux 5.14 or later and suitable headers; failures propagate. Set
`options.mapping.prefault = false` to request demand paging instead. Optional
`options.mapping.lock_memory = true` requires `mlock` to succeed; failure rolls
back instead of silently falling back. Prefaulting alone does not prevent later
eviction or guarantee latency. For RAM-backed mmap storage, choose a path on
tmpfs, such as `/dev/shm`; ordinary files retain their filesystem's behavior.

The access path performs bounds checks and byte copies, with no allocation,
syscalls, locks, or notification primitives. It provides no queue protocol,
message headers, object construction, allocation, initialization-ready flag, or
cross-process synchronization. Higher layers own those contracts, including
release/acquire publication when polling a message-size field. Shared structures
must use offsets instead of process-local pointers. Concurrent close/move and
access to the same owner require external synchronization. File rotation and
ring queues belong in those higher layers.

The legacy `detail::fixed_shm` / `detail::shm_t` interface is defined separately
in `fixed_shm.h`. Existing callers, including `rx_shm`, include that header.
The legacy implementation has no dependency on `fixed_shared_mem.h`.
It accepts paths of at most 255 bytes excluding the terminator. Invalid paths
are reported by `open()` as false with `EINVAL` (null/empty) or `ENAMETOOLONG`.
Zero mapping sizes fail with `EINVAL`; unrepresentable sizes fail with `EOVERFLOW`.
Only an exclusive creator sizes a new file. Existing files must cover the
requested mapping; shorter files fail with `EINVAL` and are never resized.
Larger files may be mapped by prefix. File length is rechecked before mapping;
callers must prevent truncation while opening or using a mapping.
`writeto()` rejects out-of-range writes before copying. Move assignment releases
the destination's previous mapping and descriptor before taking ownership.
New code should use the `basic` API above.

## Shared-memory message queue

`shm_ring_queue.h` defines `basic::ShmRingQueue`, a Linux SPSC variable-length
byte queue using the original `detail::shm_t` from `fixed_shm.h`. The original
mapping API is retained with the validation described above. Each file has one
producer and one consumer, each used from one thread. Bidirectional communication
uses two independent queue files.

```cpp
#include "basic/ipc/shm_ring_queue.h"

using Queue = basic::ShmRingQueue;
// In process A:
auto producer = Queue::open("/dev/shm/app-a-to-b", 1024 * 1024,
                            Queue::Role::kProducer);
// In process B (either process may start first):
auto consumer = Queue::open("/dev/shm/app-a-to-b", 1024 * 1024,
                            Queue::Role::kConsumer);

int sent = producer.sendto("hello", 6);
char message[1024]{};
int received = consumer.recvfrom(message, sizeof(message));
// Check each return value: -1 reports the failure through errno.
```

`open` exclusively creates a new file with mode 0600, or attaches to the existing
queue. Only the actual creator initializes it; attach never clears records or
changes capacity. Capacity counts data bytes and must be a power of two of at
least 2048. Both endpoints must specify the same capacity. Initialization errors
throw `std::system_error`. An optional fourth argument sets the initialization
wait timeout (`std::chrono::milliseconds`, default five seconds); timeout throws
`ETIMEDOUT` without taking over abandoned initialization. The timeout bounds
waiting for initialization/its file lock, not filesystem or mapping syscalls.

Startup uses `flock`, file size/format checks, and a short `/proc/self/fd/` path to
pin the checked inode while the original class opens/maps it. This requires
Linux procfs. These operations run only during `open`. Roles are checked locally;
the application must ensure there is only one active endpoint for each role.
Files must not be deleted, replaced, truncated, or reinitialized while opening
or using a queue. Capacity/format mismatches fail instead of resetting the file.
After both endpoints close, the application may unlink the file to start a new
queue. Closing/destruction itself never removes it.

The shared control area is 192 bytes: one cache line for immutable configuration
and readiness, one for the committed write cursor, and one for the released read
cursor. The data area starts immediately afterward. A record occupies exactly
`2 + length` bytes: a two-byte little-endian length and 0..1023 payload bytes.
Both header and payload can span the tail and head; callers always provide and
receive contiguous buffers. There are no padding records or reserved empty bytes.

Whole records become visible through a release store of the write cursor. The
consumer acquires that cursor before reading, then releases the read cursor
only after copying the whole record. Cached peer cursors reduce shared loads.
Naturally aligned lock-free 32/64-bit GCC/Clang atomics are required at compile
time. This uses the Linux shared-memory/compiler ABI. The transfer path performs
no allocation, syscalls, locking, flushing, waiting, or cross-process notification.
The caller chooses how to retry on `EAGAIN`.

| Result | Behavior |
| --- | --- |
| Success | Returns the payload length; zero-length messages return zero. |
| `EAGAIN` | No space for a complete record, or no complete published message. |
| `EMSGSIZE` | Send length is at least 1024, or receive buffer is too small. A failed receive retains the message. |
| `EINVAL` | Nonzero buffer length/capacity with a null pointer. Null with zero is accepted. |
| `EBADF` | Endpoint is closed. |
| `EPERM` | Operation does not match the endpoint's role. |
| `EPROTO` | Invalid record length or cursor distance; no partial message is returned. |

Queue ownership is move-only; moves transfer the existing mapping without calling
legacy `shm_t` move assignment. `close` is idempotent and preserves the original
class's destructor flushing behavior. Do not close or move an endpoint while it
is being used. Reopening an orderly closed endpoint preserves unread messages;
no automatic recovery or exactly-once processing is promised after process
failure. No throughput or latency bound is guaranteed without measurement on the
intended workload and CPU placement.

## Fixed-size shared-memory message queue

`shm_fixed_queue.h` defines `basic::ShmFixedQueue<T, Capacity>`. Both message type
and slot count are template arguments. This is an independent Linux SPSC queue
backed by the original `detail::shm_t`; the existing shared-memory and variable-
length queue APIs remain independent.

```cpp
#include "basic/ipc/shm_fixed_queue.h"
#include <cstdint>

struct Message {
    std::uint64_t sequence;
    std::int32_t value;
    std::uint32_t flags;
};
using Queue = basic::ShmFixedQueue<Message, 1024>;

// Process A, or process B first: either endpoint may create the file.
auto sender = Queue::open("/dev/shm/fixed-messages", Queue::Role::kProducer);
auto receiver = Queue::open("/dev/shm/fixed-messages", Queue::Role::kConsumer);
Message outgoing{1, 42, 0};
int sent = sender.sendto(outgoing);
Message incoming{};
int received = receiver.recvfrom(incoming);
// Success returns sizeof(Message). Check -1 and errno on failure.
```

`Capacity` must be a power of two of at least two slots. `T` must be an unqualified,
trivially copyable standard-layout value type; arrays can be wrapped in a struct
or `std::array`. Top-level pointer and array types are rejected. The complete
mapping must fit pointer differences and file offsets, and `sizeof(T)` must fit
an `int` return value. These requirements are checked at compile time. There is
no 1024-byte message limit: the message size is exactly `sizeof(T)`.

The shared file occupies `192 + Capacity * sizeof(T)` bytes. The 192-byte control
area isolates configuration, write cursor, and read cursor on separate cache
lines. Each slot is exactly `sizeof(T)` bytes, including the structure's own
padding, with no additional slot padding or per-message length header. Cursors
count messages; selecting `(position & (Capacity - 1))` yields a complete slot.
Every slot is usable. Each successful transfer performs one object-representation
copy and publishes/releases its cursor with release/acquire atomics. Shared
storage is treated as bytes; it does not construct, destroy, or dereference a
shared `T` object, including for over-aligned types.

Both processes must use the same `T` definition and compatible compiler ABI.
Messages must not contain process-local pointers or resource ownership. The
traits do not inspect every member for such content. Attachment checks format,
capacity, message size, and message alignment; same-size/same-alignment schema
changes still require coordination by the application. Fixed and variable queue
formats are distinct and must use different files.

`open(path, role, timeout)` follows the variable queue's startup rules: exclusive
creation, creator-only initialization, file locking confined to startup, pinned
procfs descriptors, capacity/format validation, and a five-second default wait
for initialization. It throws `std::system_error`; abandoned initialization is
never reset automatically. The timeout covers waiting, not filesystem I/O.
Exactly one active producer and one active consumer are required, each used from
one thread. Roles are checked locally, not registered as process ownership.

`sendto(const T&)` and `recvfrom(T&)` return `sizeof(T)` on success. Failure returns
-1 with `EAGAIN` for full/empty, `EBADF` for a closed endpoint, `EPERM` for a wrong
role, or `EPROTO` for an invalid cursor distance. A failed receive leaves the
output object unchanged. There are no partial transfers. The transfer path has
no allocation, syscalls, locks, flushing, or notification. Bidirectional use
requires two queues.

Ownership is move-only. `close()` is idempotent, preserves the original class's
destructor flushing, and does not delete the file. Reopening retains unread
records. Do not close or move an endpoint concurrently with its use, or delete,
replace, resize, or reinitialize an active queue. No automatic crash recovery or
measured latency guarantee is provided.

## Zero-copy fixed-slot shared-memory queue

`shm_fixed_zero_copy_queue.h` defines the independent Linux SPSC class
`basic::ShmFixedZeroCopyQueue<T, Capacity>`, backed directly by the original
`detail::shm_t`. Its `send` and `recv` methods invoke a callback on the mapped
slot; the queue performs no payload copies, clearing, or allocations in these
methods. Callback work, including any copying it chooses to perform, belongs to
the application. The queue uses the legacy mapping validation described above.

```cpp
#include "basic/ipc/shm_fixed_zero_copy_queue.h"
#include <cstdint>

struct Message {
    std::uint64_t sequence;
    std::int32_t value;
    std::uint32_t flags;
};
using Queue = basic::ShmFixedZeroCopyQueue<Message, 1024>;

// Process A. Either process may create the file first.
auto sender = Queue::open("/dev/shm/zero-copy-messages", Queue::Role::kProducer);
int sent = sender.send([](Message& slot) {
    slot.sequence = 1;
    slot.value = 42;
    slot.flags = 0;
});

// Process B. Both processes use the same type, capacity, ABI, and path.
auto receiver = Queue::open("/dev/shm/zero-copy-messages", Queue::Role::kConsumer);
std::int32_t value = 0;
int received = receiver.recv([&](const Message& slot) {
    value = slot.value;  // Process the shared message before returning.
});
// Success returns sizeof(Message); check -1 and errno on failure.
```

Callbacks must return `void`. A normal return from `send` publishes the complete
message by advancing the write cursor with release semantics. A normal return
from `recv` releases the slot by advancing the read cursor with release
semantics. The peer acquires the appropriate cursor before using/reusing slots.
The consumer's reference is read-only. While its callback is running, that slot
cannot be overwritten, and a full queue remains full until the callback returns.
References and pointers into a slot must not escape the callback, including via
asynchronous work. The producer must initialize every field the consumer reads;
slots are deliberately not zeroed, and padding bytes may contain old data.

Callback exceptions propagate unchanged. A throwing writer does not publish its
partially written slot; the next successful writer must fill it again. A throwing
reader leaves the record queued for retry. Application side effects are not
rolled back, so exception retry does not imply exactly-once processing. Ordinary
queue errors never invoke the callback: `EAGAIN` means full/empty, `EBADF` closed,
`EPERM` wrong role, and `EPROTO` invalid cursor distance. A recursive call to the
same endpoint's transfer method returns `EBUSY`.

`T` has the copying queue's type restrictions and additionally must be trivially
default constructible. A producer starts its slot's object lifetime with trivial
placement default construction, without clearing or copying the payload. Shared
object access across independent processes relies on the Linux/compiler ABI;
this is not a portable ISO C++ interprocess object model. Pointer members and
process-local resource ownership are forbidden by contract, including nested
members that type traits cannot inspect. Default member initializers commonly
make default construction nontrivial; initialize message fields in `send` instead.

The control area is 192 bytes, with metadata and each cursor in separate 64-byte
regions. The data offset is rounded up to `alignof(T)`; every slot occupies exactly
`sizeof(T)` bytes, including its native structure padding. File size is
`round_up(192, alignof(T)) + Capacity * sizeof(T)`. `open` rejects alignments above
the system page size before creating a file. Compile-time checks require a
power-of-two capacity of at least two, a message size fitting `int`, and a mapping
fitting pointer/file offsets. All slots are usable, including across unsigned
cursor wrap. There is no per-slot message header.

The format has its own magic value and records the data offset, capacity, size,
and alignment. Use a separate file from the copying and variable-length queues.
Same-size, same-alignment schema changes still require application coordination.
`open(path, role, timeout)` uses creator-only initialization, startup file locking,
a pinned procfs descriptor, and a five-second default initialization wait. It
throws `std::system_error`; an abandoned file is not reset automatically.

Ownership is move-only. `close()` returns 0 on success and is idempotent; during
a callback it returns -1 with `EBUSY`. Moving either an active source or active
destination throws `std::system_error(EBUSY)` without changing either endpoint.
Do not destroy an endpoint from its callback. Endpoints must not be accessed
concurrently by multiple threads. Closing preserves the legacy destructor's
flush behavior and keeps the file; orderly reopening retains unread messages.
Do not unlink, replace, resize, or reinitialize an active queue. There is no
notification, crash recovery, or measured latency guarantee. Bidirectional
communication requires two queues.
