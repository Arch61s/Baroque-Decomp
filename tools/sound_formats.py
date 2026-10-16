"""Baroque (PS2, SLUS_217.14) sound formats: BRQSOUND.SFS archive, STER streams, VAG samples,
sce HD/BD bank, and the game's id tables (stdlib only).

Verified against IOPSND.IRX (the game's IOP sound driver) and the EE code; see work/reports/SOUND.md.

SFS layout (IOP numbering, which is what the game uses):
  sectors 0..4   : table. u32 word[0] = 5 (table size in 2048-byte sectors), word[i>=1] = start sector of
                   entry i-1 ... BUT the IOP indexes the raw words directly: entry i spans word[i]..word[i+1].
                   So entry 0 = sectors 5..945 = the BD (raw ADPCM body of the HD bank), entry 1 = the
                   IECS HD (22528 bytes), entries 2..28 = 27 'STER' BGM streams, entries 29..2119 = VAGp
                   samples (157 SE + 1934 voice clips). Last word = end-of-file sector.
  STER entry     : 48-byte header {'STER', u32le perChannelBytes, u32le loopStartFileOffset (-1 = none;
                   relative to entry start, header included, always 0x30 + k*32), u32be perChannelBytes,
                   u32be sampleRate (48000), 12 zero bytes, char name[16]} followed by 2*perChannelBytes of
                   Sony ADPCM, interleaved L/R per 16-byte block.
  VAGp entry     : standard 48-byte big-endian VAG header + ADPCM body.

Usage:
  sound_formats.py index                 -> work/sound/sfs_entries.json (IOP-numbered entry table)
  sound_formats.py ster  N [out.wav]     -> decode STER entry N (IOP index, 2..28) to 48 kHz stereo WAV
  sound_formats.py vag   N [out.wav]     -> decode VAGp entry N to mono WAV
  sound_formats.py bdvag I [out.wav]     -> decode HD/BD VAG #I (0..250) to mono WAV
  sound_formats.py hd    [out.json]      -> dump HD tables + resolved (program,note) chains
  sound_formats.py ids   [out.json]      -> dump SE/BGM/voice id maps from the ELF + IOP driver tables
  sound_formats.py all                   -> index + hd + ids + stream 1 to work/sound/ster_001.wav
"""
import json
import os
import struct
import sys
import wave

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SFS = os.path.join(ROOT, 'work', 'disc', 'SOUND_EN', 'BRQSOUND.SFS')
ELF = os.path.join(ROOT, 'work', 'disc', 'SLUS_217.14')
IOPSND = os.path.join(ROOT, 'work', 'disc', 'MODULES', 'IOPSND.IRX')
OUT = os.path.join(ROOT, 'work', 'sound')
SECTOR = 2048

# ---------------------------------------------------------------- Sony ADPCM (exact /64 predictor table)
VAG_F = [(0, 0), (60, 0), (115, -52), (98, -55), (122, -60)]


def decode_block(block, hist):
    """Decode one 16-byte block. hist = [s1, s2] (ints, previous two samples). Returns (28 ints, flags)."""
    shift = block[0] & 0x0f
    pred = block[0] >> 4
    flags = block[1]
    f0, f1 = VAG_F[pred] if pred < 5 else VAG_F[0]
    s1, s2 = hist
    out = []
    for i in range(14):
        b = block[2 + i]
        for nib in (b & 0x0f, b >> 4):
            v = nib << 12
            if v & 0x8000:
                v -= 0x10000
            # SPU arithmetic: 12-bit sample scaled by shift, plus filtered history (>>6)
            s = (v >> shift) + ((s1 * f0 + s2 * f1 + 32) >> 6)
            s = max(-32768, min(32767, s))
            s2, s1 = s1, s
            out.append(s)
    hist[0], hist[1] = s1, s2
    return out, flags


def decode_mono(body, stop_at_end=True):
    hist = [0, 0]
    pcm = []
    for o in range(0, len(body) - 15, 16):
        samples, flags = decode_block(body[o:o + 16], hist)
        if flags == 7:  # end marker block (silence/mute)
            break
        pcm.extend(samples)
        if stop_at_end and (flags & 1) and flags != 2:
            break
    return pcm


def write_wav(path, channels, rate, pcm_channels):
    n = min(len(c) for c in pcm_channels)
    frames = bytearray()
    if channels == 1:
        frames = struct.pack('<%dh' % n, *pcm_channels[0][:n])
    else:
        inter = [0] * (n * channels)
        for c, ch in enumerate(pcm_channels):
            inter[c::channels] = ch[:n]
        frames = struct.pack('<%dh' % len(inter), *inter)
    os.makedirs(os.path.dirname(os.path.abspath(path)), exist_ok=True)
    with wave.open(path, 'wb') as w:
        w.setnchannels(channels)
        w.setsampwidth(2)
        w.setframerate(rate)
        w.writeframes(frames)
    return n


# ---------------------------------------------------------------- SFS archive
class Sfs:
    def __init__(self, path=SFS):
        self.path = path
        self.f = open(path, 'rb')
        self.size = os.path.getsize(path)
        hdr = self.f.read(SECTOR)
        nsec = struct.unpack_from('<I', hdr, 0)[0]
        self.f.seek(0)
        hdr = self.f.read(nsec * SECTOR)
        words = struct.unpack_from('<%dI' % (len(hdr) // 4), hdr)
        table = [words[0]]
        for w in words[1:]:
            if w == 0:
                break
            table.append(w)
        self.table = table                      # IOP uses table[i] directly (table[0] = table sector count)
        self.count = len(table) - 1            # entry i spans table[i]..table[i+1]

    def span(self, i):
        return self.table[i] * SECTOR, (self.table[i + 1] - self.table[i]) * SECTOR

    def read(self, i, limit=None):
        off, size = self.span(i)
        self.f.seek(off)
        return self.f.read(size if limit is None else min(size, limit))

    def kind(self, i):
        if i == 0:
            return 'BD'
        magic = self.read(i, 4)
        return {b'VAGp': 'VAGp', b'STER': 'STER', b'IECS': 'IECS'}.get(magic, magic.hex())

    def entry_info(self, i):
        off, size = self.span(i)
        head = self.read(i, 64)
        e = {'entry': i, 'offset': off, 'size': size, 'kind': self.kind(i)}
        if e['kind'] == 'VAGp':
            e['version'], e['data_size'], e['rate'] = struct.unpack_from('>IxxxxII', head, 4)
            e['name'] = head[32:48].split(b'\0')[0].decode('latin1')
        elif e['kind'] == 'STER':
            e.update(parse_ster_header(head))
        return e

    def index(self):
        return [self.entry_info(i) for i in range(self.count)]


def parse_ster_header(head):
    per_ch, loop, per_ch_be, rate = struct.unpack_from('<IiI', head, 4) + (struct.unpack_from('>I', head, 16)[0],)
    assert per_ch == struct.unpack('>I', struct.pack('<I', per_ch_be))[0]
    d = {'name': head[32:48].split(b'\0')[0].decode('latin1'), 'rate': rate, 'per_channel_bytes': per_ch,
         'samples_per_channel': per_ch // 16 * 28, 'loop_start_file_offset': loop}
    if loop != -1:
        d['loop_start_sample'] = (loop - 0x30) // 32 * 28  # per-channel sample index
    return d


def decode_ster(data):
    """data = whole STER entry. Returns (header dict, L, R)."""
    hdr = parse_ster_header(data[:48])
    n = hdr['per_channel_bytes']
    body = data[0x30:0x30 + 2 * n]
    hl, hr = [0, 0], [0, 0]
    L, R = [], []
    for o in range(0, len(body) - 31, 32):
        s, fl = decode_block(body[o:o + 16], hl)
        if fl == 7:
            break
        L.extend(s)
        s, fr = decode_block(body[o + 16:o + 32], hr)
        R.extend(s)
    return hdr, L, R


def decode_vag_entry(data):
    rate = struct.unpack_from('>I', data, 16)[0]
    size = struct.unpack_from('>I', data, 12)[0]
    return rate, decode_mono(data[48:48 + size])


# ---------------------------------------------------------------- sce HD (IECS) bank
def parse_hd(hd):
    chunks = {}
    o = 0
    while o + 12 <= len(hd) and hd[o:o + 4] == b'IECS':
        typ = hd[o + 4:o + 8][::-1].decode()
        size = struct.unpack_from('<I', hd, o + 8)[0]
        chunks[typ] = (o, size)
        o += size
    head = struct.unpack_from('<16I', hd, chunks['Head'][0])
    out = {'file_size': head[3], 'body_size': head[4], 'program_chunk': head[5], 'sampleset_chunk': head[6],
           'sample_chunk': head[7], 'vaginfo_chunk': head[8], 'chunks': {k: {'offset': v[0], 'size': v[1]} for k, v in chunks.items()}}

    def table(typ):
        o, s = chunks[typ]
        n = struct.unpack_from('<I', hd, o + 12)[0] + 1
        offs = struct.unpack_from('<%dI' % n, hd, o + 16)
        return o, offs

    o, offs = table('Vagi')
    out['vagi'] = []
    for i, x in enumerate(offs):
        bd_off, rate, attr, res = struct.unpack_from('<IHBB', hd, o + x)
        nxt = struct.unpack_from('<I', hd, o + offs[i + 1])[0] if i + 1 < len(offs) else head[4]
        out['vagi'].append({'index': i, 'bd_offset': bd_off, 'size': nxt - bd_off, 'rate': rate, 'attr': attr})
    o, offs = table('Smpl')
    out['samples'] = []
    for i, x in enumerate(offs):
        b = hd[o + x:o + x + 42]
        out['samples'].append({
            'index': i, 'vag_index': struct.unpack_from('<H', b, 0)[0],
            'vel_range_low': b[2], 'vel_crossfade': b[3], 'vel_range_high': b[4],
            'vel_follow_pitch': b[5], 'vel_follow_pitch_center': b[6], 'vel_follow_pitch_curve': b[7],
            'vel_follow_amp': b[8], 'vel_follow_amp_center': b[9], 'vel_follow_amp_curve': b[10],
            'base_note': b[11], 'detune': b[12], 'panpot': b[13], 'group': b[14], 'priority': b[15],
            'volume': b[16], 'adsr1': struct.unpack_from('<H', b, 18)[0], 'adsr2': struct.unpack_from('<H', b, 20)[0],
            'key_follow_ar': b[22], 'kf_ar_center': b[23], 'key_follow_dr': b[24], 'kf_dr_center': b[25],
            'key_follow_sr': b[26], 'kf_sr_center': b[27], 'key_follow_rr': b[28], 'kf_rr_center': b[29],
            'key_follow_sl': b[30], 'kf_sl_center': b[31],
            'pitch_lfo_delay': struct.unpack_from('<H', b, 32)[0], 'pitch_lfo_fade': struct.unpack_from('<H', b, 34)[0],
            'amp_lfo_delay': struct.unpack_from('<H', b, 36)[0], 'amp_lfo_fade': struct.unpack_from('<H', b, 38)[0],
            'lfo_attr': b[40], 'spu_attr': b[41], 'raw': b.hex()})
    o, offs = table('Sset')
    out['samplesets'] = []
    for i, x in enumerate(offs):
        b = hd[o + x:o + x + 6]
        out['samplesets'].append({'index': i, 'vel_curve': b[0], 'vel_limit_low': b[1], 'vel_limit_high': b[2],
                                  'n_samples': b[3], 'sample_indices': list(struct.unpack_from('<%dH' % b[3], b, 4)) if b[3] else [],
                                  'raw': hd[o + x:o + x + 4 + 2 * b[3]].hex()})
    o, offs = table('Prog')
    out['programs'] = []
    for i, x in enumerate(offs):
        if x == 0xffffffff:
            out['programs'].append(None)
            continue
        b = hd[o + x:o + x + 0x24]
        nsplit, blk = b[4], b[5]
        prog = {'index': i, 'split_block_addr': struct.unpack_from('<I', b, 0)[0], 'n_splits': nsplit, 'split_block_size': blk,
                'volume': b[6], 'panpot': b[7], 'transpose': b[8], 'detune': b[9], 'key_follow_pan': b[10],
                'kf_pan_center': b[11], 'attr': b[12], 'raw_header': b.hex(), 'splits': []}
        for s in range(nsplit):
            sb = hd[o + x + 0x24 + s * blk:o + x + 0x24 + (s + 1) * blk]
            prog['splits'].append({'sampleset': struct.unpack_from('<H', sb, 0)[0], 'range_low': sb[2], 'crossfade': sb[3],
                                   'range_high': sb[4], 'split_number': sb[5], 'bend_range_low': sb[6], 'bend_range_high': sb[7],
                                   'key_follow_pitch': struct.unpack_from('<H', sb, 8)[0], 'key_follow_amp': sb[10], 'kf_amp_center': sb[11],
                                   'key_follow_pan': sb[12], 'kf_pan_center': sb[13], 'key_follow_lfo': sb[14], 'kf_lfo_center': sb[15],
                                   'volume': sb[16], 'panpot': sb[17], 'transpose': sb[18], 'detune': sb[19], 'raw': sb.hex()})
        out['programs'].append(prog)
    return out


def resolve_note(hd, prog, note):
    """(program, note) -> dict chain: split -> sampleset -> sample -> vagi (first matching, like a synth would)."""
    p = hd['programs'][prog]
    for sp in p['splits']:
        if sp['range_low'] <= note <= sp['range_high']:
            ss = hd['samplesets'][sp['sampleset']]
            smp = hd['samples'][ss['sample_indices'][0]]
            vg = hd['vagi'][smp['vag_index']]
            return {'program': prog, 'note': note, 'sampleset': ss['index'], 'sample': smp['index'], 'vag': vg['index'],
                    'bd_offset': vg['bd_offset'], 'bd_size': vg['size'], 'rate': vg['rate'], 'base_note': smp['base_note'],
                    'adsr1': smp['adsr1'], 'adsr2': smp['adsr2'], 'volume': smp['volume'], 'panpot': smp['panpot'],
                    'split_volume': sp['volume'], 'split_panpot': sp['panpot'], 'prog_volume': p['volume'], 'prog_panpot': p['panpot'],
                    'priority': smp['priority']}
    return None


# ---------------------------------------------------------------- ELF / IRX tables
class Elf:
    BASE, FILE_OFF = 0x100000, 0x80

    def __init__(self, path=ELF):
        self.d = open(path, 'rb').read()

    def bytes(self, addr, n):
        o = addr - self.BASE + self.FILE_OFF
        return self.d[o:o + n]

    def u32(self, a):
        return struct.unpack_from('<I', self.d, a - self.BASE + self.FILE_OFF)[0]

    def cstr(self, a):
        return self.bytes(a, 64).split(b'\0')[0].decode('latin1')


SE_TABLE, SE_ROWS = 0x46bcf0, 398          # EE SE descriptor table (0x14 bytes/row), verified in FUN_00210f70/FUN_00211ef0
BGM_DESC, VOICE_DESC = 0x46dd80, 0x46dda0  # descriptors used by FUN_00212aa0 for stream BGM / voice
EFFECT_TABLE = 0x46dc10                    # 6 rows x 0x20: sceSdSetEffectAttr parameters (FUN_00210170 case 0x13)
BANK_TABLE = 0x46bcc0                      # 2 rows x 0x14: {hdEntry, bdEntry, name*, idList*, count}


def parse_row(b):
    flags, x, a, prio, c, d, user = struct.unpack('<IIHHHHI', b)
    return {'flags': flags, 'x': x, 'field8': a, 'priority': prio, 'field12': c, 'default_volume': d & 0xff,
            'mode': d >> 8, 'user': user}


def id_tables(sfs=None, hd=None):
    elf = Elf()
    sfs = sfs or Sfs()
    names = {}
    for i in range(sfs.count):
        k = sfs.kind(i)
        if k in ('VAGp', 'STER'):
            names[i] = sfs.read(i, 48)[32:48].split(b'\0')[0].decode('latin1')
    out = {'bank_table': [], 'se': [], 'bgm': [], 'voice_base_entry': 0xba, 'effects': []}
    for r in range(2):
        b = elf.bytes(BANK_TABLE + r * 0x14, 0x14)
        hdi, bdi, namep, listp, cnt = struct.unpack('<IIIII', b)
        row = {'hd_entry': hdi, 'bd_entry': bdi, 'name': elf.cstr(namep) if namep else None, 'count': cnt}
        if listp:
            row['se_ids'] = list(struct.unpack('<%dh' % cnt, elf.bytes(listp, 2 * cnt)))
        out['bank_table'].append(row)
    for i in range(SE_ROWS):
        row = parse_row(elf.bytes(SE_TABLE + i * 0x14, 0x14))
        row['id'] = i
        f = row['flags']
        if f & 0x20000000:
            row['kind'] = 'stream_vag'
            row['sfs_entry'] = row['x']
            row['file'] = names.get(row['x'])
        elif f & 0x02000000:
            row['kind'] = 'synth'
            row['program'], row['note'] = row['x'] & 0xff, row['x'] >> 8
            if hd:
                row['resolved'] = resolve_note(hd, row['program'], row['note'])
        else:
            row['kind'] = 'none'
        out['se'].append(row)
    for n in range(27):
        e = n + 2
        info = sfs.entry_info(e)
        out['bgm'].append({'bgm_id': n, 'sfs_entry': e, 'file': info['name'], 'loop_start_file_offset': info['loop_start_file_offset'],
                           'loop_start_sample': info.get('loop_start_sample'), 'seconds': info['samples_per_channel'] / info['rate']})
    out['bgm_descriptor'] = parse_row(elf.bytes(BGM_DESC, 0x14))
    out['voice_descriptor'] = parse_row(elf.bytes(VOICE_DESC, 0x14))
    out['voices'] = [{'voice_id': i - 0xba, 'sfs_entry': i, 'file': names[i]} for i in range(0xba, sfs.count) if i in names]
    for r in range(6):
        w = struct.unpack('<8I', elf.bytes(EFFECT_TABLE + r * 0x20, 0x20))
        out['effects'].append({'index': r, 'mode': w[0], 'depth_l': w[1] & 0xffff, 'depth_r': w[1] >> 16, 'delay': w[2], 'feedback': w[3]})
    out['iop'] = iop_tables()
    return out


def iop_tables():
    """Constant tables from IOPSND.IRX .data (file offset = link addr + 0xb0 for this module)."""
    d = open(IOPSND, 'rb').read()
    off = 0xb0

    def words(addr, n):
        return list(struct.unpack_from('<%di' % n, d, addr + off))
    return {'adpcm_coef_x100': words(0x5ed0, 10), 'pan_to_spu_pan_0_99': words(0x5f00, 100),
            'volume_curve_pct_0_100': words(0x6258, 101),
            'stream_spu_buffer': [hex(x) for x in words(0x6230, 5)], 'stream_core1_voice': words(0x6244, 5),
            'rpc_id': 0x12346, 'tick_hz': 240, 'tick_usec': 0x1047}


# ---------------------------------------------------------------- CLI
def main(argv):
    if not argv:
        print(__doc__)
        return
    cmd = argv[0]
    sfs = Sfs()
    os.makedirs(OUT, exist_ok=True)
    if cmd == 'index':
        idx = sfs.index()
        json.dump({'table_sectors': sfs.table[0], 'entries': idx}, open(os.path.join(OUT, 'sfs_entries.json'), 'w'), indent=1)
        import collections
        print(len(idx), 'entries', collections.Counter(e['kind'] for e in idx))
    elif cmd == 'ster':
        n = int(argv[1])
        hdr, L, R = decode_ster(sfs.read(n))
        out = argv[2] if len(argv) > 2 else os.path.join(OUT, 'ster_%03d.wav' % (n - 1))
        frames = write_wav(out, 2, hdr['rate'], [L, R])
        print(hdr, '->', out, frames, 'frames', '%.2fs' % (frames / hdr['rate']))
    elif cmd == 'vag':
        n = int(argv[1])
        data = sfs.read(n)
        rate, pcm = decode_vag_entry(data)
        out = argv[2] if len(argv) > 2 else os.path.join(OUT, 'vag_%04d.wav' % n)
        write_wav(out, 1, rate, [pcm])
        print(data[32:48].split(b'\0')[0].decode('latin1'), rate, 'Hz', len(pcm), 'samples ->', out)
    elif cmd in ('hd', 'bdvag', 'ids', 'all'):
        hd = parse_hd(sfs.read(1))
        if cmd == 'bdvag':
            i = int(argv[1])
            v = hd['vagi'][i]
            bd = sfs.read(0)
            pcm = decode_mono(bd[v['bd_offset']:v['bd_offset'] + v['size']])
            out = argv[2] if len(argv) > 2 else os.path.join(OUT, 'bdvag_%03d.wav' % i)
            write_wav(out, 1, v['rate'], [pcm])
            print(v, len(pcm), 'samples ->', out)
            return
        if cmd in ('hd', 'all'):
            hd['note_map'] = {'%d:%d' % (p['index'], sp['range_low']): resolve_note(hd, p['index'], sp['range_low'])
                              for p in hd['programs'] if p for sp in p['splits']}
            out = argv[1] if (cmd == 'hd' and len(argv) > 1) else os.path.join(OUT, 'hd_bank.json')
            json.dump(hd, open(out, 'w'), indent=1)
            print('HD: %d programs, %d samplesets, %d samples, %d vags -> %s' % (len(hd['programs']), len(hd['samplesets']), len(hd['samples']), len(hd['vagi']), out))
        if cmd in ('ids', 'all'):
            ids = id_tables(sfs, hd)
            out = argv[1] if (cmd == 'ids' and len(argv) > 1) else os.path.join(OUT, 'sound_ids.json')
            json.dump(ids, open(out, 'w'), indent=1)
            print('ids: %d SE rows, %d BGM, %d voices -> %s' % (len(ids['se']), len(ids['bgm']), len(ids['voices']), out))
        if cmd == 'all':
            main(['index'])
            main(['ster', '2', os.path.join(OUT, 'ster_001.wav')])
    else:
        print(__doc__)


if __name__ == '__main__':
    main(sys.argv[1:])
