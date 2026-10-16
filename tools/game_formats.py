"""Parse Baroque (PS2, SLUS_217.14) game-data formats: ACT, PRM, TLK, EVT (+ EXI/AISEQ headers, ELF tables).

Usage: python -I tools/game_formats.py [--quiet]
Writes work/game_formats_summary.json. Every structural expectation is asserted; a clean run means zero failures.
Layouts are documented in work/reports/GAME_FORMATS.md (function addresses in SLUS_217.14).
"""
import json
import os
import struct
import sys
from collections import Counter, OrderedDict

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ASSETS = os.path.join(ROOT, 'work', 'assets', 'DATA')
ELF = os.path.join(ROOT, 'work', 'disc', 'SLUS_217.14')
OUT = os.path.join(ROOT, 'work', 'game_formats_summary.json')

u32 = lambda b, o: struct.unpack_from('<I', b, o)[0]
s32 = lambda b, o: struct.unpack_from('<i', b, o)[0]
f32 = lambda b, o: struct.unpack_from('<f', b, o)[0]


def sjis(b):
    return b.decode('cp932', 'replace')


def cstr(b, o):
    e = b.find(b'\0', o)
    assert e >= 0, 'unterminated string at %x' % o
    return b[o:e]


# ---------------------------------------------------------------- ACT (FUN_00143aa0 lookup, FUN_00146950 dispatch)
# 66 command names in table order (static-init table at 0x48e600, 0x18 bytes/entry; handler PTMF at 0x44b358+0x10*i).
ACT_COMMANDS = ['End', 'Action', 'Initial', 'Frame', 'Frame_T', 'AnmSet', 'AnmChg', 'AnmChgEx', 'AnmSpeed', 'AnmFrame',
                'Hit_a', 'Hit_b', 'Hit_c', 'HitEnd', 'Move', 'MoveAcc', 'MoveJump', 'Pad', 'PadEnd', 'BGM', 'Voice', 'SE',
                'SEvol', 'SEatr', 'SEadx', 'SEadxEnd', 'Exi_a', 'Exi_b', 'ExiZofs', 'ExiLpOut', 'ExiEnd', 'Object',
                'ObjThrow', 'ThrowItm', 'DropItem', 'ClearItm', 'Track', 'TrackEx', 'TrackEnd', 'Fade', 'FadeEnd', 'Enemy',
                'CollsRes', 'ChrColls', 'NTurnPos', 'NTurnAct', 'NTurnEnd', 'RotYPos', 'RotYAct', 'RotYEnd', 'Event',
                'Destroy', 'ShowBox', 'HideBox', 'SetPos', 'SetScale', 'AnmScale', 'Invincib', 'Warp', 'SetFlag', 'ChkFlag',
                'GetItem', 'ShowItem', 'HideItem', 'FpsView', 'NrmView']
# container commands: number of leading scalar operands before the nested CMND list (verified in handlers)
ACT_CONTAINERS = {'Initial': 0, 'Frame': 1, 'Frame_T': 1, 'PadEnd': 1, 'CollsRes': 1, 'ChkFlag': 2, 'FpsView': 0, 'NrmView': 0}
# operand decoding templates (verified from handler pseudocode): i=int, f=float, s4=4-char id, s8=8-char name, S=NUL string padded to 4
ACT_OPERANDS = {'Action': 's8', 'AnmSet': 's4', 'AnmChg': 's4', 'AnmChgEx': 's4 f i f', 'AnmSpeed': 'f', 'AnmFrame': 'i',
                'Hit_a': 'i f i i i i f S', 'Hit_b': 'i f i i i i f S S', 'Hit_c': 'i f i i i i f S f f i', 'HitEnd': 'i',
                'Move': 'f f', 'MoveAcc': 'f', 'MoveJump': 'f f f', 'Pad': 'i', 'BGM': 'i', 'Voice': 'i', 'SE': 'i', 'SEvol': 'i f',
                'SEatr': 'i i i i', 'SEadx': 'i i', 'SEadxEnd': 'i', 'Exi_a': 'i i i i f i i i', 'Exi_b': 'i i i i f i i i S',
                'ExiZofs': 'i f', 'ExiLpOut': 'i', 'ExiEnd': 'i', 'Object': 's4 f f f S', 'ObjThrow': 's4 f f f i i S',
                'ThrowItm': 's4 f f f i f f S', 'DropItem': 's4 f f f S', 'ClearItm': 'i', 'Track': 'i S', 'TrackEx': 'i i i i S',
                'TrackEnd': 'i i', 'Fade': 'i i i f f f f f f f f', 'FadeEnd': 'i', 'Enemy': 's4 s8 f f f f S', 'ChrColls': 'f f',
                'NTurnPos': 'f f f', 'NTurnAct': 's4 S', 'NTurnEnd': 'i', 'RotYPos': 'f f f f', 'RotYAct': 's4 f f', 'RotYEnd': 'i',
                'Event': 'i', 'Destroy': 'i', 'ShowBox': 'i', 'HideBox': 'i', 'SetPos': 'f f f f', 'SetScale': 'f f f',
                'AnmScale': 'i', 'Invincib': 'i', 'Warp': 'i', 'SetFlag': 'i i', 'GetItem': 'i', 'ShowItem': 'i f f S',
                'HideItem': 'i', 'End': 'i'}


def decode_operands(name, body):
    """Decode scalar operands per template; returns list. Unknown leftovers are returned as raw words."""
    tmpl = ACT_OPERANDS.get(name, '').split()
    out, q = [], 0
    for t in tmpl:
        if q >= len(body):
            break
        if t == 'i':
            out.append(s32(body, q)); q += 4
        elif t == 'f':
            out.append(round(f32(body, q), 6)); q += 4
        elif t == 's4':
            out.append(body[q:q + 4].rstrip(b'\0').decode('ascii', 'replace')); q += 4
        elif t == 's8':
            out.append(body[q:q + 8].rstrip(b'\0').decode('ascii', 'replace')); q += 8
        elif t == 'S':
            s = cstr(body, q)
            out.append(s.decode('ascii', 'replace')); q += (len(s) + 4) & ~3
    while q + 4 <= len(body):
        out.append(('raw', hex(u32(body, q)))); q += 4
    return out


def parse_cmnds(d, p, end, hist, depth=0):
    cmds = []
    while p + 8 <= end:
        tag = d[p:p + 4]
        if tag != b'CMND':
            assert tag in (b'ACTN', b'\0\0\0\0'), 'bad chunk tag %r at %x' % (tag, p)
            break
        size = u32(d, p + 4)
        assert size >= 12 and (p + 8 + size) <= end and size % 4 == 0, 'bad CMND size %d at %x' % (size, p)
        name = d[p + 8:p + 16].split(b'\0')[0].decode('ascii')
        assert name in ACT_COMMANDS, 'unknown command %r at %x' % (name, p)
        argc = u32(d, p + 16)
        body = d[p + 20:p + 8 + size]
        hist[name] += 1
        cmd = OrderedDict(cmd=name, argc=argc)
        if name in ACT_CONTAINERS:
            lead = ACT_CONTAINERS[name]
            scalars = [s32(body, 4 * i) for i in range(lead)]
            nested = parse_cmnds(body, 4 * lead, len(body), hist, depth + 1)
            assert len(nested) == argc - lead, '%s argc=%d lead=%d nested=%d at %x' % (name, argc, lead, len(nested), p)
            cmd['args'] = scalars
            cmd['body'] = nested
        else:
            assert b'CMND' not in body, 'unexpected nested CMND in %s at %x' % (name, p)
            cmd['args'] = decode_operands(name, body)
        cmds.append(cmd)
        p += 8 + size
    return cmds


def parse_act(path, hist):
    d = open(path, 'rb').read()
    assert len(d) % 2048 == 0, path
    actions, p = [], 0
    while p + 8 <= len(d) and d[p:p + 4] == b'ACTN':
        size = u32(d, p + 4)
        assert size >= 8 and p + 8 + size <= len(d) and size % 4 == 0, 'bad ACTN size at %x' % p
        name = d[p + 8:p + 16].split(b'\0')[0].decode('ascii')
        cmds = parse_cmnds(d, p + 16, p + 8 + size, hist)
        actions.append((name, cmds))
        p += 8 + size
    # list terminator: a zero word (FUN_00143fe0 stops on 0 or 'ACTN'); bytes after it are stale buffer contents
    assert actions and (p >= len(d) or u32(d, p) == 0), 'bad ACTN list terminator in %s' % path
    return actions


# ---------------------------------------------------------------- PRM (FUN_00133480 PRMCHAR, FUN_001d3600 PRMITEM)
ITEM_STRIDES = [0xa0, 0x8c, 0x88, 0x80, 0x80, 0x8c, 0x80, 0x80, 0x84, 0x84, 0x7c, 0x80, 0x74, 0x74]  # jump table 0x499450
ITEM_PREFIX = ['SD', 'CT', 'WG', 'HT', 'MT', 'BX', 'LQ', 'ST', 'TL', 'TP', 'BN', 'WM', 'GN', 'IS']  # table 0x463eb0
ITEM_KIND = ['sword', 'coat', 'wings', 'heart', 'flesh', 'box', 'fluid', 'brand', 'torturer', 'disk', 'bone', 'parasite', 'gun', 'crystal']


def rec_strings(d, r):
    nm, ds = u32(d, r + 4), u32(d, r + 8)
    assert 0 < nm < len(d) and 0 < ds < len(d), 'bad name/desc offset'
    return sjis(cstr(d, nm)), sjis(cstr(d, ds))


def parse_prmchar(path):
    d = open(path, 'rb').read()
    recs, off = [], 0
    while d[off:off + 4] != b'\0\0\0\0':
        rid = d[off:off + 4].decode('ascii')
        assert rid[0] in 'CE' and rid[1:3].isdigit(), rid
        name, desc = rec_strings(d, off)
        rec = OrderedDict(id=rid, name=name, desc=desc,
                          hp=s32(d, off + 0xc), vt=s32(d, off + 0x10), f14=s32(d, off + 0x14), f18=s32(d, off + 0x18),
                          f1c=s32(d, off + 0x1c), f20=s32(d, off + 0x20), f24=s32(d, off + 0x24),
                          v28=[s32(d, off + 0x28 + 4 * i) for i in range(3)], v34=[s32(d, off + 0x34 + 4 * i) for i in range(3)],
                          resist=[s32(d, off + 0x40 + 4 * i) for i in range(14)],
                          w78=[struct.unpack_from('<4H', d, off + 0x78 + 8 * i) for i in range(3)],
                          flesh_hp=s32(d, off + 0x90), flesh_vt=s32(d, off + 0x94),
                          floats98=[round(f32(d, off + 0x98 + 4 * i), 3) for i in range(7)])
        recs.append(rec)
        off += 0xb4
    # a zero id word terminates the list; the name/description string pool follows it
    assert recs and u32(d, off) == 0 and all(u32(d, r * 0xb4 + 4) > off for r in range(len(recs))), path
    return recs


def parse_prmitem(path, idx):
    d = open(path, 'rb').read()
    st = ITEM_STRIDES[idx]
    recs, off = [], 0
    while u32(d, off) != 0:
        rid = d[off:off + 4].decode('ascii')
        assert rid.startswith(ITEM_PREFIX[idx]) and rid[2:].isdigit(), (path, rid)
        name, desc = rec_strings(d, off)
        var = [s32(d, off + 0x2c + 4 * i) for i in range(16)]
        if idx == 11:  # parasite: 16 x 4-char ids of the grown form
            var = [d[off + 0x2c + 4 * i:off + 0x30 + 4 * i].rstrip(b'\0').decode('ascii', 'replace') for i in range(16)]
        eff = []
        for i in range(2):
            k, t, v, x, c = struct.unpack_from('<BBhhh', d, off + 0x78 + 8 * i)
            eff.append(OrderedDict(kind=k, type=t, value=v, x=x, count=c))
        rec = OrderedDict(id=rid, name=name, desc=desc, drop_weight=[s32(d, off + 0xc + 4 * i) for i in range(4)],
                          f1c=[s32(d, off + 0x1c + 4 * i) for i in range(4)], variant16=var, th=s32(d, off + 0x6c),
                          f70=s32(d, off + 0x70), chance74=s32(d, off + 0x74), effects=eff,
                          tail=[s32(d, off + 0x88 + 4 * i) for i in range((st - 0x88) // 4)])
        recs.append(rec)
        off += st
    assert recs and all(u32(d, r * st + 4) > off for r in range(len(recs))), path  # string pool follows the 0 terminator
    return recs


# ---------------------------------------------------------------- TLK (FUN_001a7740 / FUN_001a77c0 / FUN_001a80c0)
TLK_PAGE_OPS = {2: 0, 3: 1, 4: 1, 7: 1, 8: 1, 9: 1}  # 100 = trigger block (0x94 words), 201 = end
TLK_TRIGGER_SIZES = {0x65: 6, 0x66: 3, 0x67: 2, 0x68: 6, 0x69: 4, 0x6a: 6, 0x6b: 6, 0x6c: 3, 0x6d: 3, 0x6e: 6, 0x6f: 3,
                     0x70: 6, 0x71: 3, 0x72: 2, 0x73: 2}


def words_as_text(ws):
    return b''.join(struct.pack('<I', w & 0xffffffff) for w in ws).split(b'\0')[0].decode('ascii', 'replace')


def parse_tlk(path):
    d = open(path, 'rb').read()
    assert len(d) == 2048, path
    text = sjis(d[:0x400].rstrip(b'\0').rstrip(b' '))
    assert u32(d, 0x400) == 1, (path, hex(u32(d, 0x400)))
    w = list(struct.unpack_from('<%di' % ((len(d) - 0x404) // 4), d, 0x404))
    pc, ops, triggers = 0, OrderedDict(), []
    while True:
        assert pc < len(w), 'no terminator in %s' % path
        op = w[pc]
        if op == 201:
            break
        if op == 100:
            blk = w[pc + 1:pc + 1 + 0x94]
            pc += 1 + 0x94
            q = 0
            while q < len(blk) and blk[q] != 0:
                k = blk[q]
                assert k in TLK_TRIGGER_SIZES, 'bad trigger kind %d in %s' % (k, path)
                n = TLK_TRIGGER_SIZES[k]
                args = blk[q + 1:q + n]
                tr = OrderedDict(kind=k, frame=args[0], args=args[1:])
                if k in (0x69, 0x6a, 0x6b, 0x68, 0x70) and len(args) >= 3:
                    tr['name'] = words_as_text(args[1:3])
                triggers.append(tr)
                q += n
            continue
        assert op in TLK_PAGE_OPS, 'bad page opcode %d at %d in %s' % (op, pc, path)
        n = TLK_PAGE_OPS[op]
        ops[str(op)] = w[pc + 1] if n else True
        pc += 1 + n
    lines = [l for l in text.split(';') if l != '']
    return OrderedDict(text=text, lines=len(lines), page=ops, triggers=triggers)


# ---------------------------------------------------------------- EVT (FUN_0016be20 parse, FUN_00175e30 cue dispatch)
EVT_OPS = {1: 1, 5: 0, 6: 1, 7: 3, 8: 3, 9: 1, 10: 1, 11: 1, 12: 1, 13: 1, 14: 1, 15: 2, 16: 4}  # 20 = cue (64 words), 201 = end
EVT_CUE_OPS = {27, 28, 110, 111, 29, 30, 48, 31, 150, 32, 33, 105, 34, 35, 50, 36, 37, 100, 101, 38, 51, 52, 53, 112, 113, 39,
               40, 41, 189, 42, 43, 44, 45, 46, 47, 55, 59, 56, 63, 57, 58, 60, 61, 62, 75, 76, 80, 81, 85, 86, 87, 88, 198,
               199, 90, 91, 98, 92, 93, 94, 95, 96, 97, 190, 191, 120, 121, 123, 124, 125, 126, 133, 132, 128, 129, 130, 137,
               138, 139, 140, 141, 142, 192, 193, 180, 200}


def parse_evt(path):
    d = open(path, 'rb').read()
    assert len(d) % 2048 == 0 and len(d) > 0x808, path
    text = sjis(d[:0x800].rstrip(b'\0').rstrip(b' '))
    lines = [l for l in text.split(';') if l != '']
    map_id, count = s32(d, 0x800), s32(d, 0x804)
    w = list(struct.unpack_from('<%di' % ((len(d) - 0x808) // 4), d, 0x808))
    pc, actors, cues, cur = 0, [], [], None
    while True:
        assert pc < len(w), 'no terminator in %s' % path
        op = w[pc]
        if op == 201:
            break
        if op == 20:
            blk = w[pc + 1:pc + 65]
            assert len(blk) == 64 and (blk[0] == 0 or blk[0] in EVT_CUE_OPS), (path, blk[:4])
            cues.append(blk)
            pc += 65
            continue
        assert op in EVT_OPS, 'bad EVT opcode %d at %d in %s' % (op, pc, path)
        n = EVT_OPS[op]
        if op == 5:
            cur = OrderedDict(); actors.append(cur)
        elif op == 1:
            cur = None  # 'current id' used by later 5
        if cur is not None and op != 5:
            cur[str(op)] = w[pc + 1:pc + 1 + n]
        pc += 1 + n
    assert len(actors) <= 300 and len(cues) <= 400, path
    assert count + 1 >= len(cues) - 1, (path, count, len(cues))
    cue_ops = Counter(b[0] for b in cues)
    return OrderedDict(map=map_id, count=count, lines=len(lines), actors=len(actors), cues=len(cues),
                       cue_first_ops=dict((str(k), v) for k, v in sorted(cue_ops.items())), text_head=text[:120])


# ---------------------------------------------------------------- EXI / AISEQ / ELF tables
def parse_exi(path):
    d = open(path, 'rb').read()
    assert d[:4] == b'FORM' and d[8:12] == b'EXIF', path
    total, version = u32(d, 4), u32(d, 0xc)  # FUN_001e8b20 copies a 16-byte header, chunks start at 0x10
    assert version == 1 and total <= len(d), (total, version)
    p, chunks = 16, []
    while p + 8 <= min(len(d), total):  # 'total' is the end offset of the chunk list (0x76e50 for BQ.EXI)
        tag, size = d[p:p + 4].decode('ascii'), u32(d, p + 4)  # size includes the 8-byte chunk header
        assert size >= 8 and p + size <= len(d), (tag, size)
        chunks.append((tag, size))
        p += size
    assert [c[0] for c in chunks] and all(c[0] in ('VTXS', 'OBJS', 'KFMS', 'SEQS', 'RSQS', 'EMTS', 'EFCS') for c in chunks), chunks
    return chunks


def parse_aiseq(path):
    d = open(path, 'rb').read()
    offs = struct.unpack_from('<3I', d, 0)
    assert offs == (0xc, 0x10, 0x14), offs
    idx, ents = offs[2], []
    while d[idx:idx + 4] != b'    ':
        ents.append((d[idx:idx + 4].decode('ascii'), u32(d, idx + 4)))
        idx += 8
    assert ents and all(o < len(d) for _, o in ents)
    return OrderedDict(a=u32(d, 0xc), b=u32(d, 0x10), chars=[e[0] for e in ents])


def elf_tables():
    if not os.path.exists(ELF):
        return None
    e = open(ELF, 'rb').read()
    rd = lambda a, n: e[a - 0x100000 + 0x80:a - 0x100000 + 0x80 + n]
    wd = lambda a: u32(rd(a, 4), 0)
    cst = lambda a: rd(a, 256).split(b'\0')[0].decode('latin1')
    handlers = [hex(wd(0x44b358 + 0x10 * i)) for i in range(66)]
    assert all(wd(0x44b354 + 0x10 * i) == 0xffffffff for i in range(66))
    tex = []
    for i in range(286):
        en, grp, jp = wd(0x48a280 + 16 * i), wd(0x48a284 + 16 * i), wd(0x48a288 + 16 * i)
        tex.append([cst(en) if en else None, cst(jp) if jp else None, cst(grp) if grp else None])
    jp = rd(0x48b470, 0xa80); en = rd(0x48bf00, 0xe6)
    glyph_jp = [sjis(jp[i:i + 2]) for i in range(0, len(jp), 2)]
    glyph_en = [sjis(en[i:i + 2]) for i in range(0, len(en), 2)]
    metrics = [list(struct.unpack_from('<3f', rd(0x48bff0 + 12 * i, 12), 0)) for i in range(0x159)]
    talk = []
    for i in range(0x636):
        a = 0x457020 + 12 * i
        talk.append([cst(wd(a)), cst(wd(a + 4)), cst(wd(a + 8))])
    assert talk[0][0] == 'C00A_V15B' and talk[-1][0] == 'C27_V001'
    return OrderedDict(act_command_handlers=OrderedDict(zip(ACT_COMMANDS, handlers)), texture_table=tex,
                       glyph_codes_jp=glyph_jp, glyph_codes_en=glyph_en, glyph_metrics_en=metrics, talk_table=talk)


# ---------------------------------------------------------------- main
def main():
    quiet = '--quiet' in sys.argv
    summary = OrderedDict()
    # ACT
    hist, acts = Counter(), OrderedDict()
    adir = os.path.join(ASSETS, 'ACTION')
    for fn in sorted(os.listdir(adir)):
        actions = parse_act(os.path.join(adir, fn), hist)
        acts[fn] = OrderedDict(actions=len(actions), names=[a[0] for a in actions])
    assert len(acts) == 148, len(acts)
    sample = parse_act(os.path.join(adir, 'C00A.ACT'), Counter())[0]
    summary['act'] = OrderedDict(files=len(acts), total_actions=sum(a['actions'] for a in acts.values()),
                                 command_histogram=OrderedDict(sorted(hist.items(), key=lambda kv: -kv[1])),
                                 commands_in_table_order=ACT_COMMANDS, example=OrderedDict(action=sample[0], cmds=sample[1]),
                                 files_detail=acts)
    # PRM
    prm = OrderedDict()
    for lang in ('PARAM', 'PARAM_EN'):
        chars = parse_prmchar(os.path.join(ASSETS, lang, 'PRMCHAR.PRM'))
        items = OrderedDict()
        for i in range(14):
            items['PRMITEM%02d' % i] = parse_prmitem(os.path.join(ASSETS, lang, 'PRMITEM%02d.PRM' % i), i)
        prm[lang] = OrderedDict(chars=chars, items=items)
    assert len(prm['PARAM']['chars']) == len(prm['PARAM_EN']['chars']) == 149
    for k in prm['PARAM']['items']:
        assert len(prm['PARAM']['items'][k]) == len(prm['PARAM_EN']['items'][k]), k
    en = prm['PARAM_EN']
    summary['prm'] = OrderedDict(
        prmchar_records=len(en['chars']), prmchar_stride=0xb4,
        prmitem_strides=OrderedDict(('PRMITEM%02d' % i, [ITEM_PREFIX[i], ITEM_KIND[i], ITEM_STRIDES[i], len(en['items']['PRMITEM%02d' % i])]) for i in range(14)),
        characters_en=en['chars'], items_en=en['items'],
        characters_jp_names=[c['name'] for c in prm['PARAM']['chars']])
    # TLK
    tlk, nt = OrderedDict(), 0
    for lang in ('TALK', 'TALK_EN'):
        tdir = os.path.join(ASSETS, lang)
        for fn in sorted(os.listdir(tdir)):
            r = parse_tlk(os.path.join(tdir, fn))
            nt += 1
            if lang == 'TALK_EN':
                tlk[fn] = r
    assert nt == 3180, nt
    summary['tlk'] = OrderedDict(files=nt, en_files=len(tlk), page_opcode_sizes=TLK_PAGE_OPS, trigger_sizes=dict((hex(k), v) for k, v in TLK_TRIGGER_SIZES.items()),
                                 trigger_kind_histogram=dict(Counter(hex(t['kind']) for r in tlk.values() for t in r['triggers'])),
                                 scripts_en=tlk)
    # EVT
    evt, ne = OrderedDict(), 0
    for lang in ('EVENT', 'EVENT_EN'):
        edir = os.path.join(ASSETS, lang)
        for fn in sorted(os.listdir(edir)):
            r = parse_evt(os.path.join(edir, fn))
            ne += 1
            if lang == 'EVENT_EN':
                evt[fn] = r
    assert ne == 108, ne
    summary['evt'] = OrderedDict(files=ne, opcode_sizes=EVT_OPS, cue_opcodes=sorted(EVT_CUE_OPS), events_en=evt)
    # EXI / AI / packs
    summary['exi'] = OrderedDict(chunks=parse_exi(os.path.join(ASSETS, 'EXI', 'BQ.EXI')))
    summary['aiseq'] = parse_aiseq(os.path.join(ASSETS, 'AI', 'AISEQ.BIN'))
    packs = OrderedDict()
    pdir = os.path.join(ROOT, 'work', 'assets', 'PACKS')
    for p in ('J022', 'J023', 'J024', 'J026', 'OBJ_ITEM', 'FONT', 'PARAM'):
        files = []
        for base, _, fs in os.walk(os.path.join(pdir, p)):
            files += [os.path.relpath(os.path.join(base, f), os.path.join(pdir, p)).replace('\\', '/') for f in fs]
        packs[p] = sorted(files)
    summary['packs'] = packs
    t = elf_tables()
    if t:
        summary['elf_tables'] = t
    with open(OUT, 'w', encoding='utf-8') as f:
        json.dump(summary, f, ensure_ascii=False, indent=1)
    if not quiet:
        print('ACT files %d, actions %d, commands %d' % (len(acts), summary['act']['total_actions'], sum(hist.values())))
        print('PRMCHAR %d records; items %s' % (len(en['chars']), [len(v) for v in en['items'].values()]))
        print('TLK %d, EVT %d, EXI chunks %s, AISEQ chars %d' % (nt, ne, summary['exi']['chunks'], len(summary['aiseq']['chars'])))
        print('wrote', OUT)


if __name__ == '__main__':
    main()
