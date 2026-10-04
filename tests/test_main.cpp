#include <cstdio>
#include <cstdlib>
#include <thread>
#include <vector>

#include "ll/book_engine.hpp"
#include "ll/itch.hpp"
#include "ll/itch_gen.hpp"
#include "ll/mutex_queue.hpp"
#include "ll/spsc_queue.hpp"

using namespace ll;

#define CHECK(c)                                                                  \
    do {                                                                          \
        if (!(c)) {                                                               \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c);     \
            std::exit(1);                                                         \
        }                                                                         \
    } while (0)

// ---------- queues ----------
template <class Q>
void test_queue(const char* name) {
    {
        Q q;
        uint64_t v = 0;
        CHECK(!q.pop(v));
        for (uint64_t i = 0; i < Q::capacity(); ++i) CHECK(q.push(i));
        CHECK(!q.push(999));  // full
        for (uint64_t i = 0; i < Q::capacity(); ++i) { CHECK(q.pop(v)); CHECK(v == i); }
        CHECK(!q.pop(v));
    }
    const uint64_t N = 3'000'000;
    auto q = std::make_unique<Q>();
    bool ok = true;
    std::thread c([&] {
        uint64_t v, expect = 0;
        while (expect < N) if (q->pop(v)) { if (v != expect) ok = false; ++expect; }
    });
    for (uint64_t i = 0; i < N; ++i) while (!q->push(i)) {}
    c.join();
    CHECK(ok);
    std::printf("ok  queue %s\n", name);
}

// ---------- order book ----------
namespace g = itch::gen;

template <class E>
void test_handbuilt(const char* name) {
    std::vector<uint8_t> buf;
    uint64_t ts = 1;
    auto bbo = [](uint32_t b, uint32_t a) { return ((uint64_t)b << 32) | a; };
    // (frame bytes, expected BBO after apply)
    struct Step { size_t from, to; uint64_t want; };
    std::vector<Step> steps;
    auto step = [&](auto&& emit, uint64_t want) {
        size_t s = buf.size();
        emit();
        steps.push_back({s, buf.size(), want});
    };
    const uint16_t L = 7;
    step([&] { g::add(buf, ts, L, 1, 'B', 100, 1000000); }, bbo(1000000, 0));
    step([&] { g::add(buf, ts, L, 2, 'B', 50, 1000100); }, bbo(1000100, 0));
    step([&] { g::add(buf, ts, L, 3, 'S', 70, 1001000); }, bbo(1000100, 1001000));
    step([&] { g::exec(buf, ts, L, 2, 20, 1); }, bbo(1000100, 1001000));       // 30 left
    step([&] { g::cancel(buf, ts, L, 2, 10); }, bbo(1000100, 1001000));        // 20 left
    step([&] { g::del(buf, ts, L, 2); }, bbo(1000000, 1001000));               // level gone
    step([&] { g::replace(buf, ts, L, 3, 4, 30, 1000500); }, bbo(1000000, 1000500));
    step([&] { g::add(buf, ts, L, 5, 'B', 10, 1000050, true); }, bbo(1000050, 1000500));  // sub-penny
    step([&] { g::del(buf, ts, L, 5); }, bbo(1000000, 1000500));
    step([&] { g::add(buf, ts, L, 6, 'S', 10, 2000000); }, bbo(1000000, 1000500));        // far ask
    step([&] { g::del(buf, ts, L, 4); }, bbo(1000000, 2000000));               // falls to far ask
    step([&] { g::exec_price(buf, ts, L, 1, 100, 2, 1000000); }, bbo(0, 2000000));
    step([&] { g::del(buf, ts, L, 12345); }, 0);                                // unknown ref
    E eng(1 << 10);
    itch::Event e;
    for (auto& s : steps) {
        CHECK(buf[s.from] == 0 && buf[s.from + 1] == s.to - s.from - 2);
        CHECK(itch::parse(&buf[s.from + 2], s.to - s.from - 2, e) == itch::Parse::Event);
        CHECK(eng.apply(e) == s.want);
    }
    CHECK(eng.stats().unknown_ref == 1);
    std::printf("ok  hand-built book %s\n", name);
}

uint64_t replay_checksum(const std::vector<uint8_t>& stream, auto& eng, uint64_t* malformed = nullptr) {
    uint64_t h = kFoldInit, bad = 0;
    itch::Event e;
    itch::for_each_frame(stream.data(), stream.size(), [&](const uint8_t* p, size_t len, size_t) {
        switch (itch::parse(p, len, e)) {
            case itch::Parse::Event: h = fold(h, eng.apply(e)); break;
            case itch::Parse::Malformed: ++bad; break;
            default: break;
        }
    });
    if (malformed) *malformed = bad;
    return h;
}

// Replay regression: deterministic stream -> checksum must equal the golden
// value, and every book implementation must agree with every other.
constexpr uint64_t kGolden = 0xde293df08a4fa812ull;  // regenerate only on intentional semantic change

void test_replay() {
    g::Config c;
    c.msgs = 400'000; c.symbols = 40; c.seed = 42; c.target_live = 8'000;
    std::vector<uint8_t> s;
    g::generate(c, s);
    BookMapHeap a(1 << 20); BookMapPool b(1 << 20); BookFlatHeap d(1 << 20); BookFlatPool f(1 << 20);
    uint64_t bad = 0;
    uint64_t ha = replay_checksum(s, a, &bad);
    CHECK(bad == 0);
    CHECK(replay_checksum(s, b) == ha);
    CHECK(replay_checksum(s, d) == ha);
    CHECK(replay_checksum(s, f) == ha);
    CHECK(a.stats().unknown_ref == 0 && a.stats().rejected == 0);
    CHECK(f.stats().unknown_ref == 0 && f.stats().rejected == 0);
    std::printf("ok  replay: 4 book variants agree, checksum %016llx\n", (unsigned long long)ha);
    if (kGolden) CHECK(ha == kGolden);
}

void test_pool_store() {  // churn: insert/erase with colliding keys, compare to heap store
    PoolOrderStore p(4096);
    HeapOrderStore h(4096);
    g::Rng r{9};
    std::vector<uint64_t> keys;
    for (int i = 0; i < 200'000; ++i) {
        if (keys.size() < 2000 && (keys.empty() || r.below(100) < 52)) {
            uint64_t k = 1 + r.below(1u << 20) * 4096;  // many collisions
            Order o{k, (uint32_t)i, 1, 0, 1};
            bool a = p.insert(o) != nullptr, b = h.insert(o) != nullptr;
            CHECK(a == b);
            if (a) keys.push_back(k);
        } else if (!keys.empty()) {
            size_t i2 = r.below(keys.size());
            uint64_t k = keys[i2];
            CHECK(p.find(k) && h.find(k) && p.find(k)->price == h.find(k)->price);
            p.erase(k); h.erase(k);
            CHECK(!p.find(k));
            keys[i2] = keys.back(); keys.pop_back();
        }
        if (!keys.empty()) { uint64_t k = keys[r.below(keys.size())]; CHECK(p.find(k)); }
    }
    std::printf("ok  pool order store\n");
}

int main() {
    test_queue<SpscQueue<uint64_t, 1024, true>>("spsc padded");
    test_queue<SpscQueue<uint64_t, 1024, false>>("spsc unpadded");
    test_queue<MutexQueue<uint64_t, 1024>>("mutex");
    test_handbuilt<BookMapHeap>("map-heap");
    test_handbuilt<BookMapPool>("map-pool");
    test_handbuilt<BookFlatHeap>("flat-heap");
    test_handbuilt<BookFlatPool>("flat-pool");
    test_pool_store();
    test_replay();
    std::printf("ALL PASSED\n");
    return 0;
}
