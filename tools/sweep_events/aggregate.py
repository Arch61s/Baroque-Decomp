#!/usr/bin/env python3
"""Aggregate tools/sweep_events/results.jsonl (+ talk_results.jsonl): per-event table and deduplicated stderr lines
(numbers normalised) with counts and the events they occur in.

usage: python tools/sweep_events/aggregate.py [--md]
"""
import json, re, sys
from collections import defaultdict
from pathlib import Path

HERE = Path(__file__).resolve().parent
NORMAL = re.compile(r"^(game: floor \d+ characters|event: start|event: end|event: actor set|controls: wrote)")


def norm(line):
    return re.sub(r"-?\d+(\.\d+)?", "#", line.strip())


def load(name):
    p = HERE / name
    if not p.exists():
        return []
    out = {}
    for line in p.read_text(encoding="utf-8", errors="replace").splitlines():
        try:
            r = json.loads(line)
        except Exception:
            continue
        out[r.get("event", r.get("speaker"))] = r
    return list(out.values())


def main():
    md = "--md" in sys.argv
    evs = sorted(load("results.jsonl"), key=lambda r: r["event"])
    talks = load("talk_results.jsonl")
    issues = defaultdict(lambda: {"count": 0, "who": [], "example": ""})
    print("| event | floor | map | end | status | secs | ev ticks (first..last, max frame) | ended | frames (tick: luma/black) |")
    print("|---|---|---|---|---|---|---|---|---|")
    for r in evs:
        el = r["evlog"]
        fr = " ".join(f"{t}:{s['mean_luma']:.0f}/{s['black_frac']:.2f}" if s else f"{t}:MISSING"
                      for t, s in r["shots"].items())
        print(f"| E{r['event']:02d} | {r['floor']} | {r['map']} | {r['end']}{' (trunc)' if r['truncated'] else ''} | "
              f"{r['status']} | {r['secs']} | {el['first_tick']}..{el['last_tick']} max {el['max_frame']} | {el['ended']} | {fr} |")
        for line in r["stderr"].splitlines():
            if not line.strip() or NORMAL.match(line):
                continue
            k = norm(line); it = issues[k]; it["count"] += 1
            tag = f"E{r['event']:02d}"
            if tag not in it["who"]:
                it["who"].append(tag)
            it["example"] = it["example"] or line.strip()
    for r in talks:
        for line in r["stderr"].splitlines():
            if not line.strip() or NORMAL.match(line):
                continue
            k = norm(line); it = issues[k]; it["count"] += 1
            if r["speaker"] not in it["who"]:
                it["who"].append(r["speaker"])
            it["example"] = it["example"] or line.strip()
    print()
    print("| count | runs | stderr line (example) |")
    print("|---|---|---|")
    for k, it in sorted(issues.items(), key=lambda kv: (-len(kv[1]["who"]), -kv[1]["count"])):
        who = ",".join(it["who"][:12]) + (f" (+{len(it['who']) - 12})" if len(it["who"]) > 12 else "")
        print(f"| {it['count']} | {len(it['who'])}: {who} | `{it['example'][:200]}` |")


if __name__ == "__main__":
    main()
