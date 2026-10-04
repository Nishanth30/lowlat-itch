#pragma once
#include <cstddef>
#include <mutex>
#include <queue>

namespace ll {

// Baseline: std::mutex + std::queue, bounded to N for backpressure parity
// with the ring buffer. Consumer polls (try_pop), same as the SPSC consumer.
template <class T, size_t N>
class MutexQueue {
public:
    static constexpr size_t capacity() { return N; }

    bool push(const T& v) {
        std::lock_guard<std::mutex> g(m_);
        if (q_.size() >= N) return false;
        q_.push(v);
        return true;
    }
    bool pop(T& out) {
        std::lock_guard<std::mutex> g(m_);
        if (q_.empty()) return false;
        out = q_.front();
        q_.pop();
        return true;
    }

private:
    std::mutex m_;
    std::queue<T> q_;
};

}  // namespace ll

#include <condition_variable>

namespace ll {

// Second baseline: the textbook blocking design (mutex + condvar). The
// consumer sleeps in pop_wait(), so every message pays a futex wake-up.
template <class T, size_t N>
class CondQueue {
public:
    static constexpr size_t capacity() { return N; }

    bool push(const T& v) {
        {
            std::lock_guard<std::mutex> g(m_);
            if (q_.size() >= N) return false;
            q_.push(v);
        }
        cv_.notify_one();
        return true;
    }
    bool pop(T& out) {
        std::lock_guard<std::mutex> g(m_);
        if (q_.empty()) return false;
        out = q_.front();
        q_.pop();
        return true;
    }
    void pop_wait(T& out) {
        std::unique_lock<std::mutex> g(m_);
        cv_.wait(g, [&] { return !q_.empty(); });
        out = q_.front();
        q_.pop();
    }

private:
    std::mutex m_;
    std::condition_variable cv_;
    std::queue<T> q_;
};

}  // namespace ll
