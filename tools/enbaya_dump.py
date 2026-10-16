"""Reference dump for the native Enbaya port (Baroque-AnimCheck --dump compares against this).

    python -I tools/enbaya_dump.py OUT.txt FILE.IGB [FILE.IGB ...]

For every igAnimation (first track's igEnbayaAnimationSource) writes:
  A <file> <anim index> <name> <bones> <frames>
  F <frame> <bone> <8 hex float bits: q x y z w, p x y z>      every stored frame (decode_all)
  S <time_ns> <bone> <hex rot x4 or -> <hex trans x3 or ->     evaluate() at a fixed scrambled time list
The time list exercises forward and backward seeks and wrap-around (see sample_times)."""
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from igb import IGBFile  # noqa: E402
from enbaya import EnbayaSource, decode_all  # noqa: E402

FRAME_NS = 0x1fca055


def hx(v):
    return '%08x' % struct.unpack('<I', struct.pack('<f', v))[0]


def sample_times(duration_ns):
    n = duration_ns // FRAME_NS + 1
    ts = [0, FRAME_NS // 2, 3 * FRAME_NS + 12345, (n // 2) * FRAME_NS + 777777, FRAME_NS, 2 * FRAME_NS // 3,
          (n - 1) * FRAME_NS, duration_ns + 5 * FRAME_NS + 1, 7 * FRAME_NS // 3, 0, n * FRAME_NS * 3 + 999]
    return ts


def main(argv):
    out = open(argv[1], 'w', newline='\n')
    for path in argv[2:]:
        f = IGBFile(path)
        base = os.path.basename(path)
        for ai, a in enumerate(f.find('igAnimation')):
            try:
                src, _ = EnbayaSource.from_object(a)
            except Exception as e:  # noqa: BLE001
                out.write('X %s %d %s\n' % (base, ai, type(e).__name__))
                continue
            h, frames, times, oob = decode_all(src)
            out.write('A %s %d %s %d %d\n' % (base, ai, a.fields.get('_name'), h.bone_count, len(frames)))
            for k, fr in enumerate(frames):
                for b, (q, p) in enumerate(fr):
                    out.write('F %d %d %s\n' % (k, b, ' '.join(hx(v) for v in list(q) + list(p))))
            src2, _ = EnbayaSource.from_object(a)
            nb = h.bone_count
            for t in sample_times(a.fields['_duration']):
                for b in sorted({0, 1, nb // 2, nb - 1}):
                    r, tr = src2.evaluate(t, b)
                    rs = ' '.join(hx(v) for v in r) if r is not None else '- - - -'
                    ts = ' '.join(hx(v) for v in tr) if tr is not None else '- - -'
                    out.write('S %d %d %s %s\n' % (t, b, rs, ts))
    out.close()
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
