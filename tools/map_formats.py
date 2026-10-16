#!/usr/bin/env python3
"""Baroque (PS2, SLUS_217.14) dungeon map formats: FDT (floor), RDT (room), CLS (collision).

Parses every DATA/MAP/FLOOR/F%03d.FDT, DATA/MAP/ROOM/{MO,ME,MR}%02d_%02d.RDT and
DATA/MAP/M%02d/*.CLS under work/assets (main tree and PACKS/F*/), asserting that every
byte of every file is accounted for, and writes work/map_formats_summary.json.

Layouts were recovered from the loaders in the ELF (see work/reports/MAP_FORMATS.md):
  FDT  : FUN_0011fa00 / FUN_00122d40 / FUN_00123dc0 / FUN_00121a00 / FUN_00121f80
  RDT  : FUN_00122e20 / FUN_00125420 / FUN_00125fc0 / FUN_00120a70 / FUN_0011edf0 / FUN_0011f160
  CLS  : FUN_0013fd30 -> FUN_0020b6e0 (relocation) / FUN_0020b630 / FUN_00209fa0

Main-tree files are stored sector-padded (DATA.DIR size is a multiple of 2048 and the tail is
garbage from neighbouring files); pack copies carry the true size.  For padded files the parsed
end must lie within the last sector and, when a pack twin exists, the twin's true size must equal
the parsed end and its bytes must match.

Usage: python -I tools/map_formats.py [--root work/assets] [--out work/map_formats_summary.json]
"""
import argparse
import collections
import json
import os
import re
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)

SECTOR = 2048


def u8(b, o):
    return b[o]


def u32(b, o):
    return struct.unpack_from('<I', b, o)[0]


def s32(b, o):
    return struct.unpack_from('<i', b, o)[0]


def f32(b, o):
    return struct.unpack_from('<f', b, o)[0]


def tag(b, o):
    """4-byte ASCII tag (compared by the game as a little-endian u32)."""
    t = b[o:o + 4]
    assert all(32 <= c < 127 or c == 0 for c in t), ('non-ascii tag', t)
    return t.rstrip(b'\0').decode('ascii')


def align(v, a):
    return (v + a - 1) & ~(a - 1)


# --------------------------------------------------------------------------------------- FDT
def parse_fdt(b):
    """F%03d.FDT.  Header 0x1c bytes, then room-id byte table, then 8-byte entity table."""
    assert len(b) >= 0x1c, 'FDT too short'
    n_rooms = u8(b, 5)
    n_ents = u8(b, 6)
    room_off = u32(b, 0x14)
    ent_off = u32(b, 0x18)
    assert room_off == 0x1c, hex(room_off)
    assert ent_off == align(0x1c + n_rooms, 4), (hex(ent_off), n_rooms)
    assert 1 <= n_rooms <= 16, n_rooms              # room object table at 0x50a640 holds 16 slots
    rooms = list(b[room_off:room_off + n_rooms])
    assert len(set(rooms)) == len(rooms), 'duplicate room id'
    pad = b[room_off + n_rooms:ent_off]
    assert set(pad) <= {0}, pad
    ents = []
    for i in range(n_ents):
        o = ent_off + 8 * i
        ents.append({'tag': tag(b, o), 'count': u32(b, o + 4)})
        assert ents[-1]['count'] < 256
    end = ent_off + 8 * n_ents
    return {
        'map_set': u8(b, 0),                         # M%02d directory / texture set
        'room_count_choices': [u8(b, 1), u8(b, 2), u8(b, 3), u8(b, 4)],  # FUN_00123dc0: hdr[1+rand(0..3)]
        'room_count': n_rooms,
        'entity_table_count': n_ents,
        'random_item_count': u8(b, 7),               # FUN_00121f80
        'unused_08_13': list(b[8:0x14]),             # not referenced by any decompiled function
        'room_ids': rooms,
        'entities': ents,
    }, end


# --------------------------------------------------------------------------------------- RDT
CELL_TYPE_MASK = 0xf000
CELL_LEVEL_MASK = 0xf00


def parse_rdt(b, want_grid=True):
    """{MO,ME,MR}%02d_%02d.RDT.  0x20-byte header, cell grid, doors, spawns, emitters, lights."""
    assert len(b) >= 0x20
    room_id, w8, h8, n_doors, n_spawns, n_emit, n_lights, flags = b[0:8]
    flag_id = u32(b, 8)
    grid_off, door_off, spawn_off, emit_off, light_off = (u32(b, o) for o in (0xc, 0x10, 0x14, 0x18, 0x1c))
    w, h = w8 * 8, h8 * 8
    assert 1 <= w8 <= 6 and 1 <= h8 <= 5, (w8, h8)
    assert grid_off == 0x20, hex(grid_off)
    assert flags & ~0x07 == 0, hex(flags)           # bit0 start-candidate, bit1 need flag set, bit2 need flag clear
    assert (flag_id != 0) == bool(flags & 6), (flags, flag_id)
    pos = grid_off + w * h * 4
    cells = []
    hist = collections.Counter()
    for i in range(w * h):
        c = u32(b, grid_off + 4 * i)
        t = c & CELL_TYPE_MASK
        assert t in (0, 0x1000), hex(c)             # object cells (0x2000) are only generated at runtime
        assert c & 0x0f == 0, hex(c)                 # low nibble is overwritten with the room index at load
        assert c & 0xffff_0000 & ~0x0f07_0000 == 0, hex(c)  # only bits 16-18 and 24-27 occur in files
        if t == 0:
            assert c == 0, hex(c)
        hist[c] += 1
        cells.append(c)
    # doors
    if n_doors:
        assert door_off == pos, (hex(door_off), hex(pos))
    else:
        assert door_off == 0
    doors = []
    for i in range(n_doors):
        o = door_off + 4 * i
        d, x, y, f = b[o:o + 4]
        assert d < 4 and x < w and y < h, (d, x, y)
        assert f & ~0x0f == 0, hex(f)                # bit4 'connected' is runtime only
        assert cells[y * w + x] & CELL_TYPE_MASK == 0x1000, 'door not on a floor cell'
        assert ((cells[y * w + x] >> 8) & 0xf != 0) == (f & 0xf != 0), 'door level != cell level'
        doors.append({'dir': d, 'x': x, 'y': y, 'level': f & 0xf})
    pos = door_off + 4 * n_doors if n_doors else pos
    # spawns (0x18 bytes)
    if n_spawns:
        assert spawn_off == pos, (hex(spawn_off), hex(pos))
    else:
        assert spawn_off == 0
    spawns = []
    for i in range(n_spawns):
        o = spawn_off + 0x18 * i
        spawns.append({'tag': tag(b, o), 'x': f32(b, o + 4), 'y': f32(b, o + 8), 'z': f32(b, o + 12),
                       'angle': f32(b, o + 16), 'cond_flag': s32(b, o + 20)})
    pos = spawn_off + 0x18 * n_spawns if n_spawns else pos
    # effect emitters (0x18 bytes) -> FUN_0011edf0 / FUN_00112190
    if n_emit:
        assert emit_off == pos, (hex(emit_off), hex(pos))
    else:
        assert emit_off == 0
    emitters = []
    for i in range(n_emit):
        o = emit_off + 0x18 * i
        emitters.append({'effect_id': u32(b, o), 'x': f32(b, o + 4), 'y': f32(b, o + 8), 'z': f32(b, o + 12),
                         'rot_y': f32(b, o + 16), 'scale': f32(b, o + 20)})
    pos = emit_off + 0x18 * n_emit if n_emit else pos
    # lights (0x20 bytes) -> FUN_0011f160
    if n_lights:
        assert light_off == pos, (hex(light_off), hex(pos))
    else:
        assert light_off == 0
    lights = []
    for i in range(n_lights):
        o = light_off + 0x20 * i
        lights.append({'type': u32(b, o), 'x': f32(b, o + 4), 'y': f32(b, o + 8), 'z': f32(b, o + 12),
                       'r': f32(b, o + 16), 'g': f32(b, o + 20), 'b': f32(b, o + 24), 'radius': f32(b, o + 28)})
    end = light_off + 0x20 * n_lights if n_lights else pos
    out = {
        'room_id': room_id, 'w8': w8, 'h8': h8, 'w': w, 'h': h, 'flags': flags, 'flag_id': flag_id,
        'door_count': n_doors, 'spawn_count': n_spawns, 'emitter_count': n_emit, 'light_count': n_lights,
        'cell_histogram': {'0x%08x' % k: v for k, v in sorted(hist.items())},
        'doors': doors, 'spawns': spawns, 'emitters': emitters, 'lights': lights,
    }
    if want_grid:
        rows = []
        for y in range(h):
            rows.append(''.join(
                '.' if cells[y * w + x] == 0 else ('^' if cells[y * w + x] & CELL_LEVEL_MASK else '#')
                for x in range(w)))
        out['grid'] = rows
        out['cell_attr'] = sorted({'0x%03x' % (c >> 16) for c in cells if c})
    return out, end


# --------------------------------------------------------------------------------------- CLS
FACE_SIZE = 0x44


def parse_cls(b, want_faces=False):
    """*.CLS collision mesh.  0x64-byte header, vertices, vectors, faces, spatial grid cells."""
    assert len(b) >= 0x64
    assert u32(b, 0) == 0 and u32(b, 4) == 0x01000000 and u32(b, 8) == 100, 'bad CLS header'
    vert_off, n_vert, vec_off, n_vec, face_off, n_face = (u32(b, o) for o in (8, 0xc, 0x10, 0x14, 0x18, 0x1c))
    bbox = [f32(b, 0x20 + 4 * i) for i in range(6)]
    cell_off, n_cell = u32(b, 0x38), u32(b, 0x3c)
    cell_size = [f32(b, 0x40 + 4 * i) for i in range(3)]
    grid_n = [u32(b, 0x4c), u32(b, 0x50), u32(b, 0x54)]
    grid_o = [s32(b, 0x58), s32(b, 0x5c), s32(b, 0x60)]
    assert vert_off == 0x64
    assert vec_off == vert_off + 12 * n_vert
    assert face_off == align(vec_off + 12 * n_vec, 16)
    assert set(b[vec_off + 12 * n_vec:face_off]) <= {0}
    assert cell_off == face_off + FACE_SIZE * n_face
    assert grid_n[0] * grid_n[1] * grid_n[2] == n_cell, (grid_n, n_cell)
    assert grid_n[1] == 1 and grid_o[1] == 0
    face_types = collections.Counter()
    attrs = collections.Counter()
    faces = []
    for i in range(n_face):
        o = face_off + FACE_SIZE * i
        ty = chr(b[o])
        assert ty in 'WFL', ty
        attr = b[o + 1:o + 8]
        assert all(32 <= c < 127 or c == 0 for c in attr), attr
        face_types[ty] += 1
        attrs[attr.rstrip(b'\0').decode('ascii')] += 1
        assert u32(b, o + 8) == 0 and u32(b, o + 0x40) == 0   # runtime fields
        def vec(p):
            assert vec_off <= p < face_off and (p - vec_off) % 12 == 0, hex(p)
            return (p - vec_off) // 12
        def vert(p):
            assert vert_off <= p < vec_off and (p - vert_off) % 12 == 0, hex(p)
            return (p - vert_off) // 12
        def face(p):
            if p == 0:
                return None
            assert face_off <= p < cell_off and (p - face_off) % FACE_SIZE == 0, hex(p)
            return (p - face_off) // FACE_SIZE
        rec = {'type': ty, 'attr': attr.rstrip(b'\0').decode('ascii'), 'normal': vec(u32(b, o + 0xc)),
               'verts': [vert(u32(b, o + 0x10 + 4 * k)) for k in range(3)],
               'edges': [{'vec_a': vec(u32(b, o + 0x1c + 12 * k)), 'vec_b': vec(u32(b, o + 0x20 + 12 * k)),
                          'neighbor': face(u32(b, o + 0x24 + 12 * k))} for k in range(3)]}
        if want_faces:
            faces.append(rec)
    # grid cells: {u32 facelist_off, u32 count}; face lists follow the cell table in order
    end = cell_off + 8 * n_cell
    n_refs = 0
    for i in range(n_cell):
        lp, lc = u32(b, cell_off + 8 * i), u32(b, cell_off + 8 * i + 4)
        if lp == 0:
            assert lc == 0
            continue
        assert lp == end, (i, hex(lp), hex(end))
        for j in range(lc):
            fp = u32(b, lp + 4 * j)
            assert face_off <= fp < cell_off and (fp - face_off) % FACE_SIZE == 0, hex(fp)
        end += 4 * lc
        n_refs += lc
    out = {
        'vertex_count': n_vert, 'vector_count': n_vec, 'face_count': n_face,
        'face_types': dict(face_types), 'face_attrs': dict(attrs),
        'bbox_min': bbox[:3], 'bbox_max': bbox[3:],
        'grid_cells': n_cell, 'grid_cell_size': cell_size, 'grid_dims': grid_n, 'grid_origin': grid_o,
        'grid_face_refs': n_refs,
    }
    if want_faces:
        out['vertices'] = [[f32(b, vert_off + 12 * i + 4 * k) for k in range(3)] for i in range(n_vert)]
        out['vectors'] = [[f32(b, vec_off + 12 * i + 4 * k) for k in range(3)] for i in range(n_vec)]
        out['faces'] = faces
    return out, end


# --------------------------------------------------------------------------------- ELF tables
def elf_tables():
    """Static generator tables and the 21 fixed scenario layouts, read from the ELF (optional)."""
    try:
        sys.path.insert(0, HERE)
        import disasm  # noqa
        if not os.path.exists(disasm.ELF):
            return None
    except Exception:
        return None
    W = disasm.word
    def sw(v):
        return v - (1 << 32) if v & 0x80000000 else v
    def words(a, n, signed=False):
        return [(sw(W(a + 4 * i)) if signed else W(a + 4 * i)) for i in range(n)]
    def cstr(a):
        return disasm.cstring(a)
    t = {
        'dir_delta_0x48de20': [words(0x48de20 + 8 * i, 2, True) for i in range(4)],    # N,E,S,W (dx,dy)
        'door_cell_delta_0x48de40': [words(0x48de40 + 8 * i, 2, True) for i in range(4)],
        'door_object_by_dir_0x48de60': words(0x48de60, 4),
        'door_end_object_by_dir_0x48ddf0': words(0x48ddf0, 4),
        'dir_mask_out_0x48de00': words(0x48de00, 4),
        'dir_mask_in_0x48de10': words(0x48de10, 4),
        'corridor_object_by_mask_0x48dd70': words(0x48dd70, 16, True),
        'mask_by_object_0x48ddb0': words(0x48ddb0, 16),
        'object_model_0x48dcd0': [{'mt': W(0x48dcd0 + 8 * i), 'rot_rad': struct.unpack('<f', struct.pack('<I', W(0x48dcd4 + 8 * i)))[0]}
                                   for i in range(0x13)],
        'mt_ids_with_models_0x44a140': words(0x44a140, 6),
    }
    layouts = {}
    for name, base in (('scenario_A_300', 0x44a1b0), ('scenario_B_311', 0x44a1d0), ('scenario_C_321', 0x44a1f0)):
        lst = []
        for i in range(7):
            ent = W(base + 4 * i)
            conn_p, spawn_p = W(ent), W(ent + 4)
            conns = []
            p = conn_p
            while True:
                a, ad, bb, bd = (W(p) >> s & 0xff for s in (0, 8, 16, 24))
                if a == 0xff:
                    break
                conns.append({'room': a, 'door': None if ad == 0xff else ad, 'room2': bb, 'door2': None if bd == 0xff else bd})
                p += 4
            def rec18(p):
                return {'room': sw(W(p)), 'tag': cstr(p + 4) if W(p + 4) else None,
                        'x': struct.unpack('<f', struct.pack('<I', W(p + 8)))[0],
                        'y': struct.unpack('<f', struct.pack('<I', W(p + 12)))[0],
                        'z': struct.unpack('<f', struct.pack('<I', W(p + 16)))[0],
                        'w': sw(W(p + 20))}
            start = rec18(spawn_p)
            exit_ = rec18(spawn_p + 0x18)
            ents = []
            p = spawn_p + 0x30
            while (W(p) & 0xff) != 0xff:
                e = rec18(p)
                e['room'] = W(p) & 0xff
                ents.append(e)
                p += 0x18
            lst.append({'entry_addr': '0x%x' % ent, 'connections': conns, 'player_start': start, 'exit': exit_, 'entities': ents})
        layouts[name] = lst
    t['fixed_layouts'] = layouts
    return t


# --------------------------------------------------------------------------------------- main
def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--root', default=os.path.join(ROOT, 'work', 'assets'))
    ap.add_argument('--out', default=os.path.join(ROOT, 'work', 'map_formats_summary.json'))
    ap.add_argument('--full-cls', action='store_true', help='include CLS vertices/faces (large)')
    args = ap.parse_args()
    root = args.root

    files = {'FDT': [], 'RDT': [], 'CLS': []}
    for base in [os.path.join(root, 'DATA')] + sorted(
            os.path.join(root, 'PACKS', d, 'DATA') for d in os.listdir(os.path.join(root, 'PACKS'))):
        mp = os.path.join(base, 'MAP')
        if not os.path.isdir(mp):
            continue
        for dp, _, fns in os.walk(mp):
            for fn in fns:
                ext = fn.rsplit('.', 1)[-1].upper()
                if ext in files:
                    files[ext].append(os.path.join(dp, fn))
    for k in files:
        files[k].sort()

    def rel(p):
        return os.path.relpath(p, root).replace('\\', '/')

    summary = {'root': rel(root) if False else os.path.relpath(root, ROOT).replace('\\', '/'),
               'counts': {k: len(v) for k, v in files.items()}, 'failures': [],
               'fdt': {}, 'rdt': {}, 'cls': {}, 'stats': {}}
    padded_checked = 0
    twins_verified = 0
    by_rel = {}
    for k, lst in files.items():
        for p in lst:
            r = rel(p)
            by_rel.setdefault(re.sub(r'^PACKS/[^/]+/', '', r), []).append(p)

    parsers = {'FDT': parse_fdt, 'RDT': parse_rdt, 'CLS': parse_cls}
    stats = {'fdt_tags': collections.Counter(), 'rdt_spawn_tags': collections.Counter(),
             'rdt_prefix': collections.Counter(), 'cls_prefix': collections.Counter(),
             'cls_face_types': collections.Counter(), 'cls_face_attrs': collections.Counter(),
             'rdt_cell_attr': collections.Counter(), 'rdt_light_types': collections.Counter(),
             'rdt_emitter_ids': collections.Counter(), 'fdt_map_sets': collections.Counter()}
    for kind, lst in files.items():
        for p in lst:
            b = open(p, 'rb').read()
            size = len(b)
            try:
                if kind == 'CLS':
                    info, end = parse_cls(b, args.full_cls)
                else:
                    info, end = parsers[kind](b)
                # ---- byte accounting
                if size % SECTOR == 0 and end != size:
                    assert end <= size < end + SECTOR, ('padded file end outside last sector', hex(end), size)
                    padded_checked += 1
                    twins = [t for t in by_rel.get(re.sub(r'^PACKS/[^/]+/', '', rel(p)), []) if t != p]
                    for t in twins:
                        tb = open(t, 'rb').read()
                        if tb == b[:len(tb)] and len(tb) % SECTOR:
                            assert len(tb) == end, ('pack twin size differs', t, len(tb), hex(end))
                            twins_verified += 1
                else:
                    assert end == size, ('unaccounted bytes', hex(end), size)
                info['size'] = size
                info['parsed_end'] = end
                info['sector_padded'] = size % SECTOR == 0 and end != size
            except AssertionError as e:
                summary['failures'].append({'file': rel(p), 'error': repr(e.args)})
                continue
            fn = os.path.basename(p)
            if kind == 'FDT':
                m = re.match(r'F(\d{3})\.FDT$', fn)
                info['floor_id'] = int(m.group(1))
                for e in info['entities']:
                    stats['fdt_tags'][e['tag']] += e['count']
                stats['fdt_map_sets'][info['map_set']] += 1
                # room ids must resolve to RDT files of the same map set in the same tree
                tree = os.path.dirname(os.path.dirname(p))  # .../DATA/MAP
                found = {}
                for rid in info['room_ids']:
                    pre = [x for x in ('MO', 'ME', 'MR') if os.path.exists(
                        os.path.join(tree, 'ROOM', '%s%02d_%02d.RDT' % (x, info['map_set'], rid)))]
                    found[rid] = pre
                info['room_files'] = {str(k): v for k, v in found.items()}
            elif kind == 'RDT':
                m = re.match(r'(M[OER])(\d\d)_(\d\d)\.RDT$', fn)
                assert m, fn
                info['prefix'], info['map_set'], info['file_index'] = m.group(1), int(m.group(2)), int(m.group(3))
                if info['room_id'] != info['file_index']:
                    summary['failures'].append({'file': rel(p), 'error': 'room id != filename index'})
                stats['rdt_prefix'][m.group(1)] += 1
                for s in info['spawns']:
                    stats['rdt_spawn_tags'][s['tag']] += 1
                for a in info['cell_attr']:
                    stats['rdt_cell_attr'][a] += 1
                for l in info['lights']:
                    stats['rdt_light_types'][l['type']] += 1
                for e in info['emitters']:
                    stats['rdt_emitter_ids'][e['effect_id']] += 1
            else:
                m = re.match(r'(M[A-Z])(\d\d)_(\d\d)\.CLS$', fn)
                assert m, fn
                info['prefix'], info['map_set'], info['file_index'] = m.group(1), int(m.group(2)), int(m.group(3))
                stats['cls_prefix'][m.group(1)] += 1
                stats['cls_face_types'].update(info['face_types'])
                stats['cls_face_attrs'].update(info['face_attrs'])
            summary[kind.lower()][rel(p)] = info

    summary['padded_files_checked'] = padded_checked
    summary['pack_twins_verified'] = twins_verified
    summary['stats'] = {k: dict(sorted(v.items(), key=lambda kv: (-kv[1], str(kv[0])))) for k, v in stats.items()}
    t = elf_tables()
    if t:
        summary['elf_tables'] = t
    with open(args.out, 'w', encoding='utf-8') as f:
        json.dump(summary, f, indent=1)
    print('parsed', summary['counts'], 'failures', len(summary['failures']),
          'padded', padded_checked, 'twins verified', twins_verified)
    for f_ in summary['failures'][:20]:
        print(' FAIL', f_)
    return 1 if summary['failures'] else 0


if __name__ == '__main__':
    sys.exit(main())
