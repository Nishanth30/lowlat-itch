#pragma once
#include <algorithm>
#include <cstdint>
#include <vector>

namespace ll {

struct LatencyStats {
    uint64_t n = 0, min = 0, p50 = 0, p90 = 0, p99 = 0, p999 = 0, p9999 = 0, max = 0;
    double mean = 0;
};

// Exact percentiles (nearest-rank) over raw samples in ns. Sorts in place.
inline LatencyStats summarize(std::vector<uint32_t>& v, size_t skip = 0) {
    LatencyStats s;
    if (v.size() <= skip) return s;
    auto first = v.begin() + (std::ptrdiff_t)skip;
    std::sort(first, v.end());
    const size_t n = v.size() - skip;
    auto at = [&](double q) {
        size_t rank = (size_t)(q * (double)n + 0.999999999);
        if (rank < 1) rank = 1;
        if (rank > n) rank = n;
        return (uint64_t)*(first + (std::ptrdiff_t)rank - 1);
    };
    double sum = 0;
    for (auto it = first; it != v.end(); ++it) sum += *it;
    s.n = n;
    s.min = *first;
    s.p50 = at(0.50);
    s.p90 = at(0.90);
    s.p99 = at(0.99);
    s.p999 = at(0.999);
    s.p9999 = at(0.9999);
    s.max = v.back();
    s.mean = sum / (double)n;
    return s;
}

inline uint32_t clamp_u32(uint64_t x) { return x > 0xFFFFFFFFull ? 0xFFFFFFFFu : (uint32_t)x; }

inline double median(std::vector<double> v) {
    std::sort(v.begin(), v.end());
    return v.empty() ? 0 : v[v.size() / 2];
}

}  // namespace ll
