#!/usr/bin/env python3
"""Static cross-check: entity tags each floor's data can place (FDT entity table + RDT spawns of every listed room)
versus tags the sweep saw constructed ('game: floor N characters:' lines). Prints tags that the data places but the
port never constructed on any swept floor (candidate silent drops / missing classes).

usage: python tools/sweep/data_tags.py [results.jsonl ...]
"""
import json, re, sys
from collections import defaultdict
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
from map_formats import parse_fdt, parse_rdt  # noqa: E402

A = ROOT / "work" / "assets"


def find(rel, floor):
    p = A / "PACKS" / f"F{floor:03d}" / rel
    if p.exists(): return p
    p = A / rel
    return p if p.exists() else None


def floor_tags(floor):
    fp = find(f"DATA/MAP/FLOOR/F{floor:03d}.FDT", floor)
    if fp is None: return None
    fdt, _ = parse_fdt(fp.read_bytes())
    pre = "MO" if floor in (0x5b, 0x5c) else ("ME" if 0x5d <= floor <= 0x60 else "MR")
    tags = {}
    for e in fdt["entities"]: tags.setdefault(e["tag"], set()).add("fdt")
    for rid in fdt["room_ids"]:
        rp = find(f"DATA/MAP/ROOM/{pre}{fdt['map_set']:02d}_{rid:02d}.RDT", floor)
        if rp is None:
            tags.setdefault(f"<missing RDT {pre}{fdt['map_set']:02d}_{rid:02d}>", set()).add("-"); continue
        rdt, _ = parse_rdt(rp.read_bytes(), want_grid=False)
        for s in rdt["spawns"]:
            tags.setdefault(s["tag"], set()).add("cond" if s["cond_flag"] not in (0, -1) else "always")
    return tags


def main():
    paths = sys.argv[1:] or [str(ROOT / "tools/sweep/results/sweep.jsonl")]
    built = defaultdict(set); floors = set()
    for p in paths:
        for line in Path(p).read_text(encoding="utf-8").splitlines():
            if not line.strip(): continue
            r = json.loads(line); floors.add(r["floor"])
            for ln in (r.get("stderr") or "").splitlines():
                m = re.match(r"game: floor (\d+) characters:\d+(.*)", ln)
                if m:
                    for t in m.group(2).split(): built[t].add(r["floor"])
    data = defaultdict(set); kinds = defaultdict(set)
    for f in sorted(floors):
        ts = floor_tags(f)
        for t, k in (ts or {}).items():
            data[t].add(f); kinds[t] |= k
    # PLAY / O99Z / PT stage objects / map-model J024-J026 are not characters; items are logged as characters.
    skip = re.compile(r"^(PLAY|O99Z|PT\d\d|J02[456])$")
    print("| tag | floors whose data places it | spawn kinds (always / cond flag / fdt table) |")
    print("|---|---|---|")
    rows = []
    for t, fs in data.items():
        if skip.match(t): continue
        b = built.get(t, set())
        rows.append((len(b) > 0, -len(fs), t, fs, b))
    for has, _, t, fs, b in sorted(rows):
        if has: continue
        print(f"| {t} | {len(fs)}: {','.join(map(str, sorted(fs)[:40]))} | {'/'.join(sorted(kinds[t]))} |")
    # base-tag view: the port may substitute the 4th letter (E18B -> E18A model); compare the full tag only.


if __name__ == "__main__":
    main()
