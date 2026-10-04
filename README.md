# lowlat-itch

**A low-latency market-data pipeline in C++20: NASDAQ ITCH 5.0 decoding, a wait-free handoff queue, and a limit order book that updates in under 30 ns per message.**

When an exchange publishes a trade or a quote, trading firms race to react. The software that receives the feed, decodes
it and keeps an up-to-date picture of every order in the market (the *order book*) sits on the critical path of every
strategy, and its speed is measured in nanoseconds. Not just on average: a single slow message in a thousand can cost
more than all the fast ones earn. This project builds that path end to end and measures it the way a trading firm would,
by tail percentiles (p99, p99.9) rather than averages.

```
 ITCH 5.0 feed          parse                 handoff                  book update
 (file / replay) ──►  zero-copy decode ──►  lock-free SPSC queue ──►  limit order book ──► best bid / ask
                      reader thread          acquire/release           book thread
                      (core A)               no locks, no syscalls     (core B)
```

## Results

Pinned to separate cores on Linux (Ubuntu 24.04, aarch64, Docker VM); macOS (Apple M5) shown where noted.

### Handoff between threads: wait-free queue vs. mutex

1 M messages/s, latency from intended send to receive, median of 5 runs.

| Queue | p50 | p99 | p99.9 | Throughput |
|---|---|---|---|---|
| **Wait-free SPSC ring buffer** | **83 ns** | **1.9 µs** | 13.7 µs | **18.6 M msg/s** |
| `std::mutex` + `std::queue` (polling) | 583 ns | 10.8 µs | 53.5 µs | 6.4 M msg/s |
| `std::mutex` + condition variable | 3.2 µs | 12.8 µs | 48.0 µs | 6.5 M msg/s |

The wait-free queue has a median 7–38× lower, p99 about 6× lower, and about 3× the throughput.

### Order book: what each design choice buys

3 M ITCH messages, ~200 k live orders, single thread, parse + book update, macOS (Apple M5):

| Order store | Price levels | ns / message | M msg/s |
|---|---|---|---|
| heap hash map | `std::map` | 72.7 | 13.8 |
| fixed pool + open addressing | `std::map` | 56.8 | 17.6 |
| heap hash map | flat tick array | 43.3 | 23.1 |
| **fixed pool + open addressing** | **flat tick array** | **28.5** | **35.1** |

**2.5× faster** end to end. All four implementations produce bit-identical output on every test stream.

A finding worth calling out: the pooled order store only beats the heap when it is sized to the working set. At 4 M
capacity its hash table is 128 MB for ~200 k live orders, every probe misses cache, and the speed-up vanishes (72.2 vs
75.1 ns). At 512 k capacity (16 MB) it delivers the gain above. Cache footprint, not allocation count, was the
dominant cost.

Full tables for both machines: [results/REPORT_linux-docker.md](results/REPORT_linux-docker.md), [results/REPORT_macos.md](results/REPORT_macos.md).

## What is in here

**Wait-free SPSC ring buffer** ([spsc_queue.hpp](include/ll/spsc_queue.hpp)). `push` and `pop` each complete in a
bounded number of steps (no loops, CAS retries, locks or syscalls) and return immediately when the queue is full or
empty, over a power-of-two ring with free-running indices. The producer owns `tail`, the consumer owns `head`; a release store
publishes each slot and an acquire load observes it. Each side keeps a cached copy of the other's index so the shared
cache line is touched only when the queue looks full or empty. Producer and consumer words can be placed on separate
cache lines or packed together, to measure false sharing directly. The cache-line size is platform-aware (128 B on
Apple Silicon, 64 B elsewhere).

**Full-queue policy.** The ring is bounded, so a full queue is a decision, not a hidden wait. The pipeline's default
for paced (real-time) replay is *fail-fast*: the first overflow is counted, the run aborts with a distinct exit code and
nothing is dropped silently, matching how a production feed handler would raise a fault or trip a kill-switch rather
than corrupt the book. Unpaced throughput runs use backpressure (`--on-full spin`).

**ITCH 5.0 decoder** ([itch.hpp](include/ll/itch.hpp)). Zero-copy, big-endian decoding of the messages that change the
book (Add, Add-with-MPID, Execute, Execute-with-price, Cancel, Delete, Replace) with length-prefixed framing identical
to NASDAQ's published sample files. Truncated messages are detected, not trusted.

**Limit order book** ([book_engine.hpp](include/ll/book_engine.hpp)). Tracks every order by reference and aggregates
price levels per symbol. Two interchangeable order stores and two interchangeable level structures are composed through
templates, so every combination runs the same code with no virtual dispatch:
- *Order store:* `std::unordered_map`, or a preallocated pool with an open-addressing table (linear probing,
  backward-shift deletion, no tombstones, no allocation on the hot path).
- *Price levels:* `std::map`, or a flat array indexed by tick over a window around the market, with cached best
  bid/ask. Prices outside the window or off the tick grid spill to an overflow map, so behaviour is identical.

**Replay-regression harness** ([itch_gen.hpp](include/ll/itch_gen.hpp), [tests](tests/test_main.cpp)). A deterministic
ITCH generator (integer arithmetic and a hand-written PRNG, so output is bit-identical across compilers and platforms)
produces realistic message mixes including sub-penny and far-from-touch prices. A golden checksum of the best bid/ask
after every message pins the exact behaviour: any change to book semantics fails the test. The same harness proves
that all four book implementations agree.

**Measurement methodology** ([bench_queue.cpp](src/bench_queue.cpp), [itch_pipeline.cpp](src/itch_pipeline.cpp)).
- Exact percentiles over every sample (p50 / p99 / p99.9 / p99.99 / max), never means.
- Latency measured from the *intended* send time, so a stalled producer shows up as latency instead of disappearing
  (avoids coordinated omission).
- Hardware cycle counters for timestamps (`rdtsc` on x86, `cntvct_el0` on ARM), core pinning on Linux, medians over
  repeated runs, and a throughput run separate from the latency run so timer overhead does not skew either.

## Correctness

- Queue: FIFO order verified over 3 M items across two threads, plus full/empty edge cases, for all three queue types.
- Order store: fuzzed against `std::unordered_map` with deliberately colliding keys across 200 k insert/erase operations.
- Book: a hand-built scenario covers partial fills, cancels, deletes, replaces, sub-penny prices, prices far outside the
  flat window, and unknown order references, on all four implementations.
- Replay: 400 k-message deterministic stream, golden checksum, all variants must match.

## Scope and limits

- The benchmark feed is a deterministic synthetic ITCH 5.0 stream; the decoder uses NASDAQ's wire format and file
  framing and can replay NASDAQ sample files directly.
- Linux numbers come from a Docker VM with pinned cores and a 41.67 ns clock tick; macOS has no hard thread pinning.
  Tail percentiles on bare metal with isolated cores will be tighter.
- Out of scope: kernel-bypass networking (DPDK/RDMA), FPGA offload, order-entry protocols (OUCH/FIX), exchange-specific
  feeds (NSE/BSE).

## Build and reproduce

Requires a C++20 compiler and CMake.

```bash
scripts/run_all.sh                        # build, test, generate data, benchmark, write results/REPORT_<tag>.md
PROD_CORE=2 CONS_CORE=4 scripts/run_all.sh linux   # Linux: pinned cores
docker build -t lowlat-itch . && docker run --rm -e PROD_CORE=2 -e CONS_CORE=4 -v "$PWD/results:/work/results" lowlat-itch
scripts/perf_stat.sh                      # Linux: hardware counters per book variant
scripts/fetch_nasdaq_sample.sh <file>.NASDAQ_ITCH50.gz   # optional: real NASDAQ sample day
```
