#pragma once
#include <cstddef>

namespace ll {

// False-sharing granularity. Apple Silicon L1/L2 lines are 128 B; x86 and
// most AArch64 server parts are 64 B. Override with -DLL_CACHE_LINE=N.
#if defined(LL_CACHE_LINE)
inline constexpr std::size_t kCacheLine = LL_CACHE_LINE;
#elif defined(__APPLE__) && defined(__aarch64__)
inline constexpr std::size_t kCacheLine = 128;
#else
inline constexpr std::size_t kCacheLine = 64;
#endif

inline void cpu_relax() {
#if defined(__x86_64__) || defined(__i386__)
    __builtin_ia32_pause();
#elif defined(__aarch64__)
    asm volatile("yield" ::: "memory");
#endif
}

}  // namespace ll
