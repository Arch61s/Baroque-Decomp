"""Build native/platform/baroque.ico from the game's own title logo (standard library only).

Source: DATA/2D_EN/UI/TEX_UI32.PNG (title logo sheet, 256x256, 8-bit palette + tRNS). The "O" of "BAROQUE" carries
the cross emblem; it is cropped, alpha-normalised (PS2 textures use 0x80 = opaque), placed on a rounded tile and
written as a multi-size .ico (16/32/48 as 32-bit DIBs, 256 as PNG).

  python tools/make_icon.py [--assets work/assets] [--out native/platform/baroque.ico] [--preview out.png]
"""
import argparse
import os
import struct
import sys
import zlib

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def png_decode(path):
    data = open(path, 'rb').read()
    assert data[:8] == b'\x89PNG\r\n\x1a\n', path
    pos, idat, plte, trns = 8, b'', None, None
    w = h = bd = ct = il = 0
    while pos < len(data):
        n, typ = struct.unpack('>I4s', data[pos:pos + 8])
        body = data[pos + 8:pos + 8 + n]
        pos += 12 + n
        if typ == b'IHDR':
            w, h, bd, ct, _, _, il = struct.unpack('>IIBBBBB', body)
        elif typ == b'PLTE':
            plte = [tuple(body[i:i + 3]) for i in range(0, len(body), 3)]
        elif typ == b'tRNS':
            trns = body
        elif typ == b'IDAT':
            idat += body
        elif typ == b'IEND':
            break
    assert il == 0, 'interlaced PNG not supported'
    channels = {0: 1, 2: 3, 3: 1, 4: 2, 6: 4}[ct]
    bpp = max(1, channels * bd // 8)
    stride = (w * channels * bd + 7) // 8
    raw = zlib.decompress(idat)
    rows, prev, o = [], bytearray(stride), 0
    for _ in range(h):
        f = raw[o]
        line = bytearray(raw[o + 1:o + 1 + stride])
        o += 1 + stride
        for i in range(stride):
            a = line[i - bpp] if i >= bpp else 0
            b = prev[i]
            c = prev[i - bpp] if i >= bpp else 0
            if f == 1:
                line[i] = (line[i] + a) & 255
            elif f == 2:
                line[i] = (line[i] + b) & 255
            elif f == 3:
                line[i] = (line[i] + ((a + b) >> 1)) & 255
            elif f == 4:
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                line[i] = (line[i] + (a if pa <= pb and pa <= pc else b if pb <= pc else c)) & 255
        rows.append(line)
        prev = line
    px = []
    for line in rows:
        if bd < 8:
            vals = []
            for byte in line:
                for k in range(8 // bd):
                    vals.append((byte >> (8 - bd * (k + 1))) & ((1 << bd) - 1))
            vals = vals[:w * channels]
        else:
            assert bd == 8, '16-bit PNG not supported'
            vals = list(line)
        for x in range(w):
            if ct == 3:
                i = vals[x]
                r, g, b = plte[i]
                a = trns[i] if trns is not None and i < len(trns) else 255
            elif ct == 6:
                r, g, b, a = vals[x * 4:x * 4 + 4]
            elif ct == 2:
                r, g, b = vals[x * 3:x * 3 + 3]
                a = 255
            elif ct == 4:
                r = g = b = vals[x * 2]
                a = vals[x * 2 + 1]
            else:
                r = g = b = vals[x]
                a = 255
            px.append([r, g, b, a])
    return w, h, px


def png_encode(w, h, px):
    raw = b''.join(b'\0' + bytes(c for p in px[y * w:(y + 1) * w] for c in p) for y in range(h))

    def chunk(t, b):
        return struct.pack('>I', len(b)) + t + b + struct.pack('>I', zlib.crc32(t + b) & 0xffffffff)
    return (b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 6, 0, 0, 0))
            + chunk(b'IDAT', zlib.compress(raw, 9)) + chunk(b'IEND', b''))


def resample(w, h, px, nw, nh):
    """Area-average resample with alpha weighting (box filter over the covered source area)."""
    out = []
    for y in range(nh):
        y0, y1 = y * h / nh, (y + 1) * h / nh
        for x in range(nw):
            x0, x1 = x * w / nw, (x + 1) * w / nw
            acc = [0.0] * 4
            tot = 0.0
            yy = int(y0)
            while yy < y1 and yy < h:
                wy = min(y1, yy + 1) - max(y0, yy)
                xx = int(x0)
                while xx < x1 and xx < w:
                    wx = min(x1, xx + 1) - max(x0, xx)
                    wgt = wx * wy
                    r, g, b, a = px[yy * w + xx]
                    acc[0] += r * a * wgt
                    acc[1] += g * a * wgt
                    acc[2] += b * a * wgt
                    acc[3] += a * wgt
                    tot += wgt
                    xx += 1
                yy += 1
            if acc[3] > 0:
                out.append([int(round(acc[0] / acc[3])), int(round(acc[1] / acc[3])),
                            int(round(acc[2] / acc[3])), int(round(acc[3] / tot))])
            else:
                out.append([0, 0, 0, 0])
    return out


def over(dst, src):
    """src over dst, both straight-alpha RGBA lists of the same size."""
    out = []
    for d, s in zip(dst, src):
        sa, da = s[3] / 255.0, d[3] / 255.0
        oa = sa + da * (1 - sa)
        if oa <= 0:
            out.append([0, 0, 0, 0])
            continue
        out.append([int(round((s[i] * sa + d[i] * da * (1 - sa)) / oa)) for i in range(3)] + [int(round(oa * 255))])
    return out


def tile(n, radius, fill, edge):
    """Rounded square n x n (4x4 supersampled corners) with a thin darker rim."""
    rim = max(1.0, n / 32.0)
    px = []
    for y in range(n):
        for x in range(n):
            cov = 0
            edge_cov = 0
            for sy in range(4):
                for sx in range(4):
                    fx, fy = x + (sx + 0.5) / 4, y + (sy + 0.5) / 4
                    dx = max(radius - fx, 0, fx - (n - radius))
                    dy = max(radius - fy, 0, fy - (n - radius))
                    d = (dx * dx + dy * dy) ** 0.5
                    if d <= radius:
                        cov += 1
                        if d > radius - rim if (dx > 0 and dy > 0) else min(fx, fy, n - fx, n - fy) < rim:
                            edge_cov += 1
            if cov == 0:
                px.append([0, 0, 0, 0])
                continue
            t = edge_cov / cov
            c = [int(round(fill[i] * (1 - t) + edge[i] * t)) for i in range(3)]
            px.append(c + [int(round(255 * cov / 16))])
    return px


def emblem(w, px, cx, cy, rx, ry):
    """Cut the cross "O" out of the logo sheet. The sheet ends 4-5 px into the letter's right stroke, so columns past
    the sheet edge are the mirrored left stroke (the letter outline is left/right symmetric; the cross is not, it is
    taken as is); an elliptic mask drops the neighbouring "R" and the underline."""
    out = []
    for y in range(cy - ry, cy + ry + 1):
        for x in range(cx - rx, cx + rx + 1):
            sx = x if x < w else 2 * cx - x
            r, g, b, a = px[y * w + sx]
            d = ((x - cx) / (rx + 0.5)) ** 2 + ((y - cy) / (ry + 0.5)) ** 2
            k = max(0.0, min(1.0, (1.0 - d) * 12.0))
            out.append([r, g, b, int(round(a * k))])
    return out, 2 * rx + 1, 2 * ry + 1


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--assets', default=os.path.join(ROOT, 'work', 'assets'))
    ap.add_argument('--out', default=os.path.join(ROOT, 'native', 'platform', 'baroque.ico'))
    ap.add_argument('--preview')
    ap.add_argument('--emblem', default='226,50,35,45',
                    help='centre x,y and radii rx,ry of the cross "O" in TEX_UI32 (its right edge is cut by the sheet)')
    ap.add_argument('--tile', default='light', choices=('dark', 'light'))
    args = ap.parse_args()
    w, h, px = png_decode(os.path.join(args.assets, 'DATA', '2D_EN', 'UI', 'TEX_UI32.PNG'))
    if max(p[3] for p in px) <= 0x80:   # PS2 alpha: 0x80 = opaque
        for p in px:
            p[3] = min(255, p[3] * 2)
    crop, cw, ch = emblem(w, px, *(int(v) for v in args.emblem.split(',')))
    images = []
    for n in (256, 48, 32, 16):
        base = (tile(n, n * 0.18, (34, 30, 38), (90, 84, 96)) if args.tile == 'dark'
                else tile(n, n * 0.18, (206, 198, 184), (110, 100, 88)))
        pad = max(1, round(n * 0.07))
        avail = n - 2 * pad
        sh = avail
        sw = max(1, round(cw * sh / ch))
        if sw > avail:
            sw = avail
            sh = max(1, round(ch * sw / cw))
        glyph = resample(cw, ch, crop, sw, sh)
        layer = [[0, 0, 0, 0] for _ in range(n * n)]
        ox, oy = (n - sw) // 2, (n - sh) // 2
        for y in range(sh):
            for x in range(sw):
                layer[(oy + y) * n + ox + x] = glyph[y * sw + x]
        images.append((n, over(base, layer)))
    if args.preview:
        for n, img in images:
            root, ext = os.path.splitext(args.preview)
            open(f'{root}_{n}{ext}', 'wb').write(png_encode(n, n, img))
    entries, blobs = [], []
    for n, img in images:
        if n >= 256:
            blob = png_encode(n, n, img)
        else:
            hdr = struct.pack('<IiiHHIIiiII', 40, n, n * 2, 1, 32, 0, n * n * 4, 0, 0, 0, 0)
            xor = b''.join(bytes((p[2], p[1], p[0], p[3])) for y in range(n - 1, -1, -1)
                           for p in img[y * n:(y + 1) * n])
            mask_stride = ((n + 31) // 32) * 4
            mask = bytearray()
            for y in range(n - 1, -1, -1):
                row = bytearray(mask_stride)
                for x in range(n):
                    if img[y * n + x][3] == 0:
                        row[x // 8] |= 0x80 >> (x % 8)
                mask += row
            blob = hdr + xor + bytes(mask)
        blobs.append(blob)
        entries.append(n)
    off = 6 + 16 * len(entries)
    out = struct.pack('<HHH', 0, 1, len(entries))
    for n, blob in zip(entries, blobs):
        out += struct.pack('<BBBBHHII', n % 256, n % 256, 0, 0, 1, 32, len(blob), off)
        off += len(blob)
    out += b''.join(blobs)
    os.makedirs(os.path.dirname(args.out), exist_ok=True)
    open(args.out, 'wb').write(out)
    print('wrote', args.out, len(out), 'bytes, sizes', entries)


if __name__ == '__main__':
    sys.exit(main())
