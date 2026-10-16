#!/usr/bin/env python3
"""Talk catalog for the conversation sweep: every DATA/TALK_EN/*.TLK of the talk table 0x457020 with the page state
FUN_001a77c0 builds (voice, layout, number, first sentence), the FUN_001a80c0 triggers grouped by type, and the floors
whose map data places the speaker (FDT entity table + RDT spawns, with the spawn condition flag).

usage: python tools/sweep_talks/catalog.py   -> tools/sweep_talks/catalog.json + summary on stdout
"""
import json, re, sys
from collections import Counter, defaultdict
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
from game_formats import parse_tlk  # noqa: E402
from map_formats import parse_fdt, parse_rdt  # noqa: E402

A = ROOT / "work" / "assets"
# kind 0x65..0x73 -> manager trigger type 1..15 (FUN_001a80c0)
TYPE_NAME = {1: "effect", 2: "sentence", 3: "clear", 4: "bodyAnim", 5: "act", 6: "headAnim", 7: "hairAnim", 8: "flagSet",
             9: "flagClr", 10: "move", 11: "layout", 12: "schedule", 13: "counter", 14: "exclude", 15: "noSkip"}


def talk_table():
    rows = []
    for m in re.finditer(r'\{"([^"]+)", "([^"]*)", "([^"]*)"\}', (ROOT / "native/data/tables/talk_table.inc").read_text()):
        rows.append(dict(file=m.group(1), voiceNo=m.group(2), speaker=m.group(3)))
    return rows


def find(rel, floor):
    p = A / "PACKS" / f"F{floor:03d}" / rel
    if p.exists(): return p
    p = A / rel
    return p if p.exists() else None


def npc_floors():
    out = defaultdict(list)
    for floor in range(0, 512):
        fp = find(f"DATA/MAP/FLOOR/F{floor:03d}.FDT", floor)
        if fp is None: continue
        try:
            fdt, _ = parse_fdt(fp.read_bytes())
        except Exception:
            continue
        pre = "MO" if floor in (0x5b, 0x5c) else ("ME" if 0x5d <= floor <= 0x60 else "MR")
        for e in fdt["entities"]:
            if e["tag"].startswith("C"): out[e["tag"]].append(dict(floor=floor, src="fdt"))
        for rid in fdt["room_ids"]:
            rp = find(f"DATA/MAP/ROOM/{pre}{fdt['map_set']:02d}_{rid:02d}.RDT", floor)
            if rp is None: continue
            rdt, _ = parse_rdt(rp.read_bytes(), want_grid=False)
            for s in rdt["spawns"]:
                if s["tag"].startswith("C"):
                    out[s["tag"]].append(dict(floor=floor, room=f"{pre}{fdt['map_set']:02d}_{rid:02d}", flag=s["cond_flag"]))
    return out


def main():
    rows = talk_table()
    cat = []
    for i, r in enumerate(rows):
        p = A / "DATA/TALK_EN" / f"{r['file']}.TLK"
        t = parse_tlk(p)
        page = t["page"]
        sentences = [s for s in t["text"].split(";")]
        trig = defaultdict(list)
        for tr in t["triggers"]:
            ty = tr["kind"] - 0x64
            trig[TYPE_NAME.get(ty, str(ty))].append(dict(frame=tr["frame"], args=tr["args"], name=tr.get("name")))
        layouts = [page.get("8", 0)] + [x["args"][0] for x in trig.get("layout", [])]
        shown = [page.get("4", 0)] + [x["args"][0] for x in trig.get("sentence", [])]
        maxlen = max((len(s) for s in sentences), default=0)
        cat.append(dict(idx=i, file=r["file"], voiceNo=r["voiceNo"], speaker=r["speaker"], voice=page.get("3"),
                        speed=page.get("7", 1), layouts=sorted(set(layouts)), number=page.get("9"),
                        sentences=len([s for s in sentences if s]), shown=shown, maxlen=maxlen,
                        newlines=max((s.count("\n") for s in sentences), default=0),
                        trig={k: len(v) for k, v in trig.items()}, triggers=trig, text=t["text"]))
    floors = npc_floors()
    out = dict(talks=cat, npcFloors=floors)
    (Path(__file__).parent / "catalog.json").write_text(json.dumps(out, indent=1, ensure_ascii=False), encoding="utf-8")
    spk = Counter(c["speaker"] for c in cat)
    print(f"{len(cat)} talks, {len(spk)} speakers")
    for s, n in sorted(spk.items()):
        sub = [c for c in cat if c["speaker"] == s]
        lay = Counter(l for c in sub for l in c["layouts"])
        nv = sum(1 for c in sub if c["voice"] == -1)
        multi = sum(1 for c in sub if len(c["shown"]) > 1)
        kinds = Counter(k for c in sub for k in c["trig"])
        fl = sorted({(x["floor"]) for x in floors.get(s, [])})
        print(f"{s}: {n} talks, layouts {dict(lay)}, noVoice {nv}, multi-sentence {multi}, number {sum(1 for c in sub if c['number'] not in (None, -1))}, "
              f"floors {fl[:12]}{'...' if len(fl) > 12 else ''}, trig {dict(kinds)}")


if __name__ == "__main__":
    main()
