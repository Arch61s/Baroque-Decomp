#!/usr/bin/env python3
"""Event length estimate for the event sweep: frame of the cue block that holds op 200 (end event), max cue frame,
map id, cue / voice / text counts. Read-only; parses DATA/EVENT_EN/E??.EVT like tools/game_formats.parse_evt.

usage: python tools/sweep_events/evt_len.py [--json]
"""
import glob, json, os, struct, sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
EVT_OPS = {1: 1, 5: 0, 6: 1, 7: 3, 8: 3, 9: 1, 10: 1, 11: 1, 12: 1, 13: 1, 14: 1, 15: 2, 16: 4}


def evt_info(path):
    d = open(path, 'rb').read()
    map_id = struct.unpack_from('<i', d, 0x800)[0]
    w = list(struct.unpack_from('<%di' % ((len(d) - 0x808) // 4), d, 0x808))
    pc, cur, cues = 0, 0, []
    while pc < len(w):
        op = w[pc]
        if op == 201:
            break
        if op == 20:
            cues.append((cur, w[pc + 1:pc + 65]))
            pc += 65
            continue
        n = EVT_OPS.get(op)
        if n is None:
            break
        if op == 1:
            cur = w[pc + 1]
        pc += 1 + n
    end = None
    voices = 0
    for fr, blk in cues:
        if 200 in blk and end is None:
            end = fr
        voices += blk.count(132) + blk.count(133)
    frames = [fr for fr, _ in cues]
    real = [f for f in frames if f < 99999]
    return dict(map=map_id, cues=len(cues), end=end, maxFrame=max(frames) if frames else 0,
                realMax=max(real) if real else 0, voiceOps=voices)


def main():
    out = {}
    for p in sorted(glob.glob(str(ROOT / 'work/assets/DATA/EVENT_EN/E*.EVT'))):
        out[os.path.basename(p)[1:3]] = evt_info(p)
    if '--json' in sys.argv:
        print(json.dumps(out))
    else:
        for k, v in out.items():
            print('E' + k, v)


if __name__ == '__main__':
    main()
