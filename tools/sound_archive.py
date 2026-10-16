"""Index and decode BRQSOUND.SFS.

Layout recovered from the disc: a 2048-byte header holding a little-endian u32 count-ish word
followed by ascending u32 sector offsets (2048-byte units) for each entry, zero-terminated.
Entries are either VAGp samples (Sony VAG ADPCM, big-endian header), STER streams (stereo
ADPCM music streams) or sce HD/BD bank data (IECS* chunks).
"""
import json
import os
import struct
import sys
import wave

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SFS = os.path.join(ROOT, 'work', 'disc', 'SOUND_EN', 'BRQSOUND.SFS')
OUT = os.path.join(ROOT, 'work', 'sound')

# Exact Sony VAG ADPCM predictor table (fixed point /64).
VAG_F = [(0.0, 0.0), (60.0 / 64.0, 0.0), (115.0 / 64.0, -52.0 / 64.0), (98.0 / 64.0, -55.0 / 64.0), (122.0 / 64.0, -60.0 / 64.0)]


def decode_vag_block(block, hist):
    """Decode one 16-byte VAG block. hist = [s1, s2] previous samples (floats). Returns (samples, flags)."""
    shift = block[0] & 0x0f
    pred = (block[0] >> 4) & 0x0f
    flags = block[1]
    f0, f1 = VAG_F[pred if pred < 5 else 0]
    out = []
    s1, s2 = hist
    for i in range(14):
        byte = block[2 + i]
        for nib in (byte & 0x0f, byte >> 4):
            v = nib << 12
            if v & 0x8000:
                v -= 0x10000
            sample = (v >> shift) + s1 * f0 + s2 * f1
            s2, s1 = s1, sample
            out.append(sample)
    hist[0], hist[1] = s1, s2
    return out, flags


def decode_vag_body(body, loop=False):
    hist = [0.0, 0.0]
    pcm = []
    for o in range(0, len(body) - 15, 16):
        samples, flags = decode_vag_block(body[o:o + 16], hist)
        if flags == 7:
            break
        pcm.extend(samples)
        if flags == 1 and not loop:
            break
    return [max(-32768, min(32767, int(round(s)))) for s in pcm]


def read_table():
    with open(SFS, 'rb') as f:
        hdr = f.read(2048)
    words = struct.unpack_from('<512I', hdr, 0)
    table = []
    for w in words[1:]:
        if w == 0 and table:
            break
        table.append(w)
    return words[0], table


def main():
    size = os.path.getsize(SFS)
    first, table = read_table()
    entries = []
    with open(SFS, 'rb') as f:
        for i, sec in enumerate(table):
            start = sec * 2048
            end = (table[i + 1] * 2048) if i + 1 < len(table) else size
            f.seek(start)
            head = f.read(64)
            kind = head[:4].decode('latin1')
            e = {'index': i, 'offset': start, 'size': end - start, 'kind': kind}
            if kind == 'VAGp':
                e['version'], e['reserved'], e['data_size'], e['rate'] = struct.unpack_from('>IIII', head, 4)
                e['name'] = head[32:48].split(b'\0')[0].decode('latin1')
            elif kind == 'STER':
                e['w1'], e['w2'], e['w3'], e['rate_be'] = struct.unpack_from('<III', head, 4) + (struct.unpack_from('>I', head, 16)[0],)
            entries.append(e)
    os.makedirs(OUT, exist_ok=True)
    json.dump({'first_word': first, 'entries': entries}, open(os.path.join(OUT, 'sfs_index.json'), 'w'), indent=1)
    import collections
    print(len(table), 'entries;', collections.Counter(e['kind'] for e in entries))
    rates = collections.Counter(e.get('rate') for e in entries if e['kind'] == 'VAGp')
    print('VAG rates:', rates)
    for e in entries[:6]:
        print(e)
    if '--wav' in sys.argv:
        with open(SFS, 'rb') as f:
            for e in entries:
                if e['kind'] != 'VAGp':
                    continue
                f.seek(e['offset'] + 48)
                body = f.read(e['data_size'])
                pcm = decode_vag_body(body)
                with wave.open(os.path.join(OUT, f"vag_{e['index']:03d}_{e['name'] or 'x'}.wav"), 'wb') as w:
                    w.setnchannels(1)
                    w.setsampwidth(2)
                    w.setframerate(e['rate'])
                    w.writeframes(struct.pack('<%dh' % len(pcm), *pcm))
        print('wrote VAG wavs to', OUT)


if __name__ == '__main__':
    main()
