#pragma once
#include <chrono>
#include <cstdint>

#if defined(__x86_64__)
#include <x86intrin.h>
#endif

namespace ll {

// Raw hardware counter -> nanoseconds. x86: TSC (calibrated at startup).
// AArch64: cntvct_el0 (24 MHz on Apple Silicon => 41.67 ns tick, so latencies
// there are quantised to that step; Linux AArch64/x86 are far finer).
namespace detail {

inline uint64_t read_ticks() {
#if defined(__x86_64__)
    _mm_lfence();
    return __rdtsc();
#elif defined(__aarch64__)
    uint64_t v;
    asm volatile("isb\n\tmrs %0, cntvct_el0" : "=r"(v)::"memory");
    return v;
#else
    return (uint64_t)std::chrono::steady_clock::now().time_since_epoch().count();
#endif
}

inline double calibrate_ns_per_tick() {
#if defined(__aarch64__)
    uint64_t f;
    asm volatile("mrs %0, cntfrq_el0" : "=r"(f));
    return 1e9 / (double)f;
#elif defined(__x86_64__)
    using SC = std::chrono::steady_clock;
    auto t0 = SC::now();
    uint64_t c0 = read_ticks();
    while (SC::now() - t0 < std::chrono::milliseconds(50)) {
    }
    auto t1 = SC::now();
    uint64_t c1 = read_ticks();
    double ns = (double)std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count();
    return ns / (double)(c1 - c0);
#else
    return 1.0;
#endif
}

inline const double g_ns_per_tick = calibrate_ns_per_tick();

}  // namespace detail

struct Clock {
    static uint64_t now() { return (uint64_t)((double)detail::read_ticks() * detail::g_ns_per_tick); }
    static double resolution_ns() { return detail::g_ns_per_tick; }
};

}  // namespace ll
