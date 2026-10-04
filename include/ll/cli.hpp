#pragma once
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>

namespace ll::cli {

inline const char* find(int argc, char** argv, const char* name) {
    for (int i = 1; i + 1 < argc; ++i)
        if (!std::strcmp(argv[i], name)) return argv[i + 1];
    return nullptr;
}
inline uint64_t u64(int argc, char** argv, const char* name, uint64_t def) {
    const char* v = find(argc, argv, name);
    return v ? std::strtoull(v, nullptr, 10) : def;
}
inline int i32(int argc, char** argv, const char* name, int def) {
    const char* v = find(argc, argv, name);
    return v ? std::atoi(v) : def;
}
inline std::string str(int argc, char** argv, const char* name, const char* def) {
    const char* v = find(argc, argv, name);
    return v ? v : def;
}

}  // namespace ll::cli
