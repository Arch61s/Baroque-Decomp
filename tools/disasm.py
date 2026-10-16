"""Disassemble a range of SLUS_217.14 with rabbitizer (R5900). Usage: disasm.py START END [--raw]"""
import os
import struct
import sys

if os.environ.get('BAROQUE_TOOLCHAIN_PY'):  # optional extra module path (e.g. a local Python toolchain with rabbitizer)
    sys.path.insert(0, os.environ['BAROQUE_TOOLCHAIN_PY'])
import rabbitizer  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ELF = os.path.join(ROOT, 'work', 'disc', 'SLUS_217.14')
BASE = 0x100000
FILE_OFF = 0x80

_data = None


def data():
    global _data
    if _data is None:
        _data = open(ELF, 'rb').read()
    return _data


def word(addr):
    return struct.unpack_from('<I', data(), addr - BASE + FILE_OFF)[0]


def read_bytes(addr, n):
    o = addr - BASE + FILE_OFF
    return data()[o:o + n]


def cstring(addr):
    b = read_bytes(addr, 256)
    return b.split(b'\0')[0].decode('latin1')


def disasm(start, end):
    rabbitizer.config.regNames_gprAbiNames = rabbitizer.Abi.O32
    out = []
    for a in range(start, end, 4):
        w = word(a)
        insn = rabbitizer.Instruction(w, a, category=rabbitizer.InstrCategory.R5900)
        out.append('%08x  %08x  %s' % (a, w, insn.disassemble()))
    return out


if __name__ == '__main__':
    s, e = int(sys.argv[1], 16), int(sys.argv[2], 16)
    print('\n'.join(disasm(s, e)))
