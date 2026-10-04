#pragma once
#include <cstdint>
#include <cstring>
#include <memory>
#include <unordered_map>
#include <vector>

namespace ll {

struct Order {
    uint64_t ref;
    uint32_t price;
    uint32_t shares;
    uint16_t locate;
    uint8_t side;  // 1 = buy, 0 = sell
};

// Baseline: node-based hash map -> one malloc/free per order, pointer chasing.
class HeapOrderStore {
public:
    explicit HeapOrderStore(size_t max_orders) { m_.reserve(max_orders); }
    Order* insert(const Order& o) {
        auto [it, ok] = m_.emplace(o.ref, o);
        return ok ? &it->second : nullptr;
    }
    Order* find(uint64_t ref) {
        auto it = m_.find(ref);
        return it == m_.end() ? nullptr : &it->second;
    }
    void erase(uint64_t ref) { m_.erase(ref); }
    static constexpr const char* name() { return "heap"; }

private:
    std::unordered_map<uint64_t, Order> m_;
};

// Optimised: fixed pool (no allocation on the hot path) + open-addressing
// table (linear probing, backward-shift deletion, no tombstones).
class PoolOrderStore {
    static constexpr uint64_t kEmpty = ~0ull;
    struct Slot {
        uint64_t key;
        uint32_t idx;
    };

public:
    explicit PoolOrderStore(size_t max_orders) {
        cap_ = max_orders;
        bits_ = 1;
        while ((1ull << bits_) < 2 * max_orders) ++bits_;
        mask_ = (1ull << bits_) - 1;
        pool_.reset(new Order[cap_]);
        std::memset((void*)pool_.get(), 0, cap_ * sizeof(Order));  // pre-fault
        free_.reserve(cap_);
        tab_.reset(new Slot[mask_ + 1]);
        for (size_t i = 0; i <= mask_; ++i) tab_[i] = {kEmpty, 0};
    }

    Order* insert(const Order& o) {
        size_t i = slot_of(o.ref);
        while (tab_[i].key != kEmpty) {
            if (tab_[i].key == o.ref) return nullptr;  // duplicate
            i = (i + 1) & mask_;
        }
        uint32_t idx;
        if (!free_.empty()) {
            idx = free_.back();
            free_.pop_back();
        } else if (next_ < cap_) {
            idx = (uint32_t)next_++;
        } else {
            return nullptr;  // pool exhausted
        }
        pool_[idx] = o;
        tab_[i] = {o.ref, idx};
        return &pool_[idx];
    }

    Order* find(uint64_t ref) {
        size_t i = slot_of(ref);
        while (tab_[i].key != kEmpty) {
            if (tab_[i].key == ref) return &pool_[tab_[i].idx];
            i = (i + 1) & mask_;
        }
        return nullptr;
    }

    void erase(uint64_t ref) {
        size_t i = slot_of(ref);
        while (tab_[i].key != ref) {
            if (tab_[i].key == kEmpty) return;
            i = (i + 1) & mask_;
        }
        free_.push_back(tab_[i].idx);
        size_t j = i;
        for (;;) {
            j = (j + 1) & mask_;
            if (tab_[j].key == kEmpty) break;
            size_t k = slot_of(tab_[j].key);
            bool stay = (i <= j) ? (i < k && k <= j) : (i < k || k <= j);
            if (stay) continue;
            tab_[i] = tab_[j];
            i = j;
        }
        tab_[i].key = kEmpty;
    }
    static constexpr const char* name() { return "pool"; }

private:
    size_t slot_of(uint64_t k) const { return (size_t)((k * 0x9E3779B97F4A7C15ull) >> (64 - bits_)); }

    size_t cap_, next_ = 0, mask_;
    unsigned bits_;
    std::unique_ptr<Order[]> pool_;
    std::vector<uint32_t> free_;
    std::unique_ptr<Slot[]> tab_;
};

}  // namespace ll
