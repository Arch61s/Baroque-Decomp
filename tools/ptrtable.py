"""Dump a pointer table in SLUS_217.14 .data, resolving string pointers.
Usage: ptrtable.py START END [--stride N]   (hex addresses; stride in bytes, default 4)
Each word is printed as hex; if it points into the ELF and looks like printable text, the string is shown.
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import disasm  # noqa: E402

LO, HI = 0x100000, 0x100000 + len(disasm.data())


def describe(w):
    if LO <= w < HI:
        try:
            s = disasm.cstring(w)
        except Exception:
            return ''
        if s and all(32 <= ord(c) < 127 or c in '\n\t' for c in s):
            return repr(s)
    return ''


if __name__ == '__main__':
    s, e = int(sys.argv[1], 16), int(sys.argv[2], 16)
    stride = 4
    if '--stride' in sys.argv:
        stride = int(sys.argv[sys.argv.index('--stride') + 1])
    for a in range(s, e, stride):
        ws = [disasm.word(a + i) for i in range(0, stride, 4)]
        print('%08x  ' % a + '  '.join('%08x %s' % (w, describe(w)) for w in ws))
