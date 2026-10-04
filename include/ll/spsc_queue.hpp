#pragma once
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <type_traits>

#include "ll/platform.hpp"

namespace ll {

namespace detail {

// Padded: producer-owned and consumer-owned words each get their own cache
// line. Unpadded: all four words are packed together -> false sharing.
template <bool Padded>
struct SpscIndices;

template <>
struct SpscIndices<true> {
    alignas(kCacheLine) std::atomic<size_t> tail{0};  // written by producer
    size_t cached_head = 0;                           // producer-private
    alignas(kCacheLine) std::atomic<size_t> head{0};  // written by consumer
    size_t cached_tail = 0;                           // consumer-private
    alignas(kCacheLine) char end_pad[1] = {};
};

template <>
struct SpscIndices<false> {
    std::atomic<size_t> tail{0};
    size_t cached_head = 0;
    std::atomic<size_t> head{0};
    size_t cached_tail = 0;
};

}  // namespace detail

// Bounded single-producer/single-consumer ring buffer. Lock-free: push/pop are
// non-blocking and return false when full/empty (callers spin or back off).
//  - producer: relaxed load of own tail, acquire load of head (only when the
//    cached copy says "full"), release store of tail after the slot write.
//  - consumer: mirror image.
// Indices are free-running; capacity N must be a power of two.
template <class T, size_t N, bool Padded = true>
class SpscQueue {
    static_assert(N >= 2 && (N & (N - 1)) == 0, "N must be a power of two");
    static_assert(std::is_trivially_copyable_v<T>);

public:
    static constexpr size_t capacity() { return N; }

    bool push(const T& v) {
        const size_t t = idx_.tail.load(std::memory_order_relaxed);
        if (t - idx_.cached_head >= N) {
            idx_.cached_head = idx_.head.load(std::memory_order_acquire);
            if (t - idx_.cached_head >= N) return false;  // full
        }
        buf_[t & (N - 1)] = v;
        idx_.tail.store(t + 1, std::memory_order_release);
        return true;
    }

    bool pop(T& out) {
        const size_t h = idx_.head.load(std::memory_order_relaxed);
        if (h == idx_.cached_tail) {
            idx_.cached_tail = idx_.tail.load(std::memory_order_acquire);
            if (h == idx_.cached_tail) return false;  // empty
        }
        out = buf_[h & (N - 1)];
        idx_.head.store(h + 1, std::memory_order_release);
        return true;
    }

private:
    detail::SpscIndices<Padded> idx_;
    alignas(kCacheLine) T buf_[N];
};

}  // namespace ll
