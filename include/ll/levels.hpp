#pragma once
// Per-symbol aggregated price levels (L2). best() returns 0 when the side is empty.
#include <algorithm>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>

namespace ll {

// Baseline: red-black tree per side.
class MapLevels {
public:
    void add(bool buy, uint32_t price, uint32_t qty) { (buy ? bids_[price] : asks_[price]) += qty; }
    void reduce(bool buy, uint32_t price, uint32_t qty) {
        if (buy) dec(bids_, price, qty); else dec(asks_, price, qty);
    }
    uint32_t best_bid() const { return bids_.empty() ? 0 : bids_.begin()->first; }
    uint32_t best_ask() const { return asks_.empty() ? 0 : asks_.begin()->first; }
    static constexpr const char* name() { return "map"; }

private:
    template <class M>
    static void dec(M& m, uint32_t price, uint32_t qty) {
        auto it = m.find(price);
        if (it == m.end()) return;
        if (it->second <= qty) m.erase(it); else it->second -= qty;
    }
    std::map<uint32_t, uint32_t, std::greater<uint32_t>> bids_;
    std::map<uint32_t, uint32_t> asks_;
};

// Optimised: flat arrays indexed by (price/TICK - base) over a W-tick window
// centred on the first price seen, with cached best-bid/ask indices.
// Prices outside the window or off the tick grid (sub-penny) fall back to a
// std::map overflow, so behaviour is identical to MapLevels.
template <uint32_t TICK = 100, uint32_t W = 4096>
class FlatLevels {
public:
    void add(bool buy, uint32_t price, uint32_t qty) {
        if (!init_) init(price);
        int32_t i = index(price);
        if (i < 0) {
            (buy ? ob_bid_[price] : ob_ask_[price]) += qty;
            return;
        }
        if (buy) {
            bid_[i] += qty;
            if (i > best_bid_) best_bid_ = i;
        } else {
            ask_[i] += qty;
            if (best_ask_ < 0 || i < best_ask_) best_ask_ = i;
        }
    }

    void reduce(bool buy, uint32_t price, uint32_t qty) {
        int32_t i = init_ ? index(price) : -1;
        if (i < 0) {
            if (buy) dec(ob_bid_, price, qty); else dec(ob_ask_, price, qty);
            return;
        }
        if (buy) {
            uint32_t& q = bid_[i];
            q = q > qty ? q - qty : 0;
            if (q == 0 && i == best_bid_) {
                int32_t j = i - 1;
                while (j >= 0 && bid_[j] == 0) --j;
                best_bid_ = j;
            }
        } else {
            uint32_t& q = ask_[i];
            q = q > qty ? q - qty : 0;
            if (q == 0 && i == best_ask_) {
                int32_t j = i + 1;
                while (j < (int32_t)W && ask_[j] == 0) ++j;
                best_ask_ = j < (int32_t)W ? j : -1;
            }
        }
    }

    uint32_t best_bid() const {
        uint32_t w = best_bid_ >= 0 ? (base_ + (uint32_t)best_bid_) * TICK : 0;
        uint32_t o = ob_bid_.empty() ? 0 : ob_bid_.begin()->first;
        return std::max(w, o);
    }
    uint32_t best_ask() const {
        uint32_t w = best_ask_ >= 0 ? (base_ + (uint32_t)best_ask_) * TICK : 0;
        uint32_t o = ob_ask_.empty() ? 0 : ob_ask_.begin()->first;
        if (!w) return o;
        if (!o) return w;
        return std::min(w, o);
    }
    static constexpr const char* name() { return "flat"; }

private:
    void init(uint32_t price) {
        uint32_t t = price / TICK;
        base_ = t > W / 2 ? t - W / 2 : 0;
        bid_.reset(new uint32_t[W]());
        ask_.reset(new uint32_t[W]());
        init_ = true;
    }
    int32_t index(uint32_t price) const {
        if (price % TICK) return -1;
        uint32_t t = price / TICK;
        if (t < base_ || t >= base_ + W) return -1;
        return (int32_t)(t - base_);
    }
    template <class M>
    static void dec(M& m, uint32_t price, uint32_t qty) {
        auto it = m.find(price);
        if (it == m.end()) return;
        if (it->second <= qty) m.erase(it); else it->second -= qty;
    }

    bool init_ = false;
    uint32_t base_ = 0;
    int32_t best_bid_ = -1, best_ask_ = -1;
    std::unique_ptr<uint32_t[]> bid_, ask_;
    std::map<uint32_t, uint32_t, std::greater<uint32_t>> ob_bid_;
    std::map<uint32_t, uint32_t> ob_ask_;
};

}  // namespace ll
