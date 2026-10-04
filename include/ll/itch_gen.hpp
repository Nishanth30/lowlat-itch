#pragma once
// Deterministic synthetic ITCH 5.0 stream generator (integer-only maths and a
// hand-rolled PRNG, so output is bit-identical across compilers/platforms).
// Used for tests, replay regression and benchmarking when no real NASDAQ file
// is at hand. Also exposes the wire encoders for hand-built test messages.
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

namespace ll::itch::gen {

struct Buf {
    std::vector<uint8_t>& v;
    size_t start;
    explicit Buf(std::vector<uint8_t>& out) : v(out), start(out.size()) { v.push_back(0); v.push_back(0); }
    void u8(uint8_t x) { v.push_back(x); }
    void u16(uint16_t x) { v.push_back(x >> 8); v.push_back(x & 0xff); }
    void u32(uint32_t x) { u16(x >> 16); u16(x & 0xffff); }
    void u48(uint64_t x) { u16((x >> 32) & 0xffff); u32((uint32_t)x); }
    void u64(uint64_t x) { u32((uint32_t)(x >> 32)); u32((uint32_t)x); }
    void str(const char* s, size_t n) { for (size_t i = 0; i < n; ++i) v.push_back(i < std::strlen(s) ? (uint8_t)s[i] : ' '); }
    void header(char t, uint16_t loc, uint64_t ts) { u8((uint8_t)t); u16(loc); u16(0); u48(ts); }
    void finish() {
        size_t len = v.size() - start - 2;
        v[start] = (uint8_t)(len >> 8);
        v[start + 1] = (uint8_t)(len & 0xff);
    }
};

inline void add(std::vector<uint8_t>& o, uint64_t ts, uint16_t loc, uint64_t ref, char side,
                uint32_t shares, uint32_t price, bool mpid = false) {
    Buf b(o);
    b.header(mpid ? 'F' : 'A', loc, ts);
    b.u64(ref); b.u8((uint8_t)side); b.u32(shares);
    char sym[9]; std::snprintf(sym, sizeof sym, "S%05u", (unsigned)loc);
    b.str(sym, 8); b.u32(price);
    if (mpid) b.str("NSDQ", 4);
    b.finish();
}
inline void exec(std::vector<uint8_t>& o, uint64_t ts, uint16_t loc, uint64_t ref, uint32_t shares, uint64_t match) {
    Buf b(o); b.header('E', loc, ts); b.u64(ref); b.u32(shares); b.u64(match); b.finish();
}
inline void exec_price(std::vector<uint8_t>& o, uint64_t ts, uint16_t loc, uint64_t ref, uint32_t shares, uint64_t match, uint32_t price) {
    Buf b(o); b.header('C', loc, ts); b.u64(ref); b.u32(shares); b.u64(match); b.u8('Y'); b.u32(price); b.finish();
}
inline void cancel(std::vector<uint8_t>& o, uint64_t ts, uint16_t loc, uint64_t ref, uint32_t shares) {
    Buf b(o); b.header('X', loc, ts); b.u64(ref); b.u32(shares); b.finish();
}
inline void del(std::vector<uint8_t>& o, uint64_t ts, uint16_t loc, uint64_t ref) {
    Buf b(o); b.header('D', loc, ts); b.u64(ref); b.finish();
}
inline void replace(std::vector<uint8_t>& o, uint64_t ts, uint16_t loc, uint64_t ref, uint64_t nref, uint32_t shares, uint32_t price) {
    Buf b(o); b.header('U', loc, ts); b.u64(ref); b.u64(nref); b.u32(shares); b.u32(price); b.finish();
}
inline void trade(std::vector<uint8_t>& o, uint64_t ts, uint16_t loc, uint32_t shares, uint32_t price, uint64_t match) {
    Buf b(o); b.header('P', loc, ts); b.u64(0); b.u8('B'); b.u32(shares);
    b.str("TRADE", 8); b.u32(price); b.u64(match); b.finish();
}
inline void system_event(std::vector<uint8_t>& o, uint64_t ts, char code) {
    Buf b(o); b.header('S', 0, ts); b.u8((uint8_t)code); b.finish();
}
inline void stock_directory(std::vector<uint8_t>& o, uint64_t ts, uint16_t loc) {
    Buf b(o); b.header('R', loc, ts);
    char sym[9]; std::snprintf(sym, sizeof sym, "S%05u", (unsigned)loc);
    b.str(sym, 8);
    b.u8('Q'); b.u8('N'); b.u32(100); b.u8('N'); b.u8('C'); b.str("", 2); b.u8('P');
    b.u8('N'); b.u8('N'); b.u8('1'); b.u8('N'); b.u32(0); b.u8('N');
    b.finish();
}

struct Config {
    uint64_t msgs = 1'000'000;   // total frames (incl. R/S/P)
    uint32_t symbols = 500;
    uint64_t seed = 1;
    uint32_t target_live = 200'000;
};

struct Rng {
    uint64_t s;
    uint64_t next() {
        uint64_t z = (s += 0x9E3779B97F4A7C15ull);
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
        return z ^ (z >> 31);
    }
    uint64_t below(uint64_t n) { return n ? next() % n : 0; }
};

inline void generate(const Config& c, std::vector<uint8_t>& out) {
    struct Live { uint64_t ref; uint32_t price, shares; uint16_t loc; uint8_t buy; };
    Rng r{c.seed};
    std::vector<uint32_t> mid(c.symbols);  // cents
    for (auto& m : mid) m = 1000 + (uint32_t)r.below(20000);
    std::vector<Live> live;
    live.reserve(c.target_live * 2);
    out.reserve(c.msgs * 36);
    uint64_t ts = 4ull * 3600 * 1'000'000'000ull, ref = 1, match = 1, frames = 0;

    system_event(out, ts, 'O'); ++frames;
    for (uint32_t s = 1; s <= c.symbols && frames < c.msgs; ++s) { stock_directory(out, ts, (uint16_t)s); ++frames; }
    system_event(out, ts, 'Q'); ++frames;

    while (frames < c.msgs) {
        ts += 1 + r.below(1500);
        uint64_t rr = r.below(100);
        if (live.empty() || rr < (live.size() < c.target_live ? 55u : 35u)) {
            uint64_t sr = r.below(c.symbols);
            uint32_t sym = (uint32_t)(sr * sr / c.symbols);  // skew to low ids
            if (r.below(16) == 0) mid[sym] += (r.below(2) ? 1 : (mid[sym] > 100 ? -1 : 0));
            bool buy = r.below(2);
            uint32_t price;
            if (r.below(100) == 0) {
                price = 1 + (uint32_t)r.below(3'000'000);   // far / sub-penny
            } else {
                uint32_t off = 1 + (uint32_t)r.below(1 + r.below(40));
                uint32_t cents = buy ? (mid[sym] > off ? mid[sym] - off : 1) : mid[sym] + off;
                price = cents * 100;
            }
            uint32_t sh = 100 * (1 + (uint32_t)r.below(10));
            uint16_t loc = (uint16_t)(sym + 1);
            add(out, ts, loc, ref, buy ? 'B' : 'S', sh, price, r.below(10) == 0);
            live.push_back({ref++, price, sh, loc, (uint8_t)buy});
        } else if (r.below(60) == 0) {
            trade(out, ts, (uint16_t)(1 + r.below(c.symbols)), 100, 1000000, match++);
        } else {
            size_t k = (size_t)r.below(live.size());
            Live& o = live[k];
            uint64_t w = r.below(52);
            bool remove = false;
            if (w < 20) {                                   // delete
                del(out, ts, o.loc, o.ref);
                remove = true;
            } else if (w < 34) {                            // execute
                uint32_t q = (o.shares > 1 && r.below(3) == 0) ? 1 + (uint32_t)r.below(o.shares - 1) : o.shares;
                if (r.below(8) == 0) exec_price(out, ts, o.loc, o.ref, q, match++, o.price);
                else exec(out, ts, o.loc, o.ref, q, match++);
                o.shares -= q;
                remove = o.shares == 0;
            } else if (w < 42) {                            // cancel
                if (o.shares > 1) {
                    uint32_t q = 1 + (uint32_t)r.below(o.shares - 1);
                    cancel(out, ts, o.loc, o.ref, q);
                    o.shares -= q;
                } else {
                    del(out, ts, o.loc, o.ref);
                    remove = true;
                }
            } else {                                        // replace
                uint32_t sh = 100 * (1 + (uint32_t)r.below(10));
                int32_t d = (int32_t)r.below(5) - 2;
                uint32_t price = (uint32_t)((int64_t)o.price + (int64_t)d * 100);
                if (price == 0) price = 100;
                replace(out, ts, o.loc, o.ref, ref, sh, price);
                o.ref = ref++; o.price = price; o.shares = sh;
            }
            if (remove) { live[k] = live.back(); live.pop_back(); }
        }
        ++frames;
    }
}

}  // namespace ll::itch::gen
