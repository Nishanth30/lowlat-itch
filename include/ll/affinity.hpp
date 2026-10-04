#pragma once
#include <string>
#include <thread>

#if defined(__linux__)
#include <pthread.h>
#include <sched.h>
#elif defined(__APPLE__)
#include <pthread.h>
#include <pthread/qos.h>
#endif

namespace ll {

inline unsigned num_cpus() { return std::thread::hardware_concurrency(); }

// Pin the calling thread. Returns a human-readable status that the benches
// print, so results always say whether pinning was real.
inline std::string pin_current_thread(int core) {
    if (core < 0) return "unpinned";
#if defined(__linux__)
    cpu_set_t set;
    CPU_ZERO(&set);
    CPU_SET(core, &set);
    if (pthread_setaffinity_np(pthread_self(), sizeof(set), &set) == 0)
        return "pinned core " + std::to_string(core);
    return "pin FAILED core " + std::to_string(core);
#elif defined(__APPLE__)
    // macOS exposes no hard affinity (Apple Silicon ignores affinity tags).
    // Best effort: ask for the highest QoS so the scheduler prefers P-cores.
    pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE, 0);
    return "macOS: no hard pinning, QoS=user-interactive hint only";
#else
    return "pinning unsupported";
#endif
}

}  // namespace ll
