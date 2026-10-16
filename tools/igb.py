#!/usr/bin/env python3
"""Intrinsic Alchemy .IGB parser for Baroque (PS2, SLUS_217.14).

Layout recovered from igIGBFile::readFile (FUN_002fb270) and its helpers; see
work/reports/IGB_FORMAT.md for the evidence.  Python 3.12, stdlib only.

Usage:
  python -I tools/igb.py FILE.IGB [--dump] [--png OUTDIR] [--obj OUT.obj] [--json OUT.json]
  python -I tools/igb.py --all work/assets [--classes work/reports/igb_classes.json]
"""
import json
import os
import struct
import sys
import zlib

MAGIC = 0xFADA

_HERE = os.path.dirname(os.path.abspath(__file__))
_ELF_CLASSES = None


def elf_classes():
    """Class definitions recovered from the ELF (tools/igb_elf_classes.json), or {}."""
    global _ELF_CLASSES
    if _ELF_CLASSES is None:
        p = os.path.join(_HERE, 'igb_elf_classes.json')
        try:
            with open(p, 'r', encoding='utf-8') as f:
                _ELF_CLASSES = json.load(f)
        except OSError:
            _ELF_CLASSES = {}
    return _ELF_CLASSES


class IGBError(Exception):
    pass


class Reader:
    __slots__ = ('d', 'p')

    def __init__(self, data, pos=0):
        self.d = data
        self.p = pos

    def u32(self):
        v, = struct.unpack_from('<I', self.d, self.p)
        self.p += 4
        return v

    def s32(self):
        v, = struct.unpack_from('<i', self.d, self.p)
        self.p += 4
        return v

    def u16(self):
        v, = struct.unpack_from('<H', self.d, self.p)
        self.p += 2
        return v

    def take(self, n):
        b = self.d[self.p:self.p + n]
        if len(b) != n:
            raise IGBError('truncated: need %d bytes at 0x%x' % (n, self.p))
        self.p += n
        return b

    def cstr(self):
        e = self.d.index(b'\0', self.p)
        s = self.d[self.p:e].decode('latin1')
        self.p = e + 1
        return s

    def expect(self, pos, what):
        if self.p != pos:
            raise IGBError('%s: section end mismatch (at 0x%x, expected 0x%x)' % (what, self.p, pos))


class MetaField:
    """One persistent field of a metaobject as declared in the file."""
    __slots__ = ('type', 'ordinal', 'size', 'name', 'elf_type')

    def __init__(self, type_name, ordinal, size):
        self.type = type_name
        self.ordinal = ordinal
        self.size = size
        self.name = None
        self.elf_type = None

    def __repr__(self):
        return 'MetaField(%s %s ord=%d size=%d)' % (self.type, self.name, self.ordinal, self.size)


class MetaObject:
    __slots__ = ('index', 'name', 'version', 'w2', 'parent_index', 'parent', 'field_total', 'fields',
                 'file', 'first_own_ordinal')

    def __init__(self):
        self.fields = []
        self.parent = None

    def is_a(self, name):
        m = self
        while m is not None:
            if m.name == name:
                return True
            m = m.parent
        return False

    def chain(self):
        out = []
        m = self
        while m is not None:
            out.append(m.name)
            m = m.parent
        return out[::-1]

    def __repr__(self):
        return 'MetaObject(%s)' % self.name


class IgObject:
    """A deserialised object: .meta (MetaObject), .slot (directory index), .fields (name -> value),
    .raw (ordered list of (MetaField, value))."""
    __slots__ = ('meta', 'slot', 'fields', 'raw', 'entry')

    def __init__(self, meta, slot, entry):
        self.meta = meta
        self.slot = slot
        self.entry = entry
        self.fields = {}
        self.raw = []

    @property
    def cls(self):
        return self.meta.name

    def get(self, name, default=None):
        return self.fields.get(name, default)

    def __repr__(self):
        nm = self.fields.get('_name')
        return '<%s slot=%d%s>' % (self.meta.name, self.slot, ' name=%r' % nm if isinstance(nm, str) else '')


class IgMemory:
    """A raw memory block (vertex arrays, images, CLUTs, list storage).

    .data bytes, .size, .elem_type (metafield type name of the element, from _memTypeIndex),
    .elem_size (from the entry's _memTypeSize when present), .alignment_type (name or None),
    .refs: list of resolved objects when elem_type is a reference type."""
    __slots__ = ('slot', 'entry', 'data', 'size', 'elem_type', 'elem_size', 'alignment_type',
                 'ref_counted', 'refs', 'file_offset')

    def __init__(self, slot, entry):
        self.slot = slot
        self.entry = entry
        self.data = b''
        self.size = 0
        self.elem_type = None
        self.elem_size = None
        self.alignment_type = None
        self.ref_counted = None
        self.refs = None
        self.file_offset = None

    @property
    def cls(self):
        return 'igMemory'

    def __repr__(self):
        return '<igMemory slot=%d size=%d type=%s>' % (self.slot, self.size, self.elem_type)


class IgExternal:
    """A directory slot that refers to an object in another .IGB file (igExternalDirEntry,
    igExternalInfoEntry, igExternalIndexedEntry).  .entry holds the entry fields:
    _directoryName/_entryName/_entryIndex, or _directoryIndex/_infoName/_key, or
    _directoryIndex/_entryIndex; the directory index refers to IGBFile.external_names."""
    __slots__ = ('slot', 'entry')

    def __init__(self, slot, entry):
        self.slot = slot
        self.entry = entry

    @property
    def cls(self):
        return self.entry.cls

    @property
    def fields(self):
        return self.entry.fields

    def __repr__(self):
        return '<%s slot=%d %s>' % (self.entry.cls, self.slot,
                                    {k: v for k, v in self.entry.fields.items() if k != '_name'})


class Ref:
    """Unresolved directory reference (slot index); resolved to the object after loading."""
    __slots__ = ('slot',)

    def __init__(self, slot):
        self.slot = slot

    def __repr__(self):
        return 'Ref(%d)' % self.slot


REF_TYPES = {'igObjectRefMetaField', 'igMemoryRefMetaField', 'igRawRefMetaField',
             'igObjectRefArrayMetaField', 'igMemoryRefArrayMetaField', 'igRawRefArrayMetaField',
             'igRefMetaField'}

_SCALAR = {
    'igBoolMetaField': ('?', 1), 'igCharMetaField': ('b', 1), 'igUnsignedCharMetaField': ('B', 1),
    'igShortMetaField': ('h', 2), 'igUnsignedShortMetaField': ('H', 2),
    'igIntMetaField': ('i', 4), 'igUnsignedIntMetaField': ('I', 4), 'igEnumMetaField': ('i', 4),
    'igLongMetaField': ('q', 8), 'igUnsignedLongMetaField': ('Q', 8),
    'igFloatMetaField': ('f', 4), 'igDoubleMetaField': ('d', 8),
    'igVec2fMetaField': ('f', 4), 'igVec3fMetaField': ('f', 4), 'igVec4fMetaField': ('f', 4),
    'igMatrix44fMetaField': ('f', 4), 'igQuaternionfMetaField': ('f', 4),
}


def decode_field(mf, raw, strings):
    """Decode the raw bytes of one field according to its metafield type (verified: strings and refs
    are u32 indices, FUN_003271b0 / FUN_0031ffd0; everything else is a byte copy, FUN_003164d0)."""
    t = mf.type
    if t == 'igStringMetaField':
        idx, = struct.unpack_from('<i', raw, 0)
        return None if idx < 0 else strings[idx]
    if t in REF_TYPES:
        n = len(raw) // 4
        refs = [Ref(v) if v >= 0 else None for v in struct.unpack_from('<%di' % n, raw, 0)]
        return refs if (t.endswith('ArrayMetaField') or n != 1) else refs[0]
    base = t[:-len('ArrayMetaField')] + 'MetaField' if t.endswith('ArrayMetaField') else t
    fmt = _SCALAR.get(base)
    if fmt is None:
        return raw  # igStructMetaField, igCompoundMetaField, unknown -> bytes
    ch, sz = fmt
    n = len(raw) // sz
    if n * sz != len(raw):
        return raw
    vals = struct.unpack_from('<%d%s' % (n, ch), raw, 0)
    if base == 'igBoolMetaField':
        vals = tuple(bool(v) for v in vals)
    if base == t and n == 1:
        return vals[0]
    return list(vals)


class IGBFile:
    def __init__(self, path=None, data=None):
        self.path = path
        if data is None:
            with open(path, 'rb') as f:
                data = f.read()
        self.data = data
        self.strings = []
        self.metafield_types = []       # [(name, w1, w2)]
        self.alignment_names = []
        self.metaobjects = []
        self.external_names = []
        self.memory_pool_names = []
        self.entries = []               # IgObject for each igDirEntry record (templates)
        self.index = []                 # slot -> entry index
        self.info_list_place = None
        self.slots = []                 # slot -> IgObject | IgMemory
        self.objects = []
        self.memories = []
        self.sections = []              # (name, start, end)
        self.flags = {}
        self._parse()

    # -- helpers -------------------------------------------------------------------------------
    def _sec(self, name, start, end):
        self.sections.append((name, start, end))

    def _parse(self):
        r = Reader(self.data)
        d = self.data
        if len(d) < 0x30:
            raise IGBError('file too small')
        hdr = struct.unpack_from('<12I', d, 0)
        # FUN_002f70b0: w[10] = magic cookie, w[11] = version/flags, pairs = {size,count}
        (self.entry_buf_size, self.entry_count, self.metaobject_buf_size, self.metaobject_count,
         self.object_buf_size, self.object_count, self.memref_buf_size, self.memref_count,
         self.metafield_buf_size, self.metafield_count, magic, vw) = hdr
        if magic != MAGIC:
            if struct.unpack('>I', struct.pack('<I', magic))[0] == MAGIC:
                raise IGBError('big-endian IGB not supported')
            raise IGBError('bad magic 0x%08x' % magic)
        self.version = vw & 0xFFFF
        if self.version > 8:
            raise IGBError('unsupported version %d' % self.version)
        self.flags = dict(hasInfo=bool(vw & 0x80000000), hasExternal=bool(vw & 0x40000000),
                          sharedEntries=bool(vw & 0x20000000), hasMemoryPoolNames=bool(vw & 0x10000000),
                          hasVersion1Cruft=(vw == 1))
        if self.version < 4:
            raise IGBError('IGB version %d (<4) not supported' % self.version)
        r.p = 0x30
        self._sec('header', 0, 0x30)

        # --- string table (FUN_002f8b00, version >= 8) -------------------------------------------
        if self.version >= 8:
            start = r.p
            size = r.u32()
            count = r.u32()
            for _ in range(count):
                n = r.u32()
                s = r.take(n)
                if n == 0:
                    self.strings.append(None)
                else:
                    if s[-1:] != b'\0':
                        raise IGBError('string not NUL terminated at 0x%x' % r.p)
                    self.strings.append(s[:-1].decode('latin1'))
            r.expect(start + size, 'string table')
            self._sec('strings', start, r.p)

        # --- metafield type list (FUN_002f9ae0) -------------------------------------------------
        if self.version > 2:
            start = r.p
            recs = [struct.unpack_from('<III', d, start + 12 * i) for i in range(self.metafield_count)]
            r.p = start + 12 * self.metafield_count
            for (n, w1, w2) in recs:
                s = r.take(n)
                self.metafield_types.append((s.split(b'\0')[0].decode('latin1'), w1, w2))
            r.expect(start + self.metafield_buf_size, 'metafield type list')
            self._sec('metafield_types', start, r.p)

            # --- alignment type list (FUN_002f9e60) ---------------------------------------------
            start = r.p
            size = r.u32()
            self.alignment_w1 = r.u32()
            count = r.u32()
            if count > 0:
                lens = [r.u32() for _ in range(count)]
                for n in lens:
                    s = r.take(n)
                    self.alignment_names.append(s.split(b'\0')[0].decode('latin1'))
            r.expect(start + size, 'alignment list')
            self._sec('alignment_types', start, r.p)

        # --- metaobject list (FUN_002f72b0 + FUN_002f7360) ----------------------------------------
        start = r.p
        nw = 6 if self.version >= 4 else 3
        recs = [struct.unpack_from('<%dI' % nw, d, start + 4 * nw * i) for i in range(self.metaobject_count)]
        p = start + 4 * nw * self.metaobject_count
        for i, rec in enumerate(recs):
            m = MetaObject()
            m.index = i
            m.file = self
            m.name = d[p:p + rec[0]].split(b'\0')[0].decode('latin1')
            m.version = rec[1]
            m.w2 = rec[2]
            m.field_total = rec[3]
            m.parent_index = struct.unpack('<i', struct.pack('<I', rec[4]))[0]
            m.first_own_ordinal = rec[5] if nw == 6 else None
            p += rec[0]
            if nw == 6:
                for j in range(m.field_total):
                    ti, ordinal, size = struct.unpack_from('<HHH', d, p)
                    p += 6
                    if ti >= len(self.metafield_types):
                        raise IGBError('metafield type index %d out of range' % ti)
                    m.fields.append(MetaField(self.metafield_types[ti][0], ordinal, size))
            self.metaobjects.append(m)
        for m in self.metaobjects:
            if m.parent_index >= 0:
                m.parent = self.metaobjects[m.parent_index]
        r.p = p
        r.expect(start + self.metaobject_buf_size, 'metaobject list')
        self._sec('metaobjects', start, r.p)
        self._name_fields()

        # --- external directory list (FUN_002faa10, only if hasExternal) -------------------------
        if self.version >= 5 and self.flags['hasExternal']:
            start = r.p
            size = r.u32()
            self.external_w1 = r.u32()
            count = r.u32()
            lens = [r.u32() for _ in range(count)]
            for n in lens:
                s = r.take(n)
                self.external_names.append(s.split(b'\0')[0].decode('latin1'))
            r.expect(start + size, 'external directory list')
            self._sec('external_dirs', start, r.p)

        # --- memory pool names (FUN_002fae60) ---------------------------------------------------
        if self.flags['hasMemoryPoolNames']:
            start = r.p
            size = r.u32()
            count = r.u32()
            for _ in range(count):
                self.memory_pool_names.append(r.cstr())
            r.expect(start + size, 'memory pool names')
            self._sec('memory_pool_names', start, r.p)

        if not self.flags['sharedEntries']:
            raise IGBError('non-shared directory entries (flag bit 29 clear) not supported')

        # --- entry buffer (FUN_002f8f10 / FUN_002f8380) -----------------------------------------
        start = r.p
        for i in range(self.entry_count):
            self.entries.append(self._read_record(r, i, None))
        r.expect(start + self.entry_buf_size, 'entry buffer')
        self._sec('entries', start, r.p)

        # --- index buffer (FUN_002f9180): slot -> entry --------------------------------------------
        start = r.p
        size = r.u32()
        count = r.u32()
        n = (size - 8) // 2
        self.index = list(struct.unpack_from('<%dH' % n, d, r.p))
        r.p += 2 * n
        r.expect(start + size, 'index buffer')
        self.index_count = count
        if count != len(self.index):
            raise IGBError('index count %d != %d' % (count, len(self.index)))
        self._sec('index', start, r.p)

        # --- info list place (FUN_002fa940) -----------------------------------------------------
        if self.version >= 5 and self.flags['hasInfo']:
            self.info_list_place = r.s32()
            self._sec('info_list_place', r.p - 4, r.p)

        # instantiate slots (FUN_002f9400 -> igObjectDirEntry::FUN_0031bf60 / igMemoryDirEntry::FUN_00300b90)
        for slot, ei in enumerate(self.index):
            e = self.entries[ei]
            if e.meta.is_a('igObjectDirEntry'):
                ti = e.fields.get('_typeMetaIndex', -1)
                if ti is None or ti < 0:
                    nm = e.fields.get('_typeMetaName')
                    meta = next((m for m in self.metaobjects if m.name == nm), None)
                    if meta is None:
                        raise IGBError('object entry with unknown class %r' % nm)
                else:
                    meta = self.metaobjects[ti]
                self.slots.append(IgObject(meta, slot, e))
            elif e.meta.is_a('igExternalDirEntry') or e.meta.is_a('igExternalInfoEntry') \
                    or e.meta.is_a('igExternalIndexedEntry'):
                # resolved from another .IGB at load time (FUN_002f3440 / FUN_002f3bf0 / FUN_002f3b80);
                # consumes nothing from the object or memory buffers
                self.slots.append(IgExternal(slot, e))
            elif e.meta.is_a('igMemoryDirEntry'):
                mem = IgMemory(slot, e)
                mem.size = e.fields.get('_memSize', 0)
                mti = e.fields.get('_memTypeIndex')
                if mti is not None and mti >= 0:
                    mem.elem_type = self.metafield_types[mti][0]
                mem.elem_size = e.fields.get('_memTypeSize')
                ati = e.fields.get('_alignmentTypeIndex')
                if ati is not None and ati >= 0 and ati < len(self.alignment_names):
                    mem.alignment_type = self.alignment_names[ati]
                mem.ref_counted = e.fields.get('_refCounted')
                self.slots.append(mem)
            else:
                raise IGBError('unsupported directory entry class %s' % e.meta.name)

        # --- object buffer (FUN_002fa1d0 -> FUN_0031bee0 -> FUN_0031a5c0), slot order ----------------
        start = r.p
        for s in self.slots:
            if isinstance(s, IgObject):
                obj = self._read_record(r, s.slot, s)
                self.objects.append(obj)
        r.expect(start + self.object_buf_size, 'object buffer')
        self._sec('objects', start, r.p)

        # --- memory buffer (FUN_002fbf00 -> igMemoryDirEntry::FUN_00301530), slot order ------------
        start = r.p
        for s in self.slots:
            if isinstance(s, IgMemory):
                s.file_offset = r.p
                s.data = r.take(s.size)
                self.memories.append(s)
                r.p = start + ((r.p - start + 3) & ~3)
        if r.p > start + self.memref_buf_size:
            raise IGBError('memory buffer overrun')
        # the last block's 4-byte padding may be omitted
        if start + self.memref_buf_size - r.p > 3:
            raise IGBError('memory buffer: %d unread bytes' % (start + self.memref_buf_size - r.p))
        r.p = start + self.memref_buf_size
        self._sec('memory', start, r.p)
        self.trailing = len(d) - r.p
        self.end = r.p

        # --- resolve references ---------------------------------------------------------------
        for o in self.objects:
            for k, v in list(o.fields.items()):
                o.fields[k] = self._resolve(v)
            o.raw = [(mf, self._resolve(v)) for mf, v in o.raw]
        for m in self.memories:
            if m.elem_type in REF_TYPES and m.size % 4 == 0:
                m.refs = [self._resolve(Ref(v)) if v >= 0 else None
                          for v in struct.unpack_from('<%di' % (m.size // 4), m.data, 0)]
        self.info_list = self.slots[self.info_list_place] if self.info_list_place is not None and self.info_list_place >= 0 else None

    def _resolve(self, v):
        if isinstance(v, Ref):
            if v.slot >= len(self.slots):
                raise IGBError('reference to slot %d out of range' % v.slot)
            return self.slots[v.slot]
        if isinstance(v, list) and v and any(isinstance(x, Ref) for x in v):
            return [self._resolve(x) for x in v]
        return v

    def _read_record(self, r, slot, obj):
        """Object record: {u32 metaObjectIndex, u32 recordSize, fields...} (FUN_0031a5c0)."""
        p0 = r.p
        mi = r.u32()
        size = r.u32()
        if mi >= len(self.metaobjects):
            raise IGBError('record at 0x%x: metaobject index %d out of range' % (p0, mi))
        meta = self.metaobjects[mi]
        if obj is None:
            obj = IgObject(meta, slot, None)
        elif obj.meta is not meta:
            raise IGBError('record at 0x%x: class %s but directory says %s' % (p0, meta.name, obj.meta.name))
        for mf in meta.fields:
            raw = r.take(mf.size)
            r.p += (-mf.size) & 3  # FUN_00316490: consumed = (size + 3) & ~3
            v = decode_field(mf, raw, self.strings)
            obj.raw.append((mf, v))
            key = mf.name if mf.name else 'f%d_%s' % (mf.ordinal, mf.type)
            obj.fields[key] = v
        if r.p != p0 + size:
            raise IGBError('record at 0x%x (%s): fields end 0x%x but record size says 0x%x'
                           % (p0, meta.name, r.p, p0 + size))
        return obj

    def _name_fields(self):
        """Attach field names from the ELF class registry (ordinal = 2 hidden igObject fields +
        inherited + own, in registration order, FUN_00318080)."""
        defs = elf_classes()
        # Runtime field ordinals: igObject owns ordinals 0..1 (two hidden fields, FUN_003184f0 of
        # the igObject meta); each class' own fields occupy [parent.first_own_ordinal, first_own_ordinal)
        # in registration order.  The ranges come from the file, the names from the ELF.
        for m in self.metaobjects:
            names = {}
            c = m
            while c is not None:
                d = defs.get(c.name)
                hi = c.first_own_ordinal
                lo = c.parent.first_own_ordinal if c.parent is not None else 2
                if d is not None and hi is not None and lo is not None and hi - lo == len(d['fields']):
                    for k, f in enumerate(d['fields']):
                        names[lo + k] = (f['name'], f['type'])
                c = c.parent
            for mf in m.fields:
                if mf.ordinal in names and names[mf.ordinal][0]:
                    mf.name, mf.elf_type = names[mf.ordinal]
            # make names unique within the object
            seen = {}
            for mf in m.fields:
                if mf.name:
                    if mf.name in seen:
                        mf.name = '%s_%d' % (mf.name, mf.ordinal)
                    seen[mf.name] = True

    # -- convenience -------------------------------------------------------------------------
    def find(self, cls):
        return [o for o in self.objects if o.meta.is_a(cls)]

    def summary(self):
        from collections import Counter
        c = Counter(o.cls for o in self.objects)
        return dict(path=self.path, version=self.version, flags=self.flags, strings=len(self.strings),
                    metafield_types=len(self.metafield_types), classes=len(self.metaobjects),
                    entries=len(self.entries), slots=len(self.slots), objects=len(self.objects),
                    memories=len(self.memories), memory_bytes=sum(m.size for m in self.memories),
                    trailing=self.trailing, class_counts=dict(c))


# ------------------------------------------------------------------------------------------------
# Image / CLUT export
# ------------------------------------------------------------------------------------------------

def png_write(path, w, h, rgba):
    raw = b''.join(b'\0' + rgba[y * w * 4:(y + 1) * w * 4] for y in range(h))

    def chunk(t, b):
        c = t + b
        return struct.pack('>I', len(b)) + c + struct.pack('>I', zlib.crc32(c) & 0xffffffff)
    with open(path, 'wb') as f:
        f.write(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 6, 0, 0, 0))
                + chunk(b'IDAT', zlib.compress(raw, 9)) + chunk(b'IEND', b''))


def ps2_alpha(a):
    """PS2 alpha: 0x80 = opaque. Scale to 8-bit."""
    return 255 if a >= 0x80 else a * 2


def clut_colors(clut):
    """Return a list of RGBA tuples from an igClut object (32-bit RGBA entries, PS2 alpha).
    8-bit CLUTs with 256 entries are stored CSM1-swizzled (blocks of 8 swapped)."""
    data = clut.fields['_pData'].data
    n = clut.fields['_numEntries']
    stride = clut.fields.get('_stride', 4)
    cols = []
    for i in range(n):
        if stride == 4:
            r, g, b, a = data[i * 4:i * 4 + 4]
        elif stride == 2:
            v, = struct.unpack_from('<H', data, i * 2)
            r = (v & 31) << 3
            g = ((v >> 5) & 31) << 3
            b = ((v >> 10) & 31) << 3
            a = 0x80 if v & 0x8000 else 0
        else:
            raise IGBError('unsupported clut stride %d' % stride)
        cols.append((r, g, b, ps2_alpha(a)))
    if n == 256:
        # PS2 CSM1 32-bit CLUT ordering: within each 32-entry group, entries 8..15 and 16..23 swap
        fixed = list(cols)
        for base in range(0, 256, 32):
            fixed[base + 8:base + 16], fixed[base + 16:base + 24] = cols[base + 16:base + 24], cols[base + 8:base + 16]
        cols = fixed
    return cols


def image_rgba(img):
    """Decode an igImage to (w, h, rgba bytes). Palettised images use _pClut."""
    w = img.fields['_px']
    h = img.fields['_py']
    bits = img.fields.get('_bitsDepth') or img.fields.get('_bitsIdx') or 0
    data = img.fields['_pImage'].data if img.fields.get('_pImage') is not None else b''
    clut = img.fields.get('_pClut')
    out = bytearray(w * h * 4)
    if clut is not None and img.fields.get('_bitsIdx'):
        bidx = img.fields['_bitsIdx']
        cols = clut_colors(clut)
        if bidx == 8:
            for i in range(w * h):
                out[i * 4:i * 4 + 4] = bytes(cols[data[i]])
        elif bidx == 4:
            for i in range(w * h):
                v = data[i >> 1]
                idx = (v & 15) if (i & 1) == 0 else (v >> 4)
                out[i * 4:i * 4 + 4] = bytes(cols[idx])
        else:
            raise IGBError('unsupported index depth %d' % bidx)
    elif bits == 32:
        for i in range(w * h):
            r, g, b, a = data[i * 4:i * 4 + 4]
            out[i * 4:i * 4 + 4] = bytes((r, g, b, ps2_alpha(a)))
    elif bits == 24:
        for i in range(w * h):
            out[i * 4:i * 4 + 3] = data[i * 3:i * 3 + 3]
            out[i * 4 + 3] = 255
    elif bits == 16:
        for i in range(w * h):
            v, = struct.unpack_from('<H', data, i * 2)
            out[i * 4:i * 4 + 4] = bytes(((v & 31) << 3, ((v >> 5) & 31) << 3, ((v >> 10) & 31) << 3,
                                          255 if v & 0x8000 else 0))
    else:
        raise IGBError('unsupported image format bits=%s' % bits)
    return w, h, bytes(out)


def export_images(f, outdir):
    os.makedirs(outdir, exist_ok=True)
    written = []
    for i, img in enumerate(f.find('igImage')):
        name = img.fields.get('_pNameString') or img.fields.get('_name') or 'image%03d' % i
        name = os.path.basename(str(name)).replace('/', '_')
        base = os.path.splitext(os.path.basename(f.path))[0]
        w, h, rgba = image_rgba(img)
        p = os.path.join(outdir, '%s_%02d_%s.png' % (base, i, os.path.splitext(name)[0]))
        png_write(p, w, h, rgba)
        written.append(p)
    for i, clut in enumerate(f.find('igClut')):
        cols = clut_colors(clut)
        n = len(cols)
        w = 16 if n >= 16 else n
        h = (n + w - 1) // w
        rgba = bytearray(w * h * 4)
        for j, c in enumerate(cols):
            rgba[j * 4:j * 4 + 4] = bytes(c)
        base = os.path.splitext(os.path.basename(f.path))[0]
        p = os.path.join(outdir, '%s_clut%02d.png' % (base, i))
        png_write(p, w, h, bytes(rgba))
        written.append(p)
    return written


# ------------------------------------------------------------------------------------------------
# CLI
# ------------------------------------------------------------------------------------------------

def _fmt(v, depth=0):
    if isinstance(v, IgObject):
        return repr(v)
    if isinstance(v, IgMemory):
        return repr(v)
    if isinstance(v, (bytes, bytearray)):
        return '<%d bytes %s>' % (len(v), v[:16].hex())
    if isinstance(v, list):
        if len(v) > 12:
            return '[%s, ... %d items]' % (', '.join(_fmt(x) for x in v[:12]), len(v))
        return '[%s]' % ', '.join(_fmt(x) for x in v)
    if isinstance(v, float):
        return '%g' % v
    return repr(v)


def dump(f, out=sys.stdout):
    s = f.summary()
    print('%s: version %d flags %s' % (f.path, f.version, {k: v for k, v in f.flags.items() if v}), file=out)
    print('sections:', file=out)
    for name, a, b in f.sections:
        print('  %-20s 0x%06x..0x%06x (%d bytes)' % (name, a, b, b - a), file=out)
    print('trailing bytes: %d' % f.trailing, file=out)
    print('metafield types (%d): %s' % (len(f.metafield_types), ', '.join(t[0] for t in f.metafield_types)), file=out)
    print('alignment types: %s' % f.alignment_names, file=out)
    print('memory pool names: %s' % [n for n in f.memory_pool_names if n], file=out)
    print('external: %s' % f.external_names, file=out)
    print('classes:', file=out)
    for m in f.metaobjects:
        print('  [%d] %s (parent %s, %d fields): %s' % (m.index, m.name, m.parent.name if m.parent else None,
              len(m.fields), ', '.join('%s:%s[%d]' % (mf.name or ('ord%d' % mf.ordinal), mf.type.replace('MetaField', ''), mf.size) for mf in m.fields)), file=out)
    print('entries:', file=out)
    for i, e in enumerate(f.entries):
        print('  [%d] %s %s' % (i, e.cls, {k: _fmt(v) for k, v in e.fields.items()}), file=out)
    print('info list: %r' % (f.info_list,), file=out)
    print('objects (%d) and memory blocks (%d):' % (len(f.objects), len(f.memories)), file=out)
    for s in f.slots:
        if isinstance(s, IgObject):
            print('  slot %d %s' % (s.slot, s.cls), file=out)
            for mf, v in s.raw:
                print('      %-24s %-14s %s' % (mf.name or ('ord%d' % mf.ordinal), mf.type.replace('MetaField', ''), _fmt(v)), file=out)
        else:
            extra = ''
            if s.refs is not None:
                extra = ' refs=' + _fmt(s.refs)
            print('  slot %d igMemory size=%d elem=%s elemSize=%s align=%s @0x%x%s' % (
                s.slot, s.size, s.elem_type, s.elem_size, s.alignment_type, s.file_offset, extra), file=out)


def iter_igb(root):
    for dp, dn, fn in os.walk(root):
        for n in fn:
            if n.lower().endswith('.igb'):
                yield os.path.join(dp, n)


def scan_all(root, classes_out=None, verbose=True):
    from collections import Counter, defaultdict
    files = sorted(iter_igb(root))
    class_counts = Counter()
    class_files = Counter()
    layouts = {}
    layout_conflicts = defaultdict(set)
    mftypes = Counter()
    versions = Counter()
    flagsets = Counter()
    errors = []
    trailing = Counter()
    n_ok = 0
    for i, p in enumerate(files):
        try:
            f = IGBFile(p)
        except Exception as e:  # noqa: BLE001
            errors.append((p, '%s: %s' % (type(e).__name__, e)))
            if verbose:
                print('ERR %s: %s' % (p, e), file=sys.stderr)
            continue
        n_ok += 1
        versions[f.version] += 1
        flagsets[tuple(k for k, v in f.flags.items() if v)] += 1
        trailing[f.trailing] += 1
        for t in f.metafield_types:
            mftypes[t[0]] += 1
        seen = set()
        for o in f.objects:
            class_counts[o.cls] += 1
            seen.add(o.cls)
        for c in seen:
            class_files[c] += 1
        for m in f.metaobjects:
            lay = tuple((mf.type, mf.ordinal, mf.size, mf.name) for mf in m.fields)
            key = m.name
            if key in layouts and layouts[key]['layout'] != lay:
                layout_conflicts[key].add(lay)
            layouts.setdefault(key, dict(layout=lay, parent=m.parent.name if m.parent else None,
                                         field_total=m.field_total, example=os.path.relpath(p, root)))
    defs = elf_classes()

    def elf_offset(cls, field_name):
        c = cls
        while c:
            d = defs.get(c)
            if d is None:
                return None
            for fd in d['fields']:
                if fd['name'] == field_name:
                    return fd['offset']
            c = d.get('parent')
        return None

    report = dict(
        files=len(files), parsed=n_ok, errors=errors, versions=dict(versions),
        flag_sets={'+'.join(k): v for k, v in flagsets.items()}, trailing_bytes=dict(trailing),
        metafield_types=dict(mftypes.most_common()),
        class_object_counts=dict(class_counts.most_common()),
        class_file_counts=dict(class_files.most_common()),
        classes={k: dict(parent=v['parent'], field_total=v['field_total'], example=v['example'],
                         objects=class_counts.get(k, 0), files=class_files.get(k, 0),
                         elf_size=(defs.get(k) or {}).get('size'), elf_vtable=(defs.get(k) or {}).get('vtable'),
                         elf_registration_site=(defs.get(k) or {}).get('site'),
                         fields=[dict(type=t, ordinal=o, size=s, name=n,
                                      elf_offset=elf_offset(k, n) if n else None)
                                 for (t, o, s, n) in v['layout']],
                         layout_variants=len(layout_conflicts.get(k, ())) + 1)
                 for k, v in sorted(layouts.items())},
    )
    if classes_out:
        with open(classes_out, 'w', encoding='utf-8') as fh:
            json.dump(report, fh, indent=1)
    return report


def main(argv):
    if not argv or argv[0] in ('-h', '--help'):
        print(__doc__)
        return 0
    if argv[0] == '--all':
        root = argv[1] if len(argv) > 1 else os.path.join(_HERE, '..', 'work', 'assets')
        out = None
        if '--classes' in argv:
            out = argv[argv.index('--classes') + 1]
        rep = scan_all(root, out)
        print('files %d parsed %d errors %d' % (rep['files'], rep['parsed'], len(rep['errors'])))
        print('versions', rep['versions'], 'flags', rep['flag_sets'], 'trailing', rep['trailing_bytes'])
        print('metafield types:', rep['metafield_types'])
        print('classes (%d):' % len(rep['class_object_counts']))
        for k, v in rep['class_object_counts'].items():
            print('  %-40s %7d objects in %4d files' % (k, v, rep['class_file_counts'][k]))
        for p, e in rep['errors'][:50]:
            print('ERR', p, e)
        return 1 if rep['errors'] else 0
    f = IGBFile(argv[0])
    if '--dump' in argv or len(argv) == 1:
        dump(f)
    if '--png' in argv:
        for p in export_images(f, argv[argv.index('--png') + 1]):
            print('wrote', p)
    if '--obj' in argv:
        if _HERE not in sys.path:
            sys.path.insert(0, _HERE)
        sys.modules.setdefault('igb', sys.modules[__name__])  # igb_geom must see our class objects
        import igb_geom  # tools/igb_geom.py
        meshes, skel = igb_geom.export_obj(f, argv[argv.index('--obj') + 1])
        print('wrote %s: %d meshes%s' % (argv[argv.index('--obj') + 1], len(meshes),
                                         ', %d bones' % len(skel['bones']) if skel else ''))
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
