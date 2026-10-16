"""Demux Baroque PSS movies (MPEG-2 program stream).

Video: stream 0xE0 (mpeg2video 512x448, 29.97 fps). Audio: private stream 1 (0xBD) whose payload starts with
0xFF 0xA0 0x00 0x00 followed by an sce stream header: 'SShd' u32 size, u32 format (0x01 = PCM16LE, 0x10 = ADPCM), u32 rate,
u32 channels, u32 interleave, ... then 'SSbd' u32 size and the interleaved ADPCM body (interleave bytes per
channel block). Output: <name>.m2v (elementary video) and <name>_audio.wav (decoded PCM).

Usage: pss_movie.py <file.pss> [outdir]
"""
import os
import struct
import sys
import wave

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from sound_archive import decode_vag_block  # noqa: E402


def demux(path):
    data = open(path, 'rb').read()
    video = bytearray()
    audio = bytearray()
    i = 0
    n = len(data)
    counts = {}
    while i < n - 4:
        if data[i] != 0 or data[i + 1] != 0 or data[i + 2] != 1:
            i += 1
            continue
        sid = data[i + 3]
        if sid == 0xBA:
            if data[i + 4] & 0xC0 == 0x40:
                i += 14 + (data[i + 13] & 7)
            else:
                i += 12
            continue
        if sid == 0xB9:
            break
        plen = struct.unpack_from('>H', data, i + 4)[0]
        counts[sid] = counts.get(sid, 0) + 1
        if sid in (0xE0, 0xBD):
            hdrlen = data[i + 8]
            payload = data[i + 9 + hdrlen:i + 6 + plen]
            if sid == 0xE0:
                video += payload
            else:
                assert payload[:2] == b'\xff\xa0', payload[:4].hex()
                audio += payload[4:]
        i += 6 + plen
    return bytes(video), bytes(audio), counts


def decode_audio(audio):
    assert audio[:4] == b'SShd', audio[:16].hex()
    hdr_size, fmt, rate, channels, interleave = struct.unpack_from('<IIIII', audio, 4)
    # sce SShd format codes: 0x01 = 16-bit little-endian PCM, 0x10 = Sony ADPCM.
    assert fmt in (0x01, 0x10) and channels in (1, 2), (fmt, channels)
    body_off = 8 + hdr_size
    assert audio[body_off:body_off + 4] == b'SSbd', audio[body_off:body_off + 4]
    body_size = struct.unpack_from('<I', audio, body_off + 4)[0]
    body = audio[body_off + 8:body_off + 8 + body_size]
    hist = [[0.0, 0.0] for _ in range(channels)]
    chans = [[] for _ in range(channels)]
    pos = 0
    while pos + interleave * channels <= len(body):
        for c in range(channels):
            blk = body[pos:pos + interleave]
            pos += interleave
            if fmt == 0x01:
                chans[c].extend(struct.unpack('<%dh' % (len(blk) // 2), blk[:len(blk) // 2 * 2]))
            else:
                for o in range(0, len(blk) - 15, 16):
                    s, flags = decode_vag_block(blk[o:o + 16], hist[c])
                    chans[c].extend(s)
    frames = min(len(ch) for ch in chans)
    pcm = bytearray()
    for k in range(frames):
        for c in range(channels):
            v = int(round(chans[c][k]))
            pcm += struct.pack('<h', max(-32768, min(32767, v)))
    return rate, channels, bytes(pcm), {'hdr_size': hdr_size, 'interleave': interleave, 'body_size': body_size}


def main():
    src = sys.argv[1]
    out = sys.argv[2] if len(sys.argv) > 2 else os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), 'work', 'movies')
    os.makedirs(out, exist_ok=True)
    name = os.path.splitext(os.path.basename(src))[0]
    video, audio, counts = demux(src)
    print('packets:', {hex(k): v for k, v in counts.items()}, 'video bytes', len(video), 'audio bytes', len(audio))
    open(os.path.join(out, name + '.m2v'), 'wb').write(video)
    rate, channels, pcm, info = decode_audio(audio)
    print('audio:', rate, 'Hz', channels, 'ch', info, 'frames', len(pcm) // (2 * channels), 'seconds %.2f' % (len(pcm) / (2 * channels) / rate))
    with wave.open(os.path.join(out, name + '_audio.wav'), 'wb') as w:
        w.setnchannels(channels)
        w.setsampwidth(2)
        w.setframerate(rate)
        w.writeframes(pcm)


if __name__ == '__main__':
    main()
