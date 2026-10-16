"""Disassemble DATA/AI/AISEQ.BIN (Baroque PS2) byte-code as interpreted by FUN_001f07b0.

Usage: python -I tools/aiseq.py [ID ...] [--stats]
Layout (verified against FUN_001ed200/FUN_001f0630/FUN_001f07b0):
  header u32[3] = offsets {0xc, 0x10, 0x14}; @0xc u32 maxOpsPerTick (0x400); @0x10 u32 (0x2d5);
  @0x14 index {char id[4]; u32 recOfs} ... terminated by "    ". Record = u32 stateTable[] of
  file offsets (0 = none): state 0 = main (run every tick), state 1 = init (run once at spawn);
  the interpreter resolves jump targets as base(recOfs) + (target - recOfs).
"""
import os
import struct
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PATH = os.path.join(ROOT, 'work', 'assets', 'DATA', 'AI', 'AISEQ.BIN')

SYSVARS = {
    0xffffff: 'v0', 0x1000000: 'v1', 0x1000001: 'v2', 0x1000002: 'v3', 0x1000003: 'v4',
    0x1000004: 'v5', 0x1000005: 'v6', 0x1000006: 'v7',
    0x1000007: 'FLAG', 0x1000008: 'MODE', 0x1000009: 'SEED', 0x100000a: 'HP_PCT',
    0x100000b: 'MY_ROOM_TYPE', 0x100000c: 'DIST3D', 0x100000d: 'DIST_XZ', 0x100000e: 'DIST_Y',
    0x100000f: 'PLAYER_ABOVE', 0x1000010: 'ANGLE_TO_PLAYER', 0x1000011: 'PLAYER_ROOM_TYPE',
    0x1000012: 'CAN_SEE_PLAYER', 0x1000013: 'PLAYER_ATTACKING', 0x1000014: 'FRAME',
    0x1000015: 'HAS_ITEM', 0x1000016: 'TARGET_MODE', 0x100001b: 'N_WANDERING',
    0x100001c: 'N_ENEMIES', 0x100001d: 'ST_1', 0x100001e: 'ST_2', 0x100001f: 'ST_4',
    0x1000020: 'ST_8', 0x1000021: 'ST_10', 0x1000022: 'ST_20', 0x1000023: 'ST_10000',
    0x1000024: 'ST_40000', 0x1000025: 'ST_2000', 0x1000026: 'PL_ST_1', 0x1000027: 'PL_ST_2',
    0x1000028: 'PL_ST_4', 0x1000029: 'PL_ST_8', 0x100002a: 'PL_ST_10', 0x100002b: 'PL_ST_20',
    0x100002c: 'PL_ST_10000', 0x100002d: 'PL_ST_40000', 0x100002e: 'PL_ST_2000',
}
CMP = ['==', '!=', '>', '>=', '<', '<=', '&']


def var(v):
    return SYSVARS.get(v, '?%x' % v) if v >= 0xffffff else str(v)


def u32(d, o):
    return struct.unpack_from('<I', d, o)[0]


def f32(d, o):
    return struct.unpack_from('<f', d, o)[0]


def cstr(d, o):
    e = d.index(b'\0', o)
    return d[o:e].decode('cp932', 'replace'), e + 1


def disasm(d, start, rec_ofs, stats=None):
    """Yield (ofs, text) for one sequence; stops after the final 0 with no pending return."""
    out = []
    p = start
    seen_zero_depth = 0
    while p < len(d):
        op = d[p]
        if stats is not None:
            stats[op] = stats.get(op, 0) + 1
        if op == 0x22:
            out.append((p, 'TURN_OFS %.1f deg' % f32(d, p + 1))); p += 5
        elif op == 0x21:
            out.append((p, 'TURN_TO_DEST rate=%.2f' % f32(d, p + 1))); p += 5
        elif op == 0x20:
            sub = d[p + 1]
            names = {0: 'TARGET_PLAYER', 1: 'TARGET_PLAYER', 2: 'TARGET_WANDER', 3: 'TARGET_WANDER_FAR', 0x64: 'TARGET_NONE'}
            out.append((p, names.get(sub, 'TARGET_NONE(%d)' % sub))); p += 2
        elif op in (0x10, 0x12, 0x13, 0x14, 0x15):
            v = u32(d, p + 1); a = u32(d, p + 5)
            mn = {0x10: 'SET', 0x12: 'RAND', 0x13: 'ADD', 0x14: 'SUB', 0x15: 'AND'}[op]
            out.append((p, '%s %s, %s' % (mn, var(v), var(a) if op != 0x12 else 'rand(%d)' % a))); p += 9
        elif op == 0x0f:
            out.append((p, 'TAG4 %r' % d[p + 1:p + 5])); p += 5
        elif op == 0x0e:
            s, p2 = cstr(d, p + 1); out.append((p, '; %s' % s)); p = p2
        elif op == 0x06:
            out.append((p, 'SETFLAG8')); p += 1
        elif op == 0x05:
            tag = d[p + 1:p + 5].decode('latin1'); ofs = u32(d, p + 5); push = d[p + 9]
            out.append((p, 'IF_ID %s %s %05x' % (tag, 'CALL' if push else 'GOTO', ofs))); p += 10
        elif op == 0x04:
            v = u32(d, p + 1); a = u32(d, p + 5); c = d[p + 9]; ofs = u32(d, p + 10); push = d[p + 14]
            out.append((p, 'IF %s %s %s %s %05x' % (var(v), CMP[c] if c < 7 else '?%d' % c, var(a), 'CALL' if push else 'GOTO', ofs))); p += 15
        elif op == 0x03:
            s, p2 = cstr(d, p + 1); ofs = u32(d, p2); push = d[p2 + 4]
            out.append((p, 'IF_ACTION "%s" %s %05x' % (s, 'CALL' if push else 'GOTO', ofs))); p = p2 + 5
        elif op == 0x02:
            ofs = u32(d, p + 1); push = d[p + 5]
            out.append((p, '%s %05x' % ('CALL' if push else 'GOTO', ofs))); p += 6
        elif op == 0x01:
            s, p2 = cstr(d, p + 1); out.append((p, 'ACTION "%s"' % s)); p = p2
        elif op == 0x00:
            out.append((p, 'END')); p += 1
            return out, p
        else:
            out.append((p, 'BAD %02x' % op)); p += 1
            return out, p
    return out, p


def main():
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    d = open(PATH, 'rb').read()
    ofs_idx = u32(d, 8)
    max_ops = u32(d, u32(d, 0))
    ids = []
    p = ofs_idx
    while d[p:p + 4] != b'    ':
        ids.append((d[p:p + 4].decode('latin1'), u32(d, p + 4)))
        p += 8
    want = [a for a in sys.argv[1:] if not a.startswith('--')]
    stats = {} if '--stats' in sys.argv else None
    print('maxOpsPerTick=%d records=%d' % (max_ops, len(ids)))
    uniq = sorted(set(o for _, o in ids)) + [len(d)]
    for n, (tag, rec) in enumerate(ids):
        end = uniq[uniq.index(rec) + 1]
        # state table: words until first sequence start (min nonzero offset)
        states = []
        q = rec
        first_seq = end
        while q < first_seq:
            w = u32(d, q)
            if w:
                first_seq = min(first_seq, w)
            states.append(w)
            q += 4
        if want and tag not in want:
            if stats is not None:
                for s in states:
                    if s:
                        disasm(d, s, rec, stats)
            continue
        print('\n== %s rec=%05x states=%s' % (tag, rec, ['%05x' % s for s in states]))
        for si, s in enumerate(states):
            if not s:
                continue
            print('-- state %d @%05x' % (si, s))
            lines, _ = disasm(d, s, rec, stats)
            # Also disassemble until the next state's start, to cover subroutines after END
            nxt = min([x for x in states + [end] if x > s] or [end])
            pos = lines[-1][0] + 1 if lines else s
            while pos < nxt and d[pos] != 0 and d[pos] <= 0x22:
                more, pos2 = disasm(d, pos, rec, stats)
                lines += more
                if pos2 <= pos:
                    break
                pos = pos2
            for o, t in lines:
                print('  %05x  %s' % (o, t))
    if stats is not None:
        print('\nopcode histogram:', {('%02x' % k): v for k, v in sorted(stats.items())})


if __name__ == '__main__':
    main()
