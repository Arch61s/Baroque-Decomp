"""Generate native/game/dungeon_tables.cpp from the SLUS_217.14 ELF (scenario layouts + small generator tables).
Usage: python -I tools/gen/gen_tables.py native/game/dungeon_tables.cpp"""
import os, sys, struct
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
import disasm
W = disasm.word
def sw(v): return v - (1 << 32) if v & 0x80000000 else v
def f32(a): return struct.unpack('<f', struct.pack('<I', W(a)))[0]
def ff(v):
    s = repr(float(v))
    if 'e' in s or 'inf' in s or 'nan' in s: raise SystemExit(s)
    return s + 'f'

# --- static-init stub 0x4c2840..0x4c3340 (static ctor region) fills the tag words of the layout tables from
#     string literals at 0x44a610..; emulate its straight-line lui/addiu/ori/or/sll/lb/sw sequence.
_stub = {}
def _emulate():
    regs = {0: 0}
    def s16(v): return v - 0x10000 if v & 0x8000 else v
    def g(r): return regs.get(r, 0)
    def st(r, v):
        if r: regs[r] = v & 0xffffffff
    for a in range(0x4c2840, 0x4c3340, 4):
        w = W(a); op = w >> 26; rs = (w >> 21) & 31; rt = (w >> 16) & 31; rdr = (w >> 11) & 31; imm = w & 0xffff; sa = (w >> 6) & 31; fn = w & 0x3f
        if op == 0x0f: st(rt, imm << 16)
        elif op in (0x09, 0x19): st(rt, g(rs) + s16(imm))
        elif op == 0x0d: st(rt, g(rs) | imm)
        elif op == 0x20:
            v = disasm.read_bytes((g(rs) + s16(imm)) & 0xffffffff, 1)[0]; st(rt, v - 256 if v & 0x80 else v)
        elif op == 0x24: st(rt, disasm.read_bytes((g(rs) + s16(imm)) & 0xffffffff, 1)[0])
        elif op == 0x23: st(rt, W((g(rs) + s16(imm)) & 0xffffffff))
        elif op == 0x2b: _stub[(g(rs) + s16(imm)) & 0xffffffff] = g(rt)
        elif op == 0:
            if fn == 0x25: st(rdr, g(rs) | g(rt))
            elif fn == 0x00: st(rdr, g(rt) << sa)
            elif fn in (0x2d, 0x21): st(rdr, g(rs) + g(rt))
_emulate()
def WT(a):
    return _stub.get(a, W(a))
def tagstr(a):
    b = struct.pack('<I', WT(a))
    return ''.join(chr(c) if 32 <= c < 127 else '?'  for c in b)
out = []
out.append('// Generated from SLUS_217.14 .data by tools/gen/gen_tables.py (tools/disasm.py word reads).')
out.append('// Entity tag words are zero in the ELF image and are filled by the static-init stub at 0x4c2a40..0x4c3340')
out.append('// (static ctor region, function 0x4c2840) from string literals at 0x44a610..; that stub was emulated to recover them.')
out.append('// Scenario floor layouts: pointer tables 0x44a1b0 (ids 301-307), 0x44a1d0 (311-317), 0x44a1f0 (321-327),')
out.append('// each entry {conn_t* connections; spawn_t* spawns} (see work/reports/MAP_FORMATS.md section 8.4).')
out.append('#include "game/dungeon_tables.hpp"')
out.append('namespace baroque::game::tables {')
names = []
for tname, base in (('A', 0x44a1b0), ('B', 0x44a1d0), ('C', 0x44a1f0)):
    for i in range(7):
        ent = W(base + 4 * i)
        conn_p, spawn_p = W(ent), W(ent + 4)
        n = '%s%d' % (tname, i + 1)
        conns = []
        p = conn_p
        while True:
            w = W(p)
            a, ad, bb, bd = (w >> s & 0xff for s in (0, 8, 16, 24))
            conns.append('{0x%02x,0x%02x,0x%02x,0x%02x}' % (a, ad, bb, bd))
            if a == 0xff: break
            p += 4
        out.append('static const ScenarioConn kConn_%s[] = { // 0x%x' % (n, conn_p))
        out.append('    ' + ', '.join(conns) + ' };')
        def rec(p):
            return '{%d, 0x%08x, %s, %s, %s, %d}' % (sw(W(p)), WT(p + 4), ff(f32(p + 8)), ff(f32(p + 12)), ff(f32(p + 16)), sw(W(p + 20)))
        ents = []
        p = spawn_p + 0x30
        while (W(p) & 0xff) != 0xff:
            ents.append('{0x%02x, 0x%08x /*%s*/, %s, %s, %s, 0x%x}' % (W(p) & 0xff, WT(p + 4), tagstr(p + 4), ff(f32(p + 8)), ff(f32(p + 12)), ff(f32(p + 16)), W(p + 20)))
            p += 0x18
        out.append('static const ScenarioEntity kEnt_%s[] = { // 0x%x' % (n, spawn_p + 0x30))
        for e in ents: out.append('    ' + e + ',')
        out.append('    {0xff, 0, 0.0f, 0.0f, 0.0f, 0} };')
        out.append('static const ScenarioLayout kLayout_%s = { 0x%x, kConn_%s, %s, %s, kEnt_%s, %d };' % (n, ent, n, rec(spawn_p), rec(spawn_p + 0x18), n, len(ents)))
        names.append((tname, n))
for tname, base in (('A', 0x44a1b0), ('B', 0x44a1d0), ('C', 0x44a1f0)):
    out.append('const ScenarioLayout* const kScenarioLayouts%s[7] = { // 0x%x' % (tname, base))
    out.append('    ' + ', '.join('&kLayout_%s%d' % (tname, i + 1) for i in range(7)) + ' };')
out.append('}')
FOOTER = open(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'tables_footer.cpp'), encoding='utf-8').read()
open(sys.argv[1], 'w', encoding='utf-8').write(chr(10).join(out) + chr(10) + FOOTER)
print('ok', len(out))
