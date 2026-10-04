#!/usr/bin/env python3
"""Render results/*_<tag>.json into markdown tables."""
import json, os, sys

tag = sys.argv[1] if len(sys.argv) > 1 else "macos"
root = os.path.join(os.path.dirname(__file__), "..", "results")


def load(name):
    p = os.path.join(root, f"{name}_{tag}.json")
    return json.load(open(p)) if os.path.exists(p) else None


def table(head, rows):
    out = ["| " + " | ".join(head) + " |", "|" + "|".join("---" for _ in head) + "|"]
    out += ["| " + " | ".join(str(c) for c in r) + " |" for r in rows]
    return "\n".join(out)


q = load("queue")
if q:
    m = q["meta"]
    print(f"### Queue handoff latency ({tag})\n")
    print(f"Paced {m['rate']:,} msg/s, {m['msgs']:,} msgs, median of {m['reps']} reps; "
          f"cache line {m['cache_line']} B; clock tick {m['tick_ns']} ns; "
          f"producer: {m['producer']}; consumer: {m['consumer']}\n")
    print(table(["variant", "p50 ns", "p99 ns", "p99.9 ns", "p99.99 ns", "max ns", "Mmsg/s (unpaced)"],
                [[r["variant"], f"{r['p50']:.0f}", f"{r['p99']:.0f}", f"{r['p999']:.0f}",
                  f"{r['p9999']:.0f}", f"{r['max']:.0f}", f"{r['mops']:.2f}"] for r in q["results"]]))
    print()

for name, title in (("pipeline", "ITCH replay, paced"), ("pipeline_unpaced", "ITCH replay, unpaced")):
    p = load(name)
    if not p:
        continue
    m = p["meta"]
    print(f"### {title} ({tag})\n")
    print(f"{m['events']:,} book events / {m['frames']:,} frames; rate {m['rate']:,} msg/s; "
          f"median of {m['reps']} reps; {m['threads']}\n")
    print(table(["book", "mode", "ns/msg", "p50", "p99", "p99.9", "p99.99", "max", "Mmsg/s", "checksum"],
                [[r["book"], r["mode"], r["ns_per_msg"], r["p50"], r["p99"], r["p999"], r["p9999"],
                  r["max"], r["mmsg_s"], r["checksum"]] for r in p["results"]]))
    print()
