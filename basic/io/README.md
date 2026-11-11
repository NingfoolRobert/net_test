## I/O

The interfaces follow the inheritance used by the backends in `network/io`:

| Backend | Direction interface | Common base |
| --- | --- | --- |
| BSD multicast receiver, shared-memory receiver | `rx<Derived, Options>` | `io<Derived, Options>` |
| BSD multicast sender | `tx<Derived, Options>` | `io<Derived, Options>` |
| BSD TCP connection | `rtx<Derived, Options>` | `io<Derived, Options>` |

All templates are in `detail::io`. The common base in `io/io.h` owns configuration,
error state and the public `open()`/`close()` interface. It replaces the unused
`net::io` virtual interface; there is no runtime-polymorphic base. The generic
`io_cfg_t` is not used by this hierarchy: each backend supplies its own options.
`set_opt(const Options&)` accepts mutable, const and temporary options; `get_opt()`
has mutable and const-reference overloads. Configure before opening; mutating
options does not reconfigure an open resource and requires external synchronization.
`rx_shm` constructor options and legacy `set_cfg`/`get_cfg` use the same storage.

`rx` exposes `recv(char*, std::size_t)`, `tx` exposes
`send(const void*, std::size_t)`, and `rtx` exposes both operations.
Callers migrating from the old `rx.read`/`tx.write` API must use `recv`/`send`;
the old method names are not retained as aliases.
Each forwards to the corresponding `recv_impl` or `send_impl`
method and returns an `int` byte count or `-1` on failure. A nonzero length with a
null buffer fails with `EINVAL`. Lengths above `INT_MAX` fail with `EOVERFLOW`
before dispatch, preventing result truncation; negative signed lengths converted
to `std::size_t` also fail with `EOVERFLOW`. Null buffers with zero length are
forwarded to the backend. Previously `rx`/`tx` accepted `int` lengths and reported
negative lengths as `EINVAL`. The receive-only `rx_shm` no longer exposes its
placeholder `write()` operation.

All three interfaces expose `last_error()` (integer errno value) and `error_code()`
(`detail::io::ErrorCode`, defined in `io/error_code.h`). The I/O error wrapper
stores a native integer error value and exposes `value()`, `has_error()`, explicit
boolean conversion, equality comparison, and `message()` for an owned description.
It uses [strerror_r](https://man7.org/linux/man-pages/man3/strerror.3.html) on POSIX
and strerror_s on Windows; both formatting paths preserve the caller's errno.
A failed open or negative I/O result
records errno; a failure without errno records `EIO`. Successful operations,
including EOF, clear the saved error. Close implementations report failures by
setting errno. Query the error before another operation changes it:

```cpp
if (tcp.recv(buffer, sizeof(buffer)) < 0) {
    auto error = tcp.error_code();
    std::cerr << error.value() << ": " << error.message() << '\n';
}
```

The error belongs to the instance and remains valid after unrelated system calls.
Querying an error or its description does not reset it. A returned `ErrorCode`
is an independent snapshot: later successful operations clear the instance's
error, while earlier snapshots keep their original value. `close()` also updates
the instance's error; capture any prior failure before closing.
Concurrent operations share the latest completed operation's snapshot; snapshot
the error before another operation on that instance. Derived backends implement
`open_impl`/`close_impl` and the direction's `recv_impl`/`send_impl`.
Backends that support `recv_from` additionally implement `recv_from_impl`.
Private lifecycle hooks grant friendship to `io<Derived, Options>`; private
transfer hooks grant friendship to the corresponding direction interface.
The public interface belongs to the base so direct calls on a derived instance
also record errors. Derived destructors close their resources before destroying
their members; base constructors/destructors are protected to prevent creating
a standalone CRTP base object.

