#!/usr/bin/env bash
# Download a real NASDAQ ITCH 5.0 sample day (multi-GB .gz!) and gunzip it.
# Pick a file from https://emi.nasdaq.com/ITCH/Nasdaq%20ITCH/ and pass its name.
# Example: scripts/fetch_nasdaq_sample.sh 01302020.NASDAQ_ITCH50.gz
set -euo pipefail
cd "$(dirname "$0")/.."
NAME="${1:?usage: $0 <file.NASDAQ_ITCH50.gz>}"
mkdir -p data
curl -L --fail -o "data/$NAME" "https://emi.nasdaq.com/ITCH/Nasdaq%20ITCH/$NAME"
gunzip -f "data/$NAME"
echo "now: ITCH_FILE=data/${NAME%.gz} scripts/run_all.sh real   (consider --max-orders 8388608)"
