#ifndef _RING_BUFFER_H_
#define _RING_BUFFER_H_

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <utility>

// The default is suitable for the common x86 cache-line size. Override this
// value at compile time if the target platform uses a different size.
constexpr std::size_t ring_buffer_cache_line_size = 64;

static_assert(sizeof(std::atomic<std::uint64_t>) <= ring_buffer_cache_line_size,
              "atomic cursor must fit in one cache line");

template <typename T, std::size_t Capacity>
class RingBuffer {
  static_assert(Capacity >= 2, "Capacity must be at least two");
  static_assert((Capacity & (Capacity - 1)) == 0, "Capacity must be a power of two");

  public:
  RingBuffer() = default;

  RingBuffer(const RingBuffer &)            = delete;
  RingBuffer &operator=(const RingBuffer &) = delete;
  RingBuffer(RingBuffer &&)                 = delete;
  RingBuffer &operator=(RingBuffer &&)      = delete;

  ~RingBuffer() = default;

  bool try_push(const T &value) {
    const std::uint64_t tail = tail_.load(std::memory_order_relaxed);

    if (tail - cached_head_ >= Capacity) {
      cached_head_ = head_.load(std::memory_order_acquire);

      if (tail - cached_head_ >= Capacity) {
        return false;
      }
    }

    buffer_[tail & (Capacity - 1)] = value;
    tail_.store(tail + 1, std::memory_order_release);
    return true;
  }

  bool try_push(T &&value) {
    const std::uint64_t tail = tail_.load(std::memory_order_relaxed);

    if (tail - cached_head_ >= Capacity) {
      cached_head_ = head_.load(std::memory_order_acquire);

      if (tail - cached_head_ >= Capacity) {
        return false;
      }
    }

    buffer_[tail & (Capacity - 1)] = std::move(value);
    tail_.store(tail + 1, std::memory_order_release);
    return true;
  }

  template <typename... Args>
  bool try_emplace(Args &&...args) {
    const std::uint64_t tail = tail_.load(std::memory_order_relaxed);

    if (tail - cached_head_ >= Capacity) {
      cached_head_ = head_.load(std::memory_order_acquire);

      if (tail - cached_head_ >= Capacity) {
        return false;
      }
    }

    buffer_[tail & (Capacity - 1)] = T(std::forward<Args>(args)...);
    tail_.store(tail + 1, std::memory_order_release);
    return true;
  }

  // Single producer only. All-or-nothing: a failed call does not modify the
  // input or queue. The whole batch becomes visible with one tail publication.
  // count == 0 succeeds; count > Capacity fails.
  bool try_push_bulk(const T *values, std::size_t count) {
    if (count == 0) {
      return true;
    }
    if (count > Capacity) {
      return false;
    }

    const std::uint64_t tail = tail_.load(std::memory_order_relaxed);
    if (tail - cached_head_ > Capacity - count) {
      cached_head_ = head_.load(std::memory_order_acquire);
      if (tail - cached_head_ > Capacity - count) {
        return false;
      }
    }

    const std::size_t start = tail & (Capacity - 1);
    const std::size_t first =
        count < Capacity - start ? count : Capacity - start;
    for (std::size_t i = 0; i < first; ++i) {
      buffer_[start + i] = values[i];
    }
    for (std::size_t i = first; i < count; ++i) {
      buffer_[i - first] = values[i];
    }
    tail_.store(tail + count, std::memory_order_release);
    return true;
  }

  // Call from the single consumer thread only.
  bool try_pop(T &value) {
    const std::uint64_t head = head_.load(std::memory_order_relaxed);

    // Refresh the consumer's cached producer position only when the
    // cached value indicates that the queue may be empty.
    if (head == cached_tail_) {
      cached_tail_ = tail_.load(std::memory_order_acquire);

      if (head == cached_tail_) {
        return false;
      }
    }

    value = std::move(buffer_[head & (Capacity - 1)]);

    // Publish the free slot after the element has been consumed.
    head_.store(head + 1, std::memory_order_release);
    return true;
  }

  // Single consumer only. All-or-nothing: a failed call leaves the output
  // untouched. The caller must provide count live Ts. The slots become free
  // with one head publication. count == 0 succeeds; count > Capacity fails.
  bool try_pop_bulk(T *values, std::size_t count) {
    if (count == 0) {
      return true;
    }
    if (count > Capacity) {
      return false;
    }

    const std::uint64_t head = head_.load(std::memory_order_relaxed);
    if (cached_tail_ - head < count) {
      cached_tail_ = tail_.load(std::memory_order_acquire);
      if (cached_tail_ - head < count) {
        return false;
      }
    }

    const std::size_t start = head & (Capacity - 1);
    const std::size_t first =
        count < Capacity - start ? count : Capacity - start;
    for (std::size_t i = 0; i < first; ++i) {
      values[i] = std::move(buffer_[start + i]);
    }
    for (std::size_t i = first; i < count; ++i) {
      values[i] = std::move(buffer_[i - first]);
    }
    head_.store(head + count, std::memory_order_release);
    return true;
  }

  // This is a snapshot. Use try_push()/try_push_bulk() to check capacity.
  bool is_full() const noexcept {
    const std::uint64_t tail = tail_.load(std::memory_order_relaxed);
    const std::uint64_t head = head_.load(std::memory_order_acquire);
    return tail - head >= Capacity;
  }

  // This is a snapshot. Use try_pop()/try_pop_bulk() to check availability.
  bool is_empty() const noexcept {
    const std::uint64_t head = head_.load(std::memory_order_relaxed);
    const std::uint64_t tail = tail_.load(std::memory_order_acquire);
    return head == tail;
  }

  private:
  T buffer_[Capacity]{};

  // Each cursor and each thread-local cache occupies its own cache line.
  alignas(ring_buffer_cache_line_size) std::atomic<std::uint64_t> tail_{0};
  char tail_padding_[ring_buffer_cache_line_size - sizeof(std::atomic<std::uint64_t>)]{};

  alignas(ring_buffer_cache_line_size) std::atomic<std::uint64_t> head_{0};
  char head_padding_[ring_buffer_cache_line_size - sizeof(std::atomic<std::uint64_t>)]{};

  // Only the producer accesses cached_head_.
  alignas(ring_buffer_cache_line_size) std::uint64_t cached_head_{0};
  char cached_head_padding_[ring_buffer_cache_line_size - sizeof(std::uint64_t)]{};

  // Only the consumer accesses cached_tail_.
  alignas(ring_buffer_cache_line_size) std::uint64_t cached_tail_{0};
  char cached_tail_padding_[ring_buffer_cache_line_size - sizeof(std::uint64_t)]{};
};

#endif  // _RING_BUFFER_H_
