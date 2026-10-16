"""Minimal stdlib PNG decoder + frame statistics for sweep captures (8-bit RGB/RGBA, non-interlaced)."""
import struct, zlib


def _read(path):
    data = open(path, "rb").read()
    assert data[:8] == b"\x89PNG\r\n\x1a\n", "not a PNG"
    pos = 8; idat = []; w = h = ct = bd = None
    while pos < len(data):
        n, typ = struct.unpack(">I4s", data[pos:pos + 8]); body = data[pos + 8:pos + 8 + n]; pos += 12 + n
        if typ == b"IHDR":
            w, h, bd, ct, _, _, il = struct.unpack(">IIBBBBB", body)
            assert bd == 8 and il == 0 and ct in (2, 6), f"unsupported PNG (bd {bd} ct {ct} il {il})"
        elif typ == b"IDAT":
            idat.append(body)
        elif typ == b"IEND":
            break
    return w, h, (3 if ct == 2 else 4), zlib.decompress(b"".join(idat))


def png_stats(path, step=4):
    """Mean luma, fraction of near-black pixels, dominant colour share, sampled every `step` pixels."""
    w, h, bpp, raw = _read(path)
    stride = w * bpp
    prev = bytearray(stride); pos = 0
    n = 0; lsum = 0; black = 0; hist = {}
    for y in range(h):
        f = raw[pos]; line = bytearray(raw[pos + 1:pos + 1 + stride]); pos += 1 + stride
        if f == 1:
            for i in range(bpp, stride):
                line[i] = (line[i] + line[i - bpp]) & 0xff
        elif f == 2:
            line = bytearray((a + b) & 0xff for a, b in zip(line, prev))
        elif f == 3:
            for i in range(stride):
                left = line[i - bpp] if i >= bpp else 0
                line[i] = (line[i] + ((left + prev[i]) >> 1)) & 0xff
        elif f == 4:
            for i in range(stride):
                a = line[i - bpp] if i >= bpp else 0
                b = prev[i]; c = prev[i - bpp] if i >= bpp else 0
                p = a + b - c; pa = abs(p - a); pb = abs(p - b); pc = abs(p - c)
                pr = a if (pa <= pb and pa <= pc) else (b if pb <= pc else c)
                line[i] = (line[i] + pr) & 0xff
        prev = line
        if y % step:
            continue
        for x in range(0, w, step):
            o = x * bpp; r, g, b = line[o], line[o + 1], line[o + 2]
            l = (299 * r + 587 * g + 114 * b) // 1000
            lsum += l; n += 1
            if l < 8:
                black += 1
            k = (r >> 3, g >> 3, b >> 3); hist[k] = hist.get(k, 0) + 1
    dom = max(hist.items(), key=lambda kv: kv[1]) if hist else ((0, 0, 0), 0)
    return {"w": w, "h": h, "mean_luma": round(lsum / max(n, 1), 1), "black_frac": round(black / max(n, 1), 3),
            "dominant_rgb": [c * 8 for c in dom[0]], "dominant_frac": round(dom[1] / max(n, 1), 3),
            "colours": len(hist)}


if __name__ == "__main__":
    import sys, json
    for p in sys.argv[1:]:
        print(p, json.dumps(png_stats(p)))
