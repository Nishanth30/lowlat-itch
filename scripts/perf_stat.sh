#!/usr/bin/env bash
# Stage 3 on Linux: hardware counters per book variant (single-thread mode).
# Needs: perf, and /proc/sys/kernel/perf_event_paranoid <= 1 (or run as root).
set -euo pipefail
cd "$(dirname "$0")/.."
FILE="${ITCH_FILE:-data/synthetic.itch}"
CORE="${CORE:-2}"
EV=cycles,instructions,branches,branch-misses,L1-dcache-loads,L1-dcache-load-misses,LLC-loads,LLC-load-misses
for b in map-heap map-pool flat-heap flat-pool; do
  echo "=== $b ==="
  perf stat -e "$EV" -x, -o "results/perf_$b.csv" \
    taskset -c "$CORE" ./build/itch_pipeline --file "$FILE" --mode single --book "$b" --reps 1 >/dev/null
  column -s, -t "results/perf_$b.csv" | grep -v '^#'
done
