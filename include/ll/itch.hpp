#pragma once
// NASDAQ TotalView-ITCH 5.0 decoding (big-endian wire format).
// Only order-book-affecting messages are decoded: A F E C X D U.
// Everything else is reported as Ignored (framing is still honoured).
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace ll::itch {

inline uint16_t be16(const uint8_t* p) {
    uint16_t v;
    std::memcpy(&v, p, 2);
    return __builtin_bswap16(v);
}
inline uint32_t be32(const uint8_t* p) {
    uint32_t v;
    std::memcpy(&v, p, 4);
    return __builtin_bswap32(v);
}
inline uint64_t be64(const uint8_t* p) {
    uint64_t v;
    std::memcpy(&v, p, 8);
    return __builtin_bswap64(v);
}
inline uint64_t be48(const uint8_t* p) { return ((uint64_t)be16(p) << 32) | be32(p + 2); }

// Decoded, book-relevant event (40 B).
struct Event {
    uint64_t ref = 0;      // order reference (original ref for 'U')
    uint64_t new_ref = 0;  // 'U' only
    uint64_t ts = 0;       // ns since midnight, from the exchange
    uint32_t shares = 0;
    uint32_t price = 0;    // 1/10000 dollar
    uint16_t locate = 0;   // stock locate = symbol id
    char type = 0;
    char side = 0;         // 'B' / 'S' (adds only)
};

enum class Parse { Event, Ignored, Malformed };

inline Parse parse(const uint8_t* p, size_t len, Event& e) {
    if (len < 1) return Parse::Malformed;
    switch (p[0]) {
        case 'A':
        case 'F':
            if (len < (p[0] == 'A' ? 36u : 40u)) return Parse::Malformed;
            e.type = (char)p[0];
            e.locate = be16(p + 1);
            e.ts = be48(p + 5);
            e.ref = be64(p + 11);
            e.side = (char)p[19];
            e.shares = be32(p + 20);
            e.price = be32(p + 32);
            return Parse::Event;
        case 'E':
        case 'C':
            if (len < (p[0] == 'E' ? 31u : 36u)) return Parse::Malformed;
            e.type = (char)p[0];
            e.locate = be16(p + 1);
            e.ts = be48(p + 5);
            e.ref = be64(p + 11);
            e.shares = be32(p + 19);
            return Parse::Event;
        case 'X':
            if (len < 23) return Parse::Malformed;
            e.type = 'X';
            e.locate = be16(p + 1);
            e.ts = be48(p + 5);
            e.ref = be64(p + 11);
            e.shares = be32(p + 19);
            return Parse::Event;
        case 'D':
            if (len < 19) return Parse::Malformed;
            e.type = 'D';
            e.locate = be16(p + 1);
            e.ts = be48(p + 5);
            e.ref = be64(p + 11);
            return Parse::Event;
        case 'U':
            if (len < 35) return Parse::Malformed;
            e.type = 'U';
            e.locate = be16(p + 1);
            e.ts = be48(p + 5);
            e.ref = be64(p + 11);
            e.new_ref = be64(p + 19);
            e.shares = be32(p + 27);
            e.price = be32(p + 31);
            return Parse::Event;
        default:
            return Parse::Ignored;
    }
}

// Iterate NASDAQ sample-file framing: [u16 BE length][message] ...
template <class F>
inline size_t for_each_frame(const uint8_t* data, size_t size, F&& f) {
    size_t off = 0, n = 0;
    while (off + 2 <= size) {
        size_t len = be16(data + off);
        off += 2;
        if (off + len > size) break;
        f(data + off, len, n);
        off += len;
        ++n;
    }
    return n;
}

}  // namespace ll::itch
