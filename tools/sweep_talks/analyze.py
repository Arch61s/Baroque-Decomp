#!/usr/bin/env python3
"""Checks over tools/sweep_talks/batch.jsonl (run_batch.py) against the catalog:
 end      talk did not end by itself (TIMEOUT / REPLACED)
 voice    voiced talk whose voice never reported playing (status 4), or ended before its last sentence trigger
 shown    sentence triggers (catalog) that never put text into the window
 fit      text wrap audit: lines beyond the rect height, last line below the screen (448) / right of 512
 name     voiced talk without a name plate
usage: python tools/sweep_talks/analyze.py [batch.jsonl]
"""
import json, sys
from collections import Counter, defaultdict
from pathlib import Path

HERE = Path(__file__).resolve().parent


def main():
    path = Path(sys.argv[1]) if len(sys.argv) > 1 else HERE / "batch.jsonl"
    cat = {c["file"]: c for c in json.loads((HERE / "catalog.json").read_text(encoding="utf-8"))["talks"]}
    recs = {}
    for ln in path.read_text(encoding="utf-8").splitlines():
        if ln.strip():
            r = json.loads(ln); recs[r["file"]] = r
    issues = defaultdict(list)
    dur = []
    for name, c in cat.items():
        r = recs.get(name)
        if r is None:
            issues["missing"].append(name); continue
        if r.get("result") != "ended":
            issues["end"].append(f"{name} {r.get('result')} ticks={r.get('ticks')}")
        voiced = c["voice"] not in (None, -1)
        if voiced:
            if not any(v[1] == 4 for v in r["voice"]) and not any(s["vs"] == 4 for s in r["states"]):
                issues["voice"].append(f"{name} voice {c['voice']} never playing")
            last = max([s["frame"] for s in c["triggers"].get("sentence", [])] + [0])
            if r.get("ticks", 0) < last:
                issues["voice"].append(f"{name} ended at {r.get('ticks')} before sentence frame {last}")
            dur.append(r.get("ticks", 0))
            sets = [t for t in r["texts"] if not t.get("cleared")]
            want = 1 + len(c["triggers"].get("sentence", []))
            if len(sets) < want:
                issues["shown"].append(f"{name} {len(sets)}/{want} sentence sets")
            if not any(n["idx"] >= 0 and n["name"] for n in r["names"]) and c["speaker"] not in ("", "TUTO"):
                issues["name"].append(f"{name} spk={c['speaker']}")
            for t in sets:
                x, y, w, h = t["rect"]
                step = t["scale"].split()
                lh = float(step[1]) * 9 * 2 * 0.85
                maxl = int(h // lh) if t["style"] == 0 else int(w // lh)
                bad = []
                if t["style"] == 0 and t["lines"] > max(maxl, 1): bad.append(f"lines {t['lines']}>{maxl}")
                if t["style"] == 0 and t["reach"] > 448: bad.append(f"bottom {t['reach']}")
                if t["style"] == 0 and x + t["widest"] > 512: bad.append(f"right {x + t['widest']:.0f}")
                if t["style"] == 0 and t["widest"] > w + 24: bad.append(f"widest {t['widest']}>{w:.0f}")
                if bad:
                    issues["fit"].append(f"{name} rect={t['rect']} {' '.join(bad)} | {t['text'][:70]}")
    for k, v in issues.items():
        print(f"== {k}: {len(v)}")
        for s in v[:60]:
            print("  ", s)
    print(f"talks {len(recs)}/{len(cat)}, voiced durations min {min(dur) if dur else 0} max {max(dur) if dur else 0}")


if __name__ == "__main__":
    main()
