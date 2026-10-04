# lowlat-itch — lock-free handoff + NASDAQ ITCH 5.0 order book (C++20)

Market-data ingestion pipeline in the shape of an HFT feed handler:

```
ITCH 5.0 file/replay ─► parser (zero-copy, big-endian) ─► SPSC ring buffer ─► limit order book ─► BBO
        reader thread (core A)                              acquire/release        book thread (core B)
```

| Stage | What | Where |
|---|---|---|
| 1 | Wait-free SPSC ring buffer (`std::atomic`, acquire/release, cached indices), cache-line padding on/off (false-sharing ablation), mutex+queue and mutex+condvar baselines, p50/p99/p99.9/p99.99/max, optional core pinning, no coordinated omission | `include/ll/spsc_queue.hpp`, `src/bench_queue.cpp` |
| 2 | ITCH 5.0 decoder (A F E C X D U + framing; everything else skipped), order book with add/execute/cancel/delete/replace, per-message parse→book-update latency | `include/ll/itch.hpp`, `book_engine.hpp`, `src/itch_pipeline.cpp` |
| 3 | 2×2 ablation: order store {`unordered_map` (heap) vs fixed pool + open-addressing table} × price levels {`std::map` vs flat tick array with cached BBO + overflow map}; `perf stat` script | `order_store.hpp`, `levels.hpp`, `scripts/perf_stat.sh` |
| — | Replay regression: deterministic synthetic ITCH generator, golden BBO checksum, all 4 book variants must agree bit-for-bit | `itch_gen.hpp`, `tests/test_main.cpp` |

## Run

```bash
scripts/run_all.sh            # build, test, generate 3M-msg ITCH file, run everything, write results/REPORT_<tag>.md
```

Real NASDAQ data: `scripts/fetch_nasdaq_sample.sh <file>.NASDAQ_ITCH50.gz` (multi-GB), then
`ITCH_FILE=data/<file> scripts/run_all.sh real`. The parser consumes the same `[u16 len][msg]` framing as NASDAQ's sample files.

Linux (real pinning + `perf`): `PROD_CORE=2 CONS_CORE=4 scripts/run_all.sh linux` (pick two *physical* cores, `lscpu -e`;
isolate them with `isolcpus=` for best tails), then `scripts/perf_stat.sh`. A `Dockerfile` is included for a Linux VM
on a Mac (pinning works inside the VM; no reliable PMU, so use a real box for `perf`).

## Results — Apple M5, macOS, **unpinned** (indicative only, see caveats)

Full tables: [results/REPORT_macos.md](results/REPORT_macos.md). Raw JSON next to it.

**Queue handoff** (1 M msg/s paced, median of 3 reps; latency = intended send → consumer receive):

| variant | p50 | p99 | p99.9 | Mmsg/s unpaced |
|---|---|---|---|---|
| spsc-padded | 121 ns | 226 ns | 8.8 µs | 13.6 |
| spsc-unpadded | 144 ns | 280 ns | 8.4 µs | 14.4 |
| mutex + condvar | 2.2 µs | 9.2 µs | 13.4 µs | 6.6 |
| mutex + polling | 3.8 ms | 23.9 ms | 31 ms | (unfair-lock starvation; pathological, not a fair baseline) |

**Order book** (3.0 M ITCH frames, 2.97 M book events, ~200 k live orders; ns per message incl. parse; all four checksums identical):

| book | ns/msg | Mmsg/s |
|---|---|---|
| std::map levels + heap orders | 76.1 | 13.1 |
| std::map levels + pool orders | 67.0 | 14.9 |
| flat levels + heap orders | 43.6 | 22.9 |
| flat levels + pool orders | 43.3 | 23.1 |

Takeaways that the numbers support: lock-free SPSC beats a blocking mutex queue by ~18× at p50 / ~40× at p99;
flat tick-indexed levels cut book-update cost ~1.75× vs `std::map` (and 1.36× end-to-end through the queue, unpaced).

## Results — Linux (Ubuntu 24.04, aarch64) in Docker Desktop VM, **pinned** (producer core 2, consumer core 4)

Full tables: [results/REPORT_linux-docker.md](results/REPORT_linux-docker.md). Still a VM (vCPUs are host threads, 41.67 ns clock tick), so indicative.

| variant | p50 | p99 | p99.9 | Mmsg/s unpaced |
|---|---|---|---|---|
| spsc-padded | 83 ns | 1.9 µs | 13.7 µs | 18.6 |
| spsc-unpadded | 125 ns | 1.9 µs | 15.9 µs | 20.3 |
| mutex + polling | 583 ns | 10.8 µs | 53.5 µs | 6.4 |
| mutex + condvar | 3.2 µs | 12.8 µs | 48 µs | 6.5 |

SPSC vs mutex: p50 7–38× lower, p99 ~6× lower, ~3× the throughput. Book: flat levels 43 ns/msg vs 85 ns for `std::map` (~2×).

## Pool sizing matters (cache footprint) — single thread, macOS, 5 reps

The pooled order store's hash table is sized 2× `--max-orders`. At the default 4 M that is a 128 MB table for ~200 k live
orders: every probe is a cache miss, so pool ≈ heap. Sized to the workload (`--max-orders 524288`, 16 MB table):

| book | ns/msg @ 4 M | ns/msg @ 512 k |
|---|---|---|
| map-heap | 75.1 | 72.7 |
| map-pool | 72.2 | **56.8** |
| flat-heap | 44.5 | 43.3 |
| flat-pool | 44.6 | **28.5** (35 Mmsg/s) |

Heap → pool + flat levels: 72.7 → 28.5 ns/msg (**2.5×**) when the pool is right-sized. Run `scripts/perf_stat.sh` on bare-metal Linux to
confirm via cache-miss counters.

## Caveats — read before quoting any number

- **macOS has no thread pinning and no `perf`; Docker VM has no usable PMU.** Threads here are only hinted via QoS; the scheduler may migrate them
  between P and E cores, which is where the ~9 µs p99.9 comes from. Re-run on Linux with pinned/isolated cores before
  putting tail-latency numbers on a resume.
- **False sharing is *not* visible in these runs**, on macOS (unpinned) or in the pinned Linux VM (padded p50 is lower,
  83 vs 125 ns, but p99 is equal and unpadded has higher throughput). Cached index copies already batch the
  cross-core traffic. Don't claim a false-sharing speed-up unless a bare-metal x86 run shows one; "measured it, effect
  small with cached indices on AArch64" is the honest finding. Note Apple Silicon lines are **128 B**; `kCacheLine` handles that
  (64 B elsewhere).
- **Pool vs heap only wins when right-sized** (see the sizing table); at the default 4 M capacity it does not.
- Data is **synthetic** (deterministic generator, realistic message mix incl. sub-penny and far-from-touch prices) unless
  you run on a real NASDAQ file. Say "ITCH 5.0 wire format, synthetic + NASDAQ sample replay" accordingly.
- Unpaced pipeline latency is queue sojourn time (queue full) and is meaningless; use the paced run for latency and
  the unpaced run for throughput only.
- Clock: `cntvct_el0` (arm64, 1 ns tick here, ~17 ns read cost) / `rdtsc` (x86, calibrated). Per-message latencies
  below ~50 ns are dominated by timer overhead.

## Design notes

- SPSC: free-running indices, power-of-two capacity, producer owns `tail`+`cached_head`, consumer owns `head`+`cached_tail`;
  each pair on its own cache line (`SpscIndices<true>`), or packed together for the ablation (`<false>`). Release store
  publishes the slot; acquire load observes it. Tested for FIFO order over 3 M items, full/empty edges.
- Pacing uses the *intended* send time as the latency origin, so producer stalls inflate latency instead of vanishing.
- Pool store: preallocated `Order[]`, free list, power-of-two table, linear probing, backward-shift delete (no tombstones);
  fuzz-tested against `unordered_map` with heavily colliding keys.
- Flat levels: 4096-tick window (1 cent ticks) around first price; off-grid/out-of-window prices spill to `std::map`,
  so behaviour is identical to the map baseline (hand-built test covers sub-penny and far-ask cases).
- Not done (and not claimed): kernel bypass (DPDK/RDMA), FPGA, OUCH/FIX, NSE/BSE protocols, kill-switch/circuit breakers.
