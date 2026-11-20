## Storage and ownership

- `detail::bheap<T>` and the legacy `basic::Allocator<T, N>` exclusively own
  their nodes/storage. Copy construction and copy assignment are disabled;
  neither type provides move operations. Keep owners in place and pass references.
- `detail::fstring<N>` assignments from C strings and `std::string` copy at most
  `N - 1` bytes and terminate the result. C-string assignment supports overlap
  with its own storage; assigning a null C-string pointer leaves the value unchanged.
  Comparisons, searches and substrings use the logical string length.
  A substring starting at the end is empty, and a start beyond the end throws
  `std::out_of_range`.
- `detail::DynamicBufferr::size()` is the total valid byte count.
  `readable_size()` counts bytes from the current head. `read(out)` replaces
  `as<T>()` and copies a valid native object representation into an existing
  trivially copyable object. A short read returns false and leaves `out` intact.
  Callers parsing wire formats validate and decode their fields explicitly.
  A zero expansion step throws `std::invalid_argument`.
- `basic::MemoryPool<ChunkSize, CacheLineSize>` allocates aligned bytes from chunks,
  coalesces returned ranges, and uses separate aligned allocations for large blocks.
  Both allocate and deallocate are protected by a mutex. Alignment and cache-line
  arguments must be positive powers of two. Zero-byte requests return null.
- `detail::FixedMemoryPool<BlockMin, BlockMax, Unit_Size>` groups sizes in multiples
  of `BlockMin`, maintains separate classes for each alignment, and expands a class
  by `Unit_Size` blocks. Oversized requests bypass the free lists. Return a pointer
  with the original requested size. The pool owns all of its blocks and is thread-safe.
- `basic::ObjectPool<T>` constructs objects with forwarded arguments and destroys
  them on release. It reuses storage, including over-aligned storage, and destroys
  outstanding objects at pool destruction. A constructor may acquire or release
  other objects in the same pool. If it throws, the pool returns its reserved
  slot to the free list and preserves completed nested operations.
  It requires external synchronization
  when shared across threads. `capacity()`, `available()`, and `in_use()` report counts.
- Pool deallocation rejects foreign or already returned pointers and nulls the
  pointer on success. Aliases must not be used after return; an old alias cannot
  identify a later allocation at the same address.
- `detail::buffer_t(capacity)` owns bounded storage, allocated with `posix_memalign`
  at 64-byte alignment and released with `free`. Allocation failures throw
  `std::bad_alloc`; failed resizes or resets leave the buffer unchanged. `resize(capacity)`
  changes its backing capacity, preserves bytes that fit, and truncates on shrink.
  `reset(capacity)` grows storage only when the requested capacity exceeds the current
  capacity, rounding up to a multiple of the fixed 256-byte allocation step and
  preserving existing data and size; otherwise it does nothing. Rounding overflow
  throws `std::length_error` without changes. Successful growth invalidates existing
  pointers. `append()`
  returns false without changes if input is invalid or capacity is insufficient.
  `peak(offset)` addresses allocated storage and returns `nullptr` when
  `offset >= capacity()`, including for a zero-capacity buffer.
- `basic::mutable_buffer_t` borrows storage; the caller owns its lifetime.

`detail::path_dir_name` extracts the lexical parent prefix. Null, empty and
separator-free paths return an empty string. Root paths return `/`.
A trailing separator retains the preceding directory, so `/a/b/` returns
`/a/b`. Repeated separators at the parent boundary are collapsed.

