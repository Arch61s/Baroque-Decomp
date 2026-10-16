"""Unpack DATA.BIN using the DATA.DIR index.

DATA.DIR layout (little-endian), recovered from the disc:
  directory record, 32 bytes: name[16], type u32, subdir_count u32, file_count u32, file_table_offset u32
  file record, 28 bytes:      name[16], type u32, size u32, offset u32 (byte offset into DATA.BIN, 2048-aligned)
Directory records are stored depth-first; each directory's subdirectories follow it immediately.
"""
import hashlib
import json
import os
import struct
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DIR_PATH = os.path.join(ROOT, 'work', 'disc', 'DATA.DIR')
BIN_PATH = os.path.join(ROOT, 'work', 'disc', 'DATA.BIN')
OUT_ROOT = os.path.join(ROOT, 'work', 'assets')


def cstr(b):
    return b.split(b'\0')[0].decode('ascii')


def parse(data):
    """Directory records tile the region before the first file table. Field 2 of a
    directory record is the number of 32-byte records in its subtree, including itself."""
    entries = []
    dirs = []

    def read_dir(index, path):
        o = index * 32
        name = cstr(data[o:o + 16])
        dtype, span, files, table = struct.unpack_from('<IIII', data, o + 16)
        full = path + name
        dirs.append({'path': full, 'type': dtype, 'span': span, 'files': files, 'table': table})
        for i in range(files):
            fo = table + i * 28
            if path == '' and False:
                pass
            fname = cstr(data[fo:fo + 16])
            ftype, size, offset = struct.unpack_from('<III', data, fo + 16)
            entries.append({'path': full + '/' + fname, 'dir_type': dtype, 'type': ftype, 'size': size, 'offset': offset})
        child = index + 1
        end = index + span
        while child < end:
            child = read_dir(child, full + '/')
        assert child == end, (full, child, end)
        return end

    index = 0
    total = 0x9480 // 32
    root_ordinal = 0
    while index < total and data[index * 32] != 0:
        start = len(entries)
        index = read_dir(index, '')
        for e in entries[start:]:
            e['tree'] = root_ordinal
        root_ordinal += 1
    assert index == total - 1 and data[index * 32:index * 32 + 32] == bytes(32), index
    return entries, (index + 1) * 32, dirs


def main():
    data = open(DIR_PATH, 'rb').read()
    entries, consumed, dirs = parse(data)
    print(len(dirs), 'directories; roots:', [d['path'] for d in dirs if '/' not in d['path']])
    print(f'{len(entries)} files; directory records consumed {consumed} bytes (first file table at 0x9480 = {0x9480})')
    assert consumed == 0x9480, consumed
    bin_size = os.path.getsize(BIN_PATH)
    # Tree 0 is the main DATA tree; every other root is a pack file listed directly under DATA/.
    packs = {e['path'].split('/')[1]: e for e in entries if e['tree'] == 0 and e['path'].count('/') == 1}
    prev_end = 0
    for e in sorted((x for x in entries if x['tree'] == 0), key=lambda x: x['offset']):
        assert e['offset'] % 2048 == 0, e
        assert e['offset'] >= prev_end, (e, prev_end)
        assert e['offset'] + e['size'] <= bin_size, e
        prev_end = e['offset'] + e['size']
    print('main tree: offsets ascending and non-overlapping; coverage %.3f%%' % (100.0 * sum(e['size'] for e in entries if e['tree'] == 0) / bin_size))
    nested = 0
    pack_roots = set(e['path'].split('/')[0] for e in entries if e['tree'] != 0)
    for e in entries:
        if e['tree'] == 0:
            e['abs_offset'] = e['offset']
            continue
        root = e['path'].split('/')[0]
        pack = packs[root]
        assert e['offset'] + e['size'] <= pack['size'], (e, pack)
        e['abs_offset'] = pack['offset'] + e['offset']
        e['pack'] = pack['path']
        nested += 1
    print(f'{nested} nested files resolved inside {len(set(e.get("pack") for e in entries if "pack" in e))} packs')
    if '--dry' in sys.argv:
        for e in entries[:60]:
            print(e)
        return
    with open(BIN_PATH, 'rb') as fh:
        for e in entries:
            rel = e['path'] if e['tree'] == 0 else 'PACKS/' + e['path']
            if e['tree'] == 0 and e['path'].count('/') == 1 and e['path'].split('/')[1] in pack_roots:
                rel += '.pack'
            e['extracted'] = rel
            out = os.path.join(OUT_ROOT, *rel.split('/'))
            os.makedirs(os.path.dirname(out), exist_ok=True)
            fh.seek(e['abs_offset'])
            blob = fh.read(e['size'])
            e['sha256'] = hashlib.sha256(blob).hexdigest()
            with open(out, 'wb') as o:
                o.write(blob)
    json.dump(entries, open(os.path.join(ROOT, 'work', 'data_manifest.json'), 'w'), indent=1)
    import collections
    print('file types:', collections.Counter(e['type'] for e in entries))
    print('dir types:', collections.Counter(e['dir_type'] for e in entries))
    print('extensions:', collections.Counter(e['path'].rsplit('.', 1)[-1] if '.' in e['path'].rsplit('/', 1)[-1] else '(none)' for e in entries))


if __name__ == '__main__':
    main()
