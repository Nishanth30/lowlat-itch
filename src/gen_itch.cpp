// Writes a synthetic ITCH 5.0 file (u16 length-prefixed frames, like NASDAQ's sample files).
#include <cstdio>
#include <string>
#include <vector>

#include "ll/cli.hpp"
#include "ll/itch_gen.hpp"

int main(int argc, char** argv) {
    ll::itch::gen::Config c;
    c.msgs = ll::cli::u64(argc, argv, "--msgs", 5'000'000);
    c.symbols = (uint32_t)ll::cli::u64(argc, argv, "--symbols", 500);
    c.seed = ll::cli::u64(argc, argv, "--seed", 1);
    c.target_live = (uint32_t)ll::cli::u64(argc, argv, "--live", 200'000);
    std::string out = ll::cli::str(argc, argv, "--out", "data/synthetic.itch");
    std::vector<uint8_t> buf;
    ll::itch::gen::generate(c, buf);
    FILE* f = std::fopen(out.c_str(), "wb");
    if (!f) { std::perror(out.c_str()); return 1; }
    std::fwrite(buf.data(), 1, buf.size(), f);
    std::fclose(f);
    std::printf("wrote %s: %llu msgs, %zu bytes, %u symbols, seed %llu\n", out.c_str(),
                (unsigned long long)c.msgs, buf.size(), c.symbols, (unsigned long long)c.seed);
    return 0;
}
