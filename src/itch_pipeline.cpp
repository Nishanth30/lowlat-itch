// Stages 2+3: ITCH 5.0 replay -> parse -> (SPSC queue) -> limit order book.
//  single   : parse+apply on one thread; per-message cost.
//  pipeline : reader/parser thread -> SPSC queue -> book thread; latency is
//             measured from the message's (intended) wire-arrival time to the
//             moment the book is updated.
// Four book variants = {heap, pool order store} x {std::map, flat-array levels}.
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include <atomic>
#include <cstdio>
#include <string>
#include <thread>
#include <vector>

#include "ll/affinity.hpp"
#include "ll/book_engine.hpp"
#include "ll/cli.hpp"
#include "ll/clock.hpp"
#include "ll/spsc_queue.hpp"
#include "ll/stats.hpp"

using namespace ll;

namespace {

struct Slot { itch::Event e; uint64_t t0; };
using Queue = SpscQueue<Slot, 1 << 14, true>;

struct File {
    const uint8_t* data = nullptr;
    size_t size = 0;
};

File map_file(const char* path) {
    int fd = open(path, O_RDONLY);
    if (fd < 0) { std::perror(path); std::exit(1); }
    struct stat st;
    fstat(fd, &st);
    void* p = mmap(nullptr, (size_t)st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    if (p == MAP_FAILED) { std::perror("mmap"); std::exit(1); }
    close(fd);
    return {(const uint8_t*)p, (size_t)st.st_size};
}

struct Scan { uint64_t frames = 0, events = 0, ignored = 0, malformed = 0; uint64_t by_type[256] = {}; };

Scan prescan(const File& f) {  // also warms the page cache
    Scan s;
    itch::Event e;
    itch::for_each_frame(f.data, f.size, [&](const uint8_t* p, size_t len, size_t) {
        ++s.frames;
        if (len) ++s.by_type[p[0]];
        switch (itch::parse(p, len, e)) {
            case itch::Parse::Event: ++s.events; break;
            case itch::Parse::Ignored: ++s.ignored; break;
            default: ++s.malformed;
        }
    });
    return s;
}

struct Result {
    std::string book, mode;
    double ns_per_msg = 0, mmsg_s = 0;
    LatencyStats lat;
    uint64_t checksum = 0;
    EngineStats st;
};

template <class E>
Result run_single(const char* name, const File& f, const Scan& sc, size_t max_orders) {
    Result r;
    r.book = name; r.mode = "single";
    {   // (a) uninstrumented throughput
        auto eng = std::make_unique<E>(max_orders);
        itch::Event e;
        uint64_t h = kFoldInit;
        uint64_t t0 = Clock::now();
        itch::for_each_frame(f.data, f.size, [&](const uint8_t* p, size_t len, size_t) {
            if (itch::parse(p, len, e) == itch::Parse::Event) h = fold(h, eng->apply(e));
        });
        uint64_t dt = Clock::now() - t0;
        r.ns_per_msg = (double)dt / (double)sc.frames;
        r.mmsg_s = (double)sc.frames / ((double)dt / 1e9) / 1e6;
        r.checksum = h;
        r.st = eng->stats();
    }
    {   // (b) per-message parse->book-update latency
        auto eng = std::make_unique<E>(max_orders);
        std::vector<uint32_t> lat(sc.events);
        itch::Event e;
        size_t k = 0;
        itch::for_each_frame(f.data, f.size, [&](const uint8_t* p, size_t len, size_t) {
            uint64_t a = Clock::now();
            if (itch::parse(p, len, e) == itch::Parse::Event) {
                eng->apply(e);
                uint64_t b = Clock::now();
                if (k < lat.size()) lat[k++] = clamp_u32(b - a);
            }
        });
        lat.resize(k);
        r.lat = summarize(lat, k / 50);
    }
    return r;
}

template <class E>
Result run_pipeline(const char* name, const File& f, const Scan& sc, size_t max_orders, uint64_t rate,
                    int pc, int cc, std::string* pins) {
    Result r;
    r.book = name; r.mode = "pipeline";
    auto eng = std::make_unique<E>(max_orders);
    auto q = std::make_unique<Queue>();
    std::vector<uint32_t> lat(sc.events);
    std::atomic<int> ready{0};
    std::atomic<bool> go{false};
    uint64_t t_begin = 0, t_end = 0, h = kFoldInit;
    size_t got = 0;
    std::string cpin, ppin;

    std::thread cons([&] {
        cpin = pin_current_thread(cc);
        ready.fetch_add(1);
        while (!go.load(std::memory_order_acquire)) cpu_relax();
        Slot s;
        for (;;) {
            if (!q->pop(s)) { cpu_relax(); continue; }
            if (s.e.type == 0) break;  // sentinel
            uint64_t bbo = eng->apply(s.e);
            uint64_t now = Clock::now();
            h = fold(h, bbo);
            if (got < lat.size()) lat[got++] = clamp_u32(now > s.t0 ? now - s.t0 : 0);
        }
        t_end = Clock::now();
    });
    std::thread prod([&] {
        ppin = pin_current_thread(pc);
        ready.fetch_add(1);
        while (!go.load(std::memory_order_acquire)) cpu_relax();
        const uint64_t interval = rate ? 1'000'000'000ull / rate : 0;
        t_begin = Clock::now();
        Slot s{};
        itch::for_each_frame(f.data, f.size, [&](const uint8_t* p, size_t len, size_t i) {
            if (interval) {
                uint64_t target = t_begin + i * interval;
                while (Clock::now() < target) cpu_relax();
                s.t0 = target;
            } else {
                s.t0 = Clock::now();
            }
            if (itch::parse(p, len, s.e) == itch::Parse::Event)
                while (!q->push(s)) cpu_relax();
        });
        Slot end{};
        end.e.type = 0;
        while (!q->push(end)) cpu_relax();
    });
    while (ready.load() < 2) std::this_thread::yield();
    go.store(true, std::memory_order_release);
    prod.join();
    cons.join();
    if (pins) *pins = "producer=" + ppin + " | consumer=" + cpin;
    double secs = (double)(t_end - t_begin) / 1e9;
    r.mmsg_s = (double)sc.frames / secs / 1e6;
    r.ns_per_msg = secs * 1e9 / (double)sc.frames;
    lat.resize(got);
    r.lat = summarize(lat, got / 50);
    r.checksum = h;
    r.st = eng->stats();
    return r;
}

Result median_of(std::vector<Result>& v) {
    std::vector<double> a, b, c, d, e, g, mm, ns;
    for (auto& r : v) {
        a.push_back((double)r.lat.p50); b.push_back((double)r.lat.p99); c.push_back((double)r.lat.p999);
        d.push_back((double)r.lat.p9999); e.push_back((double)r.lat.max); g.push_back(r.lat.mean);
        mm.push_back(r.mmsg_s); ns.push_back(r.ns_per_msg);
    }
    Result o = v[0];
    o.lat.p50 = (uint64_t)median(a); o.lat.p99 = (uint64_t)median(b); o.lat.p999 = (uint64_t)median(c);
    o.lat.p9999 = (uint64_t)median(d); o.lat.max = (uint64_t)median(e); o.lat.mean = median(g);
    o.mmsg_s = median(mm); o.ns_per_msg = median(ns);
    return o;
}

}  // namespace

int main(int argc, char** argv) {
    std::string path = cli::str(argc, argv, "--file", "data/synthetic.itch");
    std::string mode = cli::str(argc, argv, "--mode", "both");
    std::string book = cli::str(argc, argv, "--book", "all");
    uint64_t rate = cli::u64(argc, argv, "--rate", 1'000'000);
    int reps = cli::i32(argc, argv, "--reps", 3);
    size_t max_orders = cli::u64(argc, argv, "--max-orders", 1u << 22);
    std::string json = cli::str(argc, argv, "--json", "");
#if defined(__linux__)
    int pc = cli::i32(argc, argv, "--prod-core", num_cpus() >= 4 ? 2 : -1);
    int cc = cli::i32(argc, argv, "--cons-core", num_cpus() >= 4 ? 3 : -1);
#else
    int pc = cli::i32(argc, argv, "--prod-core", -1);
    int cc = cli::i32(argc, argv, "--cons-core", -1);
#endif

    File f = map_file(path.c_str());
    Scan sc = prescan(f);
    std::printf("# file %s: %zu bytes, %llu frames, %llu book events, %llu ignored, %llu malformed\n",
                path.c_str(), f.size, (unsigned long long)sc.frames, (unsigned long long)sc.events,
                (unsigned long long)sc.ignored, (unsigned long long)sc.malformed);
    std::printf("# types:");
    for (int t = 0; t < 256; ++t) if (sc.by_type[t]) std::printf(" %c=%llu", t, (unsigned long long)sc.by_type[t]);
    std::printf("\n# clock tick %.2f ns | pipeline paced at %llu msg/s (0 = unpaced) | %d reps (median)\n",
                Clock::resolution_ns(), (unsigned long long)rate, reps);

    std::vector<Result> out;
    std::string pins;
    auto bench = [&](const char* name, auto tag) {
        using E = decltype(tag);
        if (book != "all" && book != name) return;
        if (mode == "single" || mode == "both") {
            std::vector<Result> v;
            for (int i = 0; i < reps; ++i) v.push_back(run_single<E>(name, f, sc, max_orders));
            out.push_back(median_of(v));
        }
        if (mode == "pipeline" || mode == "both") {
            std::vector<Result> v;
            for (int i = 0; i < reps; ++i) v.push_back(run_pipeline<E>(name, f, sc, max_orders, rate, pc, cc, &pins));
            out.push_back(median_of(v));
        }
    };
    bench("map-heap", BookMapHeap{1});
    bench("map-pool", BookMapPool{1});
    bench("flat-heap", BookFlatHeap{1});
    bench("flat-pool", BookFlatPool{1});
    if (!pins.empty()) std::printf("# threads: %s\n", pins.c_str());

    std::printf("\n%-10s %-9s %9s %8s %8s %9s %10s %11s %9s  %s\n", "book", "mode", "ns/msg", "p50 ns", "p99 ns",
                "p99.9 ns", "p99.99 ns", "max ns", "Mmsg/s", "checksum");
    bool mismatch = false;
    for (auto& r : out) {
        std::printf("%-10s %-9s %9.1f %8llu %8llu %9llu %10llu %11llu %9.2f  %016llx\n", r.book.c_str(),
                    r.mode.c_str(), r.ns_per_msg, (unsigned long long)r.lat.p50, (unsigned long long)r.lat.p99,
                    (unsigned long long)r.lat.p999, (unsigned long long)r.lat.p9999, (unsigned long long)r.lat.max,
                    r.mmsg_s, (unsigned long long)r.checksum);
        if (r.checksum != out[0].checksum) mismatch = true;
    }
    std::printf("# applied=%llu unknown_ref=%llu rejected=%llu | checksums %s\n",
                out.empty() ? 0ull : (unsigned long long)out[0].st.applied,
                out.empty() ? 0ull : (unsigned long long)out[0].st.unknown_ref,
                out.empty() ? 0ull : (unsigned long long)out[0].st.rejected,
                mismatch ? "MISMATCH (BUG)" : "all equal");

    if (!json.empty()) {
        FILE* jf = std::fopen(json.c_str(), "w");
        std::fprintf(jf, "{\"meta\":{\"file\":\"%s\",\"frames\":%llu,\"events\":%llu,\"rate\":%llu,\"reps\":%d,"
                         "\"tick_ns\":%.3f,\"threads\":\"%s\"},\"results\":[",
                     path.c_str(), (unsigned long long)sc.frames, (unsigned long long)sc.events,
                     (unsigned long long)rate, reps, Clock::resolution_ns(), pins.c_str());
        for (size_t i = 0; i < out.size(); ++i) {
            auto& r = out[i];
            std::fprintf(jf, "%s{\"book\":\"%s\",\"mode\":\"%s\",\"ns_per_msg\":%.2f,\"p50\":%llu,\"p99\":%llu,"
                             "\"p999\":%llu,\"p9999\":%llu,\"max\":%llu,\"mmsg_s\":%.3f,\"checksum\":\"%016llx\"}",
                         i ? "," : "", r.book.c_str(), r.mode.c_str(), r.ns_per_msg, (unsigned long long)r.lat.p50,
                         (unsigned long long)r.lat.p99, (unsigned long long)r.lat.p999,
                         (unsigned long long)r.lat.p9999, (unsigned long long)r.lat.max, r.mmsg_s,
                         (unsigned long long)r.checksum);
        }
        std::fprintf(jf, "]}\n");
        std::fclose(jf);
    }
    return mismatch ? 2 : 0;
}
