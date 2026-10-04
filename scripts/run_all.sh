#!/usr/bin/env bash
# Build, test, generate data, run every benchmark, write results/*.json + REPORT.md.
# Usage: scripts/run_all.sh [tag]     (tag defaults to "$(uname -s | tr A-Z a-z)")
# Linux: set PROD_CORE/CONS_CORE to two *physical* cores (see `lscpu -e`), ideally isolated (isolcpus=).
set -euo pipefail
cd "$(dirname "$0")/.."
TAG="${1:-$(uname -s | tr 'A-Z' 'a-z')}"
MSGS="${MSGS:-3000000}"
ARGS=()
[ -n "${PROD_CORE:-}" ] && ARGS+=(--prod-core "$PROD_CORE")
[ -n "${CONS_CORE:-}" ] && ARGS+=(--cons-core "$CONS_CORE")

cmake -S . -B build -DCMAKE_BUILD_TYPE=Release >/dev/null
cmake --build build -j >/dev/null
./build/run_tests
mkdir -p data results
[ -f data/synthetic.itch ] || ./build/gen_itch --msgs "$MSGS" --out data/synthetic.itch

./build/bench_queue "${ARGS[@]}" --json "results/queue_$TAG.json"
FILE="${ITCH_FILE:-data/synthetic.itch}"
./build/itch_pipeline --file "$FILE" "${ARGS[@]}" --json "results/pipeline_$TAG.json"
./build/itch_pipeline --file "$FILE" "${ARGS[@]}" --mode pipeline --rate 0 --reps 3 --json "results/pipeline_unpaced_$TAG.json"
python3 scripts/report.py "$TAG" > "results/REPORT_$TAG.md"
echo "wrote results/REPORT_$TAG.md"
