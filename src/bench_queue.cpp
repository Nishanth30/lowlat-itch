// Stage 1: SPSC ring buffer vs mutex+queue. Latency percentiles (not means),
// false-sharing ablation (padded vs unpadded indices), optional core pinning.
#include <atomic>
#include <cstdio>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "ll/affinity.hpp"
#include "ll/cli.hpp"
#include "ll/clock.hpp"
#include "ll/mutex_queue.hpp"
#include "ll/platform.hpp"
#include "ll/spsc_queue.hpp"
#include "ll/stats.hpp"

using namespace ll;

namespace {

constexpr size_t kCap = 1 << 14;
struct Msg { uint64_t t_send, seq; };

struct Run {
    LatencyStats lat;
    double mops = 0;
    bool order_ok = true;
    std::string prod_pin, cons_pin;
};

// interval_ns == 0: unpaced (throughput run). Otherwise producer is paced and
// the latency sample is measured from the *intended* send time, so a stalled
// producer is not hidden (no coordinated omission).
template <class Q>
Run run(size_t msgs, uint64_t interval_ns, bool record, int pc, int cc) {
    auto q = std::make_unique<Q>();
    std::vector<uint32_t> lat(record ? msgs : 0);
    std::atomic<int> ready{0};
    std::atomic<bool> go{false};
    uint64_t t0 = 0, t_end = 0;
    Run r;
    std::thread cons([&] {
        r.cons_pin = pin_current_thread(cc);
        ready.fetch_add(1);
        while (!go.load(std::memory_order_acquire)) cpu_relax();
        Msg m;
        for (size_t i = 0; i < msgs;) {
            bool got;
            if constexpr (requires { q->pop_wait(m); }) { q->pop_wait(m); got = true; }
            else got = q->pop(m);
            if (got) {
                uint64_t now = Clock::now();
                if (m.seq != i) r.order_ok = false;
                if (record) lat[i] = clamp_u32(now > m.t_send ? now - m.t_send : 0);
                ++i;
            } else {
                cpu_relax();
            }
        }
        t_end = Clock::now();
    });
    std::thread prod([&] {
        r.prod_pin = pin_current_thread(pc);
        ready.fetch_add(1);
        while (!go.load(std::memory_order_acquire)) cpu_relax();
        t0 = Clock::now();
        for (size_t i = 0; i < msgs; ++i) {
            Msg m{0, i};
            if (interval_ns) {
                uint64_t target = t0 + i * interval_ns;
                while (Clock::now() < target) cpu_relax();
                m.t_send = target;
            } else {
                m.t_send = Clock::now();
            }
            while (!q->push(m)) cpu_relax();
        }
    });
    while (ready.load() < 2) std::this_thread::yield();
    go.store(true, std::memory_order_release);
    prod.join();
    cons.join();
    r.mops = (double)msgs / ((double)(t_end - t0) / 1e9) / 1e6;
    if (record) r.lat = summarize(lat, std::min<size_t>(msgs / 20, 100000));
    return r;
}

struct Agg {
    std::string variant;
    double p50, p99, p999, p9999, max, mean, mops;
    std::vector<Run> reps;
};

template <class Q>
Agg bench(const char* name, size_t msgs, size_t tput_msgs, uint64_t interval, int reps, int pc, int cc) {
    Agg a;
    a.variant = name;
    std::vector<double> p50, p99, p999, p9999, mx, mean, mops;
    for (int i = 0; i < reps; ++i) {
        Run r = run<Q>(msgs, interval, true, pc, cc);
        if (!r.order_ok) std::fprintf(stderr, "ORDER VIOLATION in %s\n", name);
        p50.push_back((double)r.lat.p50); p99.push_back((double)r.lat.p99);
        p999.push_back((double)r.lat.p999); p9999.push_back((double)r.lat.p9999);
        mx.push_back((double)r.lat.max); mean.push_back(r.lat.mean);
        a.reps.push_back(r);
        mops.push_back(run<Q>(tput_msgs, 0, false, pc, cc).mops);
    }
    a.p50 = median(p50); a.p99 = median(p99); a.p999 = median(p999); a.p9999 = median(p9999);
    a.max = median(mx); a.mean = median(mean); a.mops = median(mops);
    return a;
}

}  // namespace

int main(int argc, char** argv) {
    size_t msgs = cli::u64(argc, argv, "--msgs", 2'000'000);
    size_t tput = cli::u64(argc, argv, "--tput-msgs", 10'000'000);
    uint64_t rate = cli::u64(argc, argv, "--rate", 1'000'000);  // msgs/s, paced run
    int reps = cli::i32(argc, argv, "--reps", 5);
#if defined(__linux__)
    int pc = cli::i32(argc, argv, "--prod-core", num_cpus() >= 4 ? 2 : -1);
    int cc = cli::i32(argc, argv, "--cons-core", num_cpus() >= 4 ? 3 : -1);
#else
    int pc = cli::i32(argc, argv, "--prod-core", -1);
    int cc = cli::i32(argc, argv, "--cons-core", -1);
#endif
    std::string json = cli::str(argc, argv, "--json", "");
    uint64_t interval = rate ? 1'000'000'000ull / rate : 0;

    using Padded = SpscQueue<Msg, kCap, true>;
    using Unpadded = SpscQueue<Msg, kCap, false>;
    using Mtx = MutexQueue<Msg, kCap>;
    using Cnd = CondQueue<Msg, kCap>;

    std::printf("# queue bench: %zu paced msgs @ %llu msg/s, %zu unpaced msgs, %d reps (median of reps)\n",
                msgs, (unsigned long long)rate, tput, reps);
    std::printf("# cache line assumed: %zu B | clock tick: %.2f ns | cpus: %u\n", kCacheLine,
                Clock::resolution_ns(), num_cpus());

    std::vector<Agg> res;
    res.push_back(bench<Padded>("spsc-padded", msgs, tput, interval, reps, pc, cc));
    res.push_back(bench<Unpadded>("spsc-unpadded", msgs, tput, interval, reps, pc, cc));
    res.push_back(bench<Mtx>("mutex-poll", msgs, tput, interval, reps, pc, cc));
    res.push_back(bench<Cnd>("mutex-condvar", msgs, tput, interval, reps, pc, cc));

    std::printf("# threads: producer=%s | consumer=%s\n", res[0].reps[0].prod_pin.c_str(),
                res[0].reps[0].cons_pin.c_str());
    std::printf("\n%-15s %8s %8s %9s %10s %12s %10s\n", "variant", "p50 ns", "p99 ns", "p99.9 ns",
                "p99.99 ns", "max ns", "Mmsg/s");
    for (auto& a : res)
        std::printf("%-15s %8.0f %8.0f %9.0f %10.0f %12.0f %10.2f\n", a.variant.c_str(), a.p50, a.p99,
                    a.p999, a.p9999, a.max, a.mops);

    if (!json.empty()) {
        FILE* f = std::fopen(json.c_str(), "w");
        std::fprintf(f, "{\"meta\":{\"msgs\":%zu,\"rate\":%llu,\"tput_msgs\":%zu,\"reps\":%d,\"cache_line\":%zu,"
                        "\"tick_ns\":%.3f,\"producer\":\"%s\",\"consumer\":\"%s\"},\"results\":[",
                     msgs, (unsigned long long)rate, tput, reps, kCacheLine, Clock::resolution_ns(),
                     res[0].reps[0].prod_pin.c_str(), res[0].reps[0].cons_pin.c_str());
        for (size_t i = 0; i < res.size(); ++i) {
            auto& a = res[i];
            std::fprintf(f, "%s{\"variant\":\"%s\",\"p50\":%.0f,\"p99\":%.0f,\"p999\":%.0f,\"p9999\":%.0f,"
                            "\"max\":%.0f,\"mean\":%.1f,\"mops\":%.3f}",
                         i ? "," : "", a.variant.c_str(), a.p50, a.p99, a.p999, a.p9999, a.max, a.mean, a.mops);
        }
        std::fprintf(f, "]}\n");
        std::fclose(f);
    }
    return 0;
}
