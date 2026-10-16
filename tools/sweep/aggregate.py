#!/usr/bin/env python3
"""Aggregate tools/sweep/results/sweep.jsonl into markdown tables (printed to stdout).

Message keys: stderr lines with decimal numbers / floats replaced by '#', hex FUN_/0x addresses and tags kept.
The per-floor "game: floor N characters:K ..." line is parsed separately (character/object tags per floor).
"""
import json, re, sys
from collections import defaultdict, Counter
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
NUM = re.compile(r"(?<![A-Za-z_0-9.])-?\d+(?:\.\d+)?(?![A-Za-z_0-9])")


def group_of(f):
    if f < 0x5a: return "low 0-89 (FUN_0011c090)"
    if f == 90: return "90"
    if f in (91, 92): return "town 91-92"
    if 93 <= f <= 96: return "event 93-96"
    if f in (98, 99): return "special 98/99"
    if 100 <= f <= 140: return "tower 100-140"
    if 200 <= f < 300: return "2xx 206-232"
    if 300 < f < 330: return "scenario 301-327"
    if 400 <= f <= 499: return "second dungeon 400-499"
    return "other"


def ranges(fs):
    fs = sorted(set(fs)); out = []; i = 0
    while i < len(fs):
        j = i
        while j + 1 < len(fs) and fs[j + 1] == fs[j] + 1: j += 1
        out.append(str(fs[i]) if i == j else f"{fs[i]}-{fs[j]}"); i = j + 1
    return ",".join(out)


def load(paths):
    recs = []
    for p in paths:
        for line in Path(p).read_text(encoding="utf-8", errors="replace").splitlines():
            if line.strip():
                recs.append(json.loads(line))
    return recs


def classify_frame(st):
    if not st or "mean_luma" not in st: return "no image"
    if st["mean_luma"] < 4 or st["black_frac"] > 0.97: return "BLACK"
    if st["black_frac"] > 0.6: return "mostly black"
    if st["dominant_frac"] > 0.7: return "flat (one colour)"
    return "ok"


def main():
    paths = sys.argv[1:] or [str(ROOT / "tools/sweep/results/sweep.jsonl")]
    recs = load(paths)
    msgs = defaultdict(lambda: {"count": 0, "floors": set(), "example": None, "cmd": None})
    chars = defaultdict(Counter)
    groups = defaultdict(lambda: {"floors": set(), "runs": 0, "ok": 0, "crash": [], "timeout": [], "no_png": [],
                                  "black": [], "warn_runs": 0})
    for r in recs:
        f = r["floor"]; g = groups[group_of(f)]
        g["floors"].add(f); g["runs"] += 1
        fr = classify_frame(r.get("png_stats"))
        r["frame"] = fr
        if r["status"] == "ok" and fr == "ok": g["ok"] += 1
        if r["status"] in ("crash", "timeout", "no_png"): g[r["status"]].append(r)
        if fr in ("BLACK", "mostly black", "flat (one colour)") and r["status"] == "ok": g["black"].append(r)
        lines = (r.get("stderr") or "").splitlines() + (r.get("stdout") or "").splitlines()
        warned = False
        for ln in lines:
            ln = ln.rstrip()
            if not ln: continue
            if ln.startswith("ai t=") or ln.startswith("ai-fell"):
                continue   # BAROQUE_DEBUG_AI trace, summarised in the walk table
            m = re.match(r"game: floor (\d+) characters:(\d+)(.*)", ln)
            if m:
                for t in m.group(3).split(): chars[t][f] += 1
                continue
            key = NUM.sub("#", ln)[:200]
            e = msgs[key]; e["count"] += 1; e["floors"].add(f)
            if e["example"] is None: e["example"] = ln[:300]; e["cmd"] = r["cmd"]
            warned = True
        if warned: g["warn_runs"] += 1

    print("## Per floor-range status\n")
    print("| range | floors run | runs | clean (exit 0, image ok) | crash | timeout | no png | dark/flat frame | runs with stderr |")
    print("|---|---|---|---|---|---|---|---|---|")
    for name in sorted(groups, key=lambda n: min(groups[n]["floors"])):
        g = groups[name]
        print(f"| {name} | {len(g['floors'])} ({ranges(g['floors'])}) | {g['runs']} | {g['ok']} | {len(g['crash'])} | "
              f"{len(g['timeout'])} | {len(g['no_png'])} | {len(g['black'])} | {g['warn_runs']} |")

    print("\n## Crashes / timeouts / missing captures\n")
    bad = [r for r in recs if r["status"] != "ok"]
    if not bad: print("none")
    for r in sorted(bad, key=lambda r: (r["floor"], r["seed"])):
        tail = " / ".join([l for l in (r.get("stderr") or "").splitlines() if l.strip()][-3:])[:300]
        rc = r["rc"]; rch = f"0x{rc & 0xffffffff:08X}" if isinstance(rc, int) else rc
        print(f"- floor {r['floor']} seed {r['seed']}: **{r['status']}** rc={rch} ({r['secs']} s) last stderr: `{tail}`  ")
        print(f"  `{r['cmd']}`")

    print("\n## Dark / flat frames\n")
    dark = [r for r in recs if r["frame"] not in ("ok",) and r["status"] == "ok"]
    if not dark: print("none")
    for r in sorted(dark, key=lambda r: (r["floor"], r["seed"])):
        st = r.get("png_stats") or {}
        print(f"- floor {r['floor']} seed {r['seed']}: {r['frame']} (mean luma {st.get('mean_luma')}, black "
              f"{st.get('black_frac')}, dominant {st.get('dominant_rgb')} x{st.get('dominant_frac')})")

    print("\n## Deduplicated stderr messages (count = lines over all runs)\n")
    print("| # | count | floors | message (numbers -> #) | example |")
    print("|---|---|---|---|---|")
    for i, (k, e) in enumerate(sorted(msgs.items(), key=lambda kv: (-len(kv[1]['floors']), -kv[1]['count']))):
        print(f"| {i + 1} | {e['count']} | {len(e['floors'])}: {ranges(e['floors'])} | `{k}` | `{e['example']}` |")

    walk = [r for r in recs if r.get("tag") == "walk"]
    if walk:
        print("\n## Walk pass (stick + attack script, BAROQUE_DEBUG_AI player trace every 60 ticks)\n")
        died = []; fell = []; stuck = []; efell = defaultdict(set); hurt = 0
        pat = re.compile(r"ai t=(\d+) PLAYER hp=(-?\d+)/(\d+) act=(\S+) state=(\d+) pos=\((-?\d+),(-?\d+),(-?\d+)\)")
        for r in walk:
            pl = pat.findall(r.get("stderr") or "")
            if not pl: continue
            hps = [int(p[1]) for p in pl]; ys = [int(p[6]) for p in pl]
            xz = {(p[5], p[7]) for p in pl[1:]}
            if min(hps) <= 0: died.append(r["floor"])
            elif min(hps) < hps[0]: hurt += 1
            if min(ys) < -1000: fell.append((r["floor"], min(ys)))
            if len(xz) <= 1: stuck.append(r["floor"])
            for ln in (r.get("stderr") or "").splitlines():
                if ln.startswith("ai-fell"):
                    m = re.search(r"ai t=\d+ (\S+) pos=\((-?\d+),(-?\d+)", ln)
                    if m: efell[m.group(1)].add(r["floor"])
        print(f"- runs: {len(walk)}; player died: {len(died)} ({ranges(died)}); hurt but alive: {hurt}")
        print(f"- player fell below y -1000: {len(fell)} {fell[:30]}")
        print(f"- player never moved (stick ignored / blocked): {len(stuck)} ({ranges(stuck)})")
        print(f"- characters below y -1000 (fell out of the map): {sum(len(v) for v in efell.values())} tag/floor pairs")
        for t in sorted(efell): print(f"  - {t}: {ranges(efell[t])}")

    print("\n## Character / object tags created per floor (from 'game: floor N characters:')\n")
    print("| tag | floors |")
    print("|---|---|")
    for t in sorted(chars):
        print(f"| {t} | {len(chars[t])}: {ranges(chars[t])} |")


if __name__ == "__main__":
    main()
