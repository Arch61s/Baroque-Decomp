"""Minimal PS2 VU (micro mode) disassembler for the VU1 microcode embedded in SLUS_217.14.
Usage: vudis.py START END   (ELF virtual addresses, 8 bytes per instruction pair: lower word, upper word)
Encoding per the EE/VU user's manual (upper FMAC ops, lower integer/branch/load ops)."""
import os
import struct
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ELF = os.path.join(ROOT, 'work', 'disc', 'SLUS_217.14')
_data = None


def data():
    global _data
    if _data is None:
        _data = open(ELF, 'rb').read()
    return _data


def word(a):
    return struct.unpack_from('<I', data(), a - 0x100000 + 0x80)[0]


BC = 'xyzw'


def dest(w):
    d = (w >> 21) & 0xf
    return ''.join(c for i, c in enumerate('xyzw') if d & (8 >> i))


UP = {}
for i, n in enumerate(['ADD', 'SUB', 'MADD', 'MSUB', 'MAX', 'MINI', 'MUL']):
    for b in range(4):
        UP[i * 4 + b] = n + BC[b]
UP.update({0x1c: 'MULq', 0x1d: 'MAXi', 0x1e: 'MULi', 0x1f: 'MINIi', 0x20: 'ADDq', 0x21: 'MADDq', 0x22: 'ADDi',
           0x23: 'MADDi', 0x24: 'SUBq', 0x25: 'MSUBq', 0x26: 'SUBi', 0x27: 'MSUBi', 0x28: 'ADD', 0x29: 'MADD',
           0x2a: 'MUL', 0x2b: 'MAX', 0x2c: 'SUB', 0x2d: 'MSUB', 0x2e: 'OPMSUB', 0x2f: 'MINI'})
UPS = {}
for i, n in enumerate(['ADDA', 'SUBA', 'MADDA', 'MSUBA']):
    for b in range(4):
        UPS[i * 4 + b] = n + BC[b]
for i, n in enumerate(['ITOF0', 'ITOF4', 'ITOF12', 'ITOF15', 'FTOI0', 'FTOI4', 'FTOI12', 'FTOI15']):
    UPS[0x10 + i] = n
for b in range(4):
    UPS[0x18 + b] = 'MULA' + BC[b]
UPS.update({0x1c: 'MULAq', 0x1d: 'ABS', 0x1e: 'MULAi', 0x1f: 'CLIPw', 0x20: 'ADDAq', 0x21: 'MADDAq', 0x22: 'ADDAi',
            0x23: 'MADDAi', 0x24: 'SUBAq', 0x25: 'MSUBAq', 0x26: 'SUBAi', 0x27: 'MSUBAi', 0x28: 'ADDA',
            0x29: 'MADDA', 0x2a: 'MULA', 0x2c: 'SUBA', 0x2d: 'MSUBA', 0x2e: 'OPMULA', 0x2f: 'NOP'})


def upper(w):
    flags = ''.join(f for b, f in ((31, 'I'), (30, 'E'), (29, 'M'), (28, 'D'), (27, 'T')) if w >> b & 1)
    op = w & 0x3f
    ft, fs, fd = (w >> 16) & 31, (w >> 11) & 31, (w >> 6) & 31
    d = dest(w)
    if op >= 0x3c:
        sop = ((w >> 6) & 31) << 2 | (w & 3)
        n = UPS.get(sop, 'upS?%02x' % sop)
        if n == 'NOP':
            s = 'nop'
        elif n == 'CLIPw':
            s = 'clipw.xyz vf%d, vf%dw' % (fs, ft)
        elif n.startswith(('ITOF', 'FTOI', 'ABS')):
            s = '%s.%s vf%d, vf%d' % (n.lower(), d, ft, fs)
        elif n[-1] in 'xyzw' and n[:-1] in ('ADDA', 'SUBA', 'MADDA', 'MSUBA', 'MULA'):
            s = '%s.%s ACC, vf%d, vf%d%s' % (n.lower(), d, fs, ft, n[-1])
        elif n[-1] in 'qi':
            s = '%s.%s ACC, vf%d, %s' % (n.lower(), d, fs, n[-1].upper())
        else:
            s = '%s.%s ACC, vf%d, vf%d' % (n.lower(), d, fs, ft)
    else:
        n = UP.get(op, 'up?%02x' % op)
        if n[-1] in 'xyzw' and len(n) > 1 and n[:-1] in ('ADD', 'SUB', 'MADD', 'MSUB', 'MAX', 'MINI', 'MUL'):
            s = '%s.%s vf%d, vf%d, vf%d%s' % (n.lower(), d, fd, fs, ft, n[-1])
        elif n[-1] in 'qi' and n not in ('MINI',):
            s = '%s.%s vf%d, vf%d, %s' % (n.lower(), d, fd, fs, n[-1].upper())
        else:
            s = '%s.%s vf%d, vf%d, vf%d' % (n.lower(), d, fd, fs, ft)
    return (('[' + flags + '] ') if flags else '') + s


LS = {0x30: 'MOVE', 0x31: 'MR32', 0x34: 'LQI', 0x35: 'SQI', 0x36: 'LQD', 0x37: 'SQD', 0x38: 'DIV', 0x39: 'SQRT',
      0x3a: 'RSQRT', 0x3b: 'WAITQ', 0x3c: 'MTIR', 0x3d: 'MFIR', 0x3e: 'ILWR', 0x3f: 'ISWR', 0x40: 'RNEXT',
      0x41: 'RGET', 0x42: 'RINIT', 0x43: 'RXOR', 0x64: 'MFP', 0x68: 'XTOP', 0x69: 'XITOP', 0x6c: 'XGKICK',
      0x70: 'ESADD', 0x71: 'ERSADD', 0x72: 'ELENG', 0x73: 'ERLENG', 0x74: 'EATANxy', 0x75: 'EATANxz', 0x76: 'ESUM',
      0x78: 'ESQRT', 0x79: 'ERSQRT', 0x7a: 'ERCPR', 0x7b: 'WAITP', 0x7c: 'ESIN', 0x7d: 'EATAN', 0x7e: 'EEXP'}
LO = {0x00: 'LQ', 0x01: 'SQ', 0x04: 'ILW', 0x05: 'ISW', 0x08: 'IADDIU', 0x09: 'ISUBIU', 0x10: 'FCEQ', 0x11: 'FCSET',
      0x12: 'FCAND', 0x13: 'FCOR', 0x14: 'FSEQ', 0x15: 'FSSET', 0x16: 'FSAND', 0x17: 'FSOR', 0x18: 'FMEQ',
      0x1a: 'FMAND', 0x1b: 'FMOR', 0x1c: 'FCGET', 0x20: 'B', 0x21: 'BAL', 0x24: 'JR', 0x25: 'JALR', 0x28: 'IBEQ',
      0x29: 'IBNE', 0x2c: 'IBLTZ', 0x2d: 'IBGTZ', 0x2e: 'IBLEZ', 0x2f: 'IBGEZ'}


def s11(v):
    v &= 0x7ff
    return v - 0x800 if v & 0x400 else v


def lower(w, pc):
    top = w >> 25
    ft, fs, fd = (w >> 16) & 31, (w >> 11) & 31, (w >> 6) & 31
    d = dest(w)
    if top == 0x40:
        op = w & 0x3f
        if op == 0x30: return 'iadd vi%d, vi%d, vi%d' % (fd, fs, ft)
        if op == 0x31: return 'isub vi%d, vi%d, vi%d' % (fd, fs, ft)
        if op == 0x32:
            imm = (w >> 6) & 31
            imm = imm - 32 if imm & 16 else imm
            return 'iaddi vi%d, vi%d, %d' % (ft, fs, imm)
        if op == 0x34: return 'iand vi%d, vi%d, vi%d' % (fd, fs, ft)
        if op == 0x35: return 'ior vi%d, vi%d, vi%d' % (fd, fs, ft)
        if op >= 0x3c:
            sop = ((w >> 6) & 31) << 2 | (w & 3)
            n = LS.get(sop, 'loS?%02x' % sop)
            fsf, ftf = BC[(w >> 21) & 3], BC[(w >> 23) & 3]
            if n == 'MOVE' or n == 'MR32': return '%s.%s vf%d, vf%d' % (n.lower(), d, ft, fs)
            if n in ('LQI', 'LQD'): return '%s.%s vf%d, (vi%d%s)' % (n.lower(), d, ft, fs, '++' if n == 'LQI' else '--')
            if n in ('SQI', 'SQD'): return '%s.%s vf%d, (vi%d%s)' % (n.lower(), d, fs, ft, '++' if n == 'SQI' else '--')
            if n in ('DIV', 'RSQRT'): return '%s Q, vf%d%s, vf%d%s' % (n.lower(), fs, fsf, ft, ftf)
            if n == 'SQRT': return 'sqrt Q, vf%d%s' % (ft, ftf)
            if n == 'MTIR': return 'mtir vi%d, vf%d%s' % (ft, fs, fsf)
            if n == 'MFIR': return 'mfir.%s vf%d, vi%d' % (d, ft, fs)
            if n in ('ILWR', 'ISWR'): return '%s.%s vi%d, (vi%d)' % (n.lower(), d, ft, fs)
            if n == 'MFP': return 'mfp.%s vf%d, P' % (d, ft)
            if n in ('XTOP', 'XITOP'): return '%s vi%d' % (n.lower(), ft)
            if n == 'XGKICK': return 'xgkick vi%d' % fs
            if n in ('ESADD', 'ERSADD', 'ELENG', 'ERLENG', 'ESUM'): return '%s P, vf%d' % (n.lower(), fs)
            if n in ('ESQRT', 'ERSQRT', 'ERCPR', 'ESIN', 'EATAN', 'EEXP'): return '%s P, vf%d%s' % (n.lower(), fs, fsf)
            return n.lower()
        return 'lo3?%02x' % op
    n = LO.get(top, 'lo?%02x' % top)
    imm = s11(w)
    if n in ('LQ',): return 'lq.%s vf%d, %d(vi%d)' % (d, ft, imm, fs)
    if n in ('SQ',): return 'sq.%s vf%d, %d(vi%d)' % (d, fs, imm, ft)
    if n in ('ILW', 'ISW'): return '%s.%s vi%d, %d(vi%d)' % (n.lower(), d, ft, imm, fs)
    if n in ('IADDIU', 'ISUBIU'):
        im15 = ((w >> 21) & 0xf) << 11 | (w & 0x7ff)
        return '%s vi%d, vi%d, 0x%x' % (n.lower(), ft, fs, im15)
    if n in ('B', 'BAL'):
        return '%s vi%d, L%04x' % (n.lower(), ft, (pc + 8 + imm * 8) & 0x3fff)
    if n in ('IBEQ', 'IBNE'): return '%s vi%d, vi%d, L%04x' % (n.lower(), ft, fs, (pc + 8 + imm * 8) & 0x3fff)
    if n.startswith('IB'): return '%s vi%d, L%04x' % (n.lower(), fs, (pc + 8 + imm * 8) & 0x3fff)
    if n in ('JR', 'JALR'): return '%s vi%d, vi%d' % (n.lower(), ft, fs)
    if n.startswith(('FC', 'FS', 'FM')): return '%s vi%d, vi%d, 0x%x' % (n.lower(), ft, fs, w & 0xffffff)
    return n.lower() + ' %08x' % w


def disasm(start, end, base=None):
    out = []
    base = start if base is None else base
    for a in range(start, end, 8):
        lo, up = word(a), word(a + 4)
        pc = a - base
        if up >> 31:
            l = 'loi %.6g (%08x)' % (struct.unpack('<f', struct.pack('<I', lo))[0], lo)
        else:
            l = lower(lo, pc)
        out.append('%06x L%04x  %-40s %s' % (a, pc & 0x3fff, upper(up), l))
    return out


if __name__ == '__main__':
    s, e = int(sys.argv[1], 16), int(sys.argv[2], 16)
    b = int(sys.argv[3], 16) if len(sys.argv) > 3 else None
    print('\n'.join(disasm(s, e, b)))
