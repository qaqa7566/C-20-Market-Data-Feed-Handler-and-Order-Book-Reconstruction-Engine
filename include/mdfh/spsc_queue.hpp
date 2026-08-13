#pragma once

#include <atomic>
#include <cstddef>
#include <memory>
#include <new>
#include <type_traits>
#include <vector>

// Bounded single-producer / single-consumer lock-free ring buffer.
//
// This is *genuinely* lock-free for the SPSC case: exactly one thread calls
// try_push and exactly one calls try_pop. Correctness rests on a
// release-store / acquire-load pair on the head and tail indices -- the
// producer publishes a slot with a release store to head_, the consumer
// observes it with an acquire load (and vice-versa for free slots). No mutex is
// taken. It is NOT safe for multiple producers or multiple consumers; that
// restriction is the whole reason it can be lock-free, and it is stated plainly
// rather than dressed up.
namespace mdfh {

template <typename T>
class SPSCQueue {
    static_assert(std::is_nothrow_move_assignable_v<T> ||
                      std::is_trivially_copyable_v<T>,
                  "T must be cheaply movable");

public:
    // Capacity is rounded up to a power of two; one slot is reserved to
    // distinguish full from empty, so usable capacity is (rounded - 1).
    explicit SPSCQueue(std::size_t capacity)
        : capacity_(round_up_pow2(capacity < 2 ? 2 : capacity)),
          mask_(capacity_ - 1),
          buffer_(capacity_) {}

    SPSCQueue(const SPSCQueue&) = delete;
    SPSCQueue& operator=(const SPSCQueue&) = delete;

    [[nodiscard]] bool try_push(const T& item) {
        const std::size_t head = head_.load(std::memory_order_relaxed);
        const std::size_t next = (head + 1) & mask_;
        if (next == tail_.load(std::memory_order_acquire)) return false;  // full
        buffer_[head] = item;
        head_.store(next, std::memory_order_release);
        return true;
    }

    [[nodiscard]] bool try_pop(T& out) {
        const std::size_t tail = tail_.load(std::memory_order_relaxed);
        if (tail == head_.load(std::memory_order_acquire)) return false;  // empty
        out = buffer_[tail];
        tail_.store((tail + 1) & mask_, std::memory_order_release);
        return true;
    }

    [[nodiscard]] bool empty() const {
        return head_.load(std::memory_order_acquire) ==
               tail_.load(std::memory_order_acquire);
    }

    [[nodiscard]] std::size_t capacity() const noexcept { return capacity_ - 1; }

private:
    static std::size_t round_up_pow2(std::size_t v) {
        std::size_t p = 1;
        while (p < v) p <<= 1;
        return p;
    }

    // Cache-line padding to avoid false sharing between the producer's head_ and
    // the consumer's tail_. Hard-coded to 64 (the common x86-64 / AArch64 line
    // size) rather than std::hardware_destructive_interference_size, whose use
    // triggers an ABI-stability warning and can differ across translation units.
    static constexpr std::size_t kCacheLine = 64;

    const std::size_t   capacity_;
    const std::size_t   mask_;
    std::vector<T>      buffer_;
    alignas(kCacheLine) std::atomic<std::size_t> head_{0};  // producer writes
    alignas(kCacheLine) std::atomic<std::size_t> tail_{0};  // consumer writes
};

}  // namespace mdfh
