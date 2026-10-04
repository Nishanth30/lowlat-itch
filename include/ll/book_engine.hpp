#pragma once
#include <vector>

#include "ll/itch.hpp"
#include "ll/levels.hpp"
#include "ll/order_store.hpp"

namespace ll {

struct EngineStats {
    uint64_t applied = 0, unknown_ref = 0, rejected = 0;
};

// Limit order book driven by ITCH events. Orders are tracked globally by
// reference (E/X/D/U carry only the ref); levels are per stock-locate.
// apply() returns a BBO word (bid<<32 | ask) for the touched symbol, which the
// runners fold into a deterministic checksum (replay regression + cross-impl
// equivalence) and which also forces the BBO read a strategy would do.
template <class OrderStore, class Levels>
class BookEngine {
public:
    explicit BookEngine(size_t max_orders) : orders_(max_orders), levels_(65536) {}

    uint64_t apply(const itch::Event& e) {
        uint16_t loc = e.locate;
        switch (e.type) {
            case 'A':
            case 'F': {
                bool buy = e.side == 'B';
                if (!orders_.insert({e.ref, e.price, e.shares, e.locate, (uint8_t)buy})) {
                    ++st_.rejected;
                    return 0;
                }
                levels_[loc].add(buy, e.price, e.shares);
                break;
            }
            case 'E':
            case 'C':
            case 'X': {
                Order* o = orders_.find(e.ref);
                if (!o) { ++st_.unknown_ref; return 0; }
                loc = o->locate;
                uint32_t q = e.shares < o->shares ? e.shares : o->shares;
                levels_[loc].reduce(o->side, o->price, q);
                o->shares -= q;
                if (o->shares == 0) orders_.erase(e.ref);
                break;
            }
            case 'D': {
                Order* o = orders_.find(e.ref);
                if (!o) { ++st_.unknown_ref; return 0; }
                loc = o->locate;
                levels_[loc].reduce(o->side, o->price, o->shares);
                orders_.erase(e.ref);
                break;
            }
            case 'U': {
                Order* o = orders_.find(e.ref);
                if (!o) { ++st_.unknown_ref; return 0; }
                loc = o->locate;
                bool buy = o->side;
                levels_[loc].reduce(buy, o->price, o->shares);
                orders_.erase(e.ref);
                if (!orders_.insert({e.new_ref, e.price, e.shares, loc, (uint8_t)buy})) {
                    ++st_.rejected;
                    break;
                }
                levels_[loc].add(buy, e.price, e.shares);
                break;
            }
            default:
                return 0;
        }
        ++st_.applied;
        const Levels& L = levels_[loc];
        return ((uint64_t)L.best_bid() << 32) | L.best_ask();
    }

    const EngineStats& stats() const { return st_; }
    const Levels& level(uint16_t loc) const { return levels_[loc]; }

private:
    OrderStore orders_;
    std::vector<Levels> levels_;
    EngineStats st_;
};

using BookMapHeap = BookEngine<HeapOrderStore, MapLevels>;
using BookMapPool = BookEngine<PoolOrderStore, MapLevels>;
using BookFlatHeap = BookEngine<HeapOrderStore, FlatLevels<>>;
using BookFlatPool = BookEngine<PoolOrderStore, FlatLevels<>>;

inline uint64_t fold(uint64_t h, uint64_t v) { return (h ^ v) * 0x100000001b3ull; }
inline constexpr uint64_t kFoldInit = 0xcbf29ce484222325ull;

}  // namespace ll
