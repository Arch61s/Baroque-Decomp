#!/usr/bin/env python3
"""Voice clip lengths (SFS entry id + 0xba, VAGp header size / rate) against the talk durations of batch.jsonl and the
last trigger frame of each talk. usage: python tools/sweep_talks/voice_len.py"""
import json, struct, sys
from pathlib import Path
HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent))
import sound_archive as sa

def sfs_table(f):
    """5-sector u32 table as IOPSND indexes it (native/audio/sfs.hpp): entry i starts at word[i]"""
    f.seek(0)
    w = struct.unpack('<%dI' % (5 * 512), f.read(5 * 2048))
    t = [w[0]]
    for x in w[1:]:
        if x == 0: break
        t.append(x)
    return t

def clip(i, f, table):
    f.seek(table[i] * 2048)
    h = f.read(48)
    if h[:4] != b'VAGp': return None
    size, rate = struct.unpack('>II', h[12:20])
    return size // 16 * 28 / rate

def main():
    cat = {c["file"]: c for c in json.loads((HERE / "catalog.json").read_text(encoding="utf-8"))["talks"]}
    recs = {}
    for ln in (HERE / "batch.jsonl").read_text(encoding="utf-8").splitlines():
        r = json.loads(ln); recs[r["file"]] = r
    rows = []
    with open(sa.SFS, 'rb') as f:
        table = sfs_table(f)
        for n, c in cat.items():
            if c["voice"] in (None, -1): continue
            secs = clip(c["voice"] + 0xba, f, table)
            r = recs.get(n)
            frames = [x["frame"] for v in c["triggers"].values() for x in v if x["frame"] >= 0]
            rows.append((n, c["voice"], secs, r and r.get("ticks"), r and r.get("result"), max(frames) if frames else 0))
    out = []
    for n, v, secs, ticks, res, last in rows:
        if secs is None: out.append(f"{n} voice {v}: not VAGp"); continue
        exp = secs * 30
        if res == "ended" and ticks is not None and abs(ticks - exp) > 4:
            out.append(f"{n} voice {v}: clip {secs:.2f}s = {exp:.0f} ticks, talk ran {ticks}")
        if last > exp + 1:
            out.append(f"{n} voice {v}: last trigger frame {last} > clip {exp:.0f} ticks (authored past the voice)")
    print("\n".join(out)); print(f"{len(rows)} voiced talks, {len(out)} notes")

if __name__ == "__main__":
    main()
