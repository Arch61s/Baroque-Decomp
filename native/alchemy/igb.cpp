// Intrinsic Alchemy .IGB reader. Read order follows igIGBFile::readFile FUN_002fb270
// (work/decompiled/002fb270.c), version-8 branch; see work/reports/IGB_FORMAT.md for the evidence.
#include "alchemy/igb.hpp"
#include "alchemy/igb_elf_fields.hpp"

#include <cstring>
#include <fstream>
#include <stdexcept>

namespace baroque::alchemy {

namespace {

struct ParseError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

[[noreturn]] void fail(const std::string& what) { throw ParseError(what); }

std::string hex(size_t v) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "0x%zx", v);
    return buf;
}

uint32_t rd32(const uint8_t* p) {
    return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
}
uint16_t rd16(const uint8_t* p) { return uint16_t(p[0] | (p[1] << 8)); }

bool endsWith(std::string_view s, std::string_view suffix) {
    return s.size() >= suffix.size() && s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

}  // namespace

bool isRefMetaType(std::string_view t) {
    return t == "igObjectRefMetaField" || t == "igMemoryRefMetaField" || t == "igRawRefMetaField" ||
           t == "igObjectRefArrayMetaField" || t == "igMemoryRefArrayMetaField" ||
           t == "igRawRefArrayMetaField" || t == "igRefMetaField";
}

// ---------------------------------------------------------------------------------------------
// MetaObject / Value / IgObject
// ---------------------------------------------------------------------------------------------

bool MetaObject::isA(std::string_view cls) const {
    for (const MetaObject* m = this; m != nullptr; m = m->parent)
        if (m->name == cls) return true;
    return false;
}

bool Value::isString() const { return field->type == "igStringMetaField"; }
bool Value::isRef() const { return isRefMetaType(field->type) && !isRefArray(); }
bool Value::isRefArray() const {
    return isRefMetaType(field->type) && (endsWith(field->type, "ArrayMetaField") || raw.size() != 4);
}
int32_t Value::i32() const {
    if (raw.size() >= 4) return int32_t(rd32(raw.data()));
    if (raw.size() == 2) return int16_t(rd16(raw.data()));
    if (raw.size() == 1) return int8_t(raw[0]);
    return 0;
}
uint32_t Value::u32() const {
    if (raw.size() >= 4) return rd32(raw.data());
    if (raw.size() == 2) return rd16(raw.data());
    if (raw.size() == 1) return raw[0];
    return 0;
}
int64_t Value::i64() const {
    if (raw.size() >= 8) return int64_t(uint64_t(rd32(raw.data())) | (uint64_t(rd32(raw.data() + 4)) << 32));
    return i32();
}
float Value::f32() const {
    float f = 0.0f;
    if (raw.size() >= 4) std::memcpy(&f, raw.data(), 4);
    return f;
}
bool Value::b() const { return !raw.empty() && raw[0] != 0; }
void Value::floats(float* out, size_t n) const {
    for (size_t i = 0; i < n; ++i) {
        float f = 0.0f;
        if ((i + 1) * 4 <= raw.size()) std::memcpy(&f, raw.data() + i * 4, 4);
        out[i] = f;
    }
}

const Value* IgObject::find(std::string_view name) const {
    for (const Value& v : values)
        if (v.field->name == name) return &v;
    return nullptr;
}
int32_t IgObject::getInt(std::string_view name, int32_t def) const {
    const Value* v = find(name);
    return v ? v->i32() : def;
}
uint32_t IgObject::getUInt(std::string_view name, uint32_t def) const {
    const Value* v = find(name);
    return v ? v->u32() : def;
}
int64_t IgObject::getLong(std::string_view name, int64_t def) const {
    const Value* v = find(name);
    return v ? v->i64() : def;
}
float IgObject::getFloat(std::string_view name, float def) const {
    const Value* v = find(name);
    return v ? v->f32() : def;
}
bool IgObject::getBool(std::string_view name, bool def) const {
    const Value* v = find(name);
    return v ? v->b() : def;
}
std::string_view IgObject::getString(std::string_view name) const {
    const Value* v = find(name);
    if (v == nullptr || !v->isString() || file == nullptr) return {};
    return file->string(v->stringIndex);
}
int IgObject::getRef(std::string_view name) const {
    const Value* v = find(name);
    return v ? v->refSlot : -1;
}
bool IgObject::getFloats(std::string_view name, float* out, size_t n) const {
    const Value* v = find(name);
    if (v == nullptr || v->floatCount() < n) return false;
    v->floats(out, n);
    return true;
}

bool IgMemory::isRefType() const { return isRefMetaType(elemType); }

std::string IgExternal::keyName() const {
    size_t e = key.find_first_of(" \t");
    return e == std::string::npos ? key : key.substr(0, e);
}

// ---------------------------------------------------------------------------------------------
// Reader
// ---------------------------------------------------------------------------------------------

struct IgbFile::Reader {
    const std::vector<uint8_t>& d;
    size_t p = 0;
    explicit Reader(const std::vector<uint8_t>& data) : d(data) {}
    void need(size_t n) const {
        if (p + n > d.size()) fail("truncated: need " + std::to_string(n) + " bytes at " + hex(p));
    }
    uint32_t u32() { need(4); uint32_t v = rd32(&d[p]); p += 4; return v; }
    int32_t s32() { return int32_t(u32()); }
    uint16_t u16() { need(2); uint16_t v = rd16(&d[p]); p += 2; return v; }
    const uint8_t* take(size_t n) { need(n); const uint8_t* b = &d[p]; p += n; return b; }
    std::string cstr() {
        size_t e = p;
        while (e < d.size() && d[e] != 0) ++e;
        if (e >= d.size()) fail("unterminated string at " + hex(p));
        std::string s(reinterpret_cast<const char*>(&d[p]), e - p);
        p = e + 1;
        return s;
    }
    void expect(size_t pos, const char* what) const {
        if (p != pos) fail(std::string(what) + ": section end mismatch (at " + hex(p) + ", expected " + hex(pos) + ")");
    }
};

bool IgbFile::load(const std::filesystem::path& path, std::string* error) {
    path_ = path;
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        if (error) *error = "cannot open " + path.string();
        return false;
    }
    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    return parse(std::move(bytes), error);
}

bool IgbFile::parse(std::vector<uint8_t> d, std::string* error) {
    try {
        if (d.size() < 0x30) fail("file too small");
        // FUN_002f70b0: five {size,count} pairs, magic cookie, version word.
        entryBufSize_ = rd32(&d[0x00]); entryCount_ = rd32(&d[0x04]);
        metaobjectBufSize_ = rd32(&d[0x08]); metaobjectCount_ = rd32(&d[0x0c]);
        objectBufSize_ = rd32(&d[0x10]); objectCount_ = rd32(&d[0x14]);
        memrefBufSize_ = rd32(&d[0x18]); memrefCount_ = rd32(&d[0x1c]);
        metafieldBufSize_ = rd32(&d[0x20]); metafieldCount_ = rd32(&d[0x24]);
        const uint32_t magic = rd32(&d[0x28]);
        const uint32_t vw = rd32(&d[0x2c]);
        if (magic != kIgbMagic) {
            const uint32_t swapped = ((magic & 0xff) << 24) | ((magic & 0xff00) << 8) | ((magic >> 8) & 0xff00) | (magic >> 24);
            if (swapped == kIgbMagic) fail("big-endian IGB not supported");
            fail("bad magic " + hex(magic));
        }
        version_ = vw & 0xFFFF;
        if (version_ > 8) fail("unsupported version " + std::to_string(version_));
        flags_.hasInfo = (vw & 0x80000000u) != 0;
        flags_.hasExternal = (vw & 0x40000000u) != 0;
        flags_.sharedEntries = (vw & 0x20000000u) != 0;
        flags_.hasMemoryPoolNames = (vw & 0x10000000u) != 0;
        flags_.hasVersion1Cruft = (vw == 1);
        if (version_ < 4) fail("IGB version < 4 not supported");

        Reader r(d);
        r.p = 0x30;

        // --- string table (FUN_002f8b00 -> FUN_002f87c0, version >= 8) ---------------------------
        if (version_ >= 8) {
            const size_t start = r.p;
            const uint32_t size = r.u32();
            const uint32_t count = r.u32();
            for (uint32_t i = 0; i < count; ++i) {
                const uint32_t n = r.u32();
                const uint8_t* s = r.take(n);
                if (n == 0) {
                    strings_.emplace_back();
                    stringNull_.push_back(true);
                } else {
                    if (s[n - 1] != 0) fail("string not NUL terminated at " + hex(r.p));
                    strings_.emplace_back(reinterpret_cast<const char*>(s), n - 1);
                    stringNull_.push_back(false);
                }
            }
            r.expect(start + size, "string table");
        }

        // --- metafield type list (FUN_002f9ae0) ----------------------------------------------------
        if (version_ > 2) {
            const size_t start = r.p;
            std::vector<uint32_t> lens;
            for (uint32_t i = 0; i < metafieldCount_; ++i) {
                const uint8_t* rec = r.take(12);
                lens.push_back(rd32(rec));  // words 1/2 (always 1, 0) are not read by the loader
            }
            for (uint32_t n : lens) {
                const uint8_t* s = r.take(n);
                size_t e = 0;
                while (e < n && s[e] != 0) ++e;
                metafieldTypes_.emplace_back(reinterpret_cast<const char*>(s), e);
            }
            r.expect(start + metafieldBufSize_, "metafield type list");

            // --- alignment type list (FUN_002f9e60) -----------------------------------------------
            const size_t astart = r.p;
            const uint32_t asize = r.u32();
            (void)r.u32();  // always 1
            const uint32_t acount = r.u32();
            if (acount > 0) {
                std::vector<uint32_t> alens;
                for (uint32_t i = 0; i < acount; ++i) alens.push_back(r.u32());
                for (uint32_t n : alens) {
                    const uint8_t* s = r.take(n);
                    size_t e = 0;
                    while (e < n && s[e] != 0) ++e;
                    alignmentNames_.emplace_back(reinterpret_cast<const char*>(s), e);
                }
            }
            r.expect(astart + asize, "alignment list");
        }

        // --- metaobject list (FUN_002f72b0 + FUN_002f7360) --------------------------------------------
        {
            const size_t start = r.p;
            const size_t nw = version_ >= 4 ? 6 : 3;
            std::vector<std::vector<uint32_t>> recs;
            for (uint32_t i = 0; i < metaobjectCount_; ++i) {
                const uint8_t* rec = r.take(4 * nw);
                std::vector<uint32_t> w(nw);
                for (size_t k = 0; k < nw; ++k) w[k] = rd32(rec + 4 * k);
                recs.push_back(std::move(w));
            }
            metaobjects_.reserve(metaobjectCount_);
            for (uint32_t i = 0; i < metaobjectCount_; ++i) {
                auto m = std::make_unique<MetaObject>();
                const std::vector<uint32_t>& w = recs[i];
                m->index = int(i);
                const uint8_t* nm = r.take(w[0]);
                size_t e = 0;
                while (e < w[0] && nm[e] != 0) ++e;
                m->name.assign(reinterpret_cast<const char*>(nm), e);
                m->version = w[1];
                m->w2 = w[2];
                m->fieldTotal = w[3];
                m->parentIndex = int32_t(w[4]);
                m->firstFreeOrdinal = nw == 6 ? w[5] : 0;
                if (nw == 6) {
                    for (uint32_t j = 0; j < m->fieldTotal; ++j) {
                        const uint8_t* f = r.take(6);
                        MetaField mf;
                        const uint16_t ti = rd16(f);
                        if (ti >= metafieldTypes_.size()) fail("metafield type index out of range");
                        mf.type = metafieldTypes_[ti];
                        mf.ordinal = rd16(f + 2);
                        mf.size = rd16(f + 4);
                        m->fields.push_back(std::move(mf));
                    }
                }
                metaobjects_.push_back(std::move(m));
            }
            for (auto& m : metaobjects_) {
                if (m->parentIndex >= 0) {
                    if (size_t(m->parentIndex) >= metaobjects_.size()) fail("metaobject parent out of range");
                    m->parent = metaobjects_[size_t(m->parentIndex)].get();
                }
            }
            r.expect(start + metaobjectBufSize_, "metaobject list");
            nameFields();
        }

        // --- external directory list (FUN_002faa10, only if hasExternal) -----------------------------
        if (version_ >= 5 && flags_.hasExternal) {
            const size_t start = r.p;
            const uint32_t size = r.u32();
            (void)r.u32();  // always 1
            const uint32_t count = r.u32();
            std::vector<uint32_t> lens;
            for (uint32_t i = 0; i < count; ++i) lens.push_back(r.u32());
            for (uint32_t n : lens) {
                const uint8_t* s = r.take(n);
                size_t e = 0;
                while (e < n && s[e] != 0) ++e;
                externalNames_.emplace_back(reinterpret_cast<const char*>(s), e);
            }
            r.expect(start + size, "external directory list");
        }

        // --- memory pool names (FUN_002fae60) ---------------------------------------------------------
        if (flags_.hasMemoryPoolNames) {
            const size_t start = r.p;
            const uint32_t size = r.u32();
            const uint32_t count = r.u32();
            for (uint32_t i = 0; i < count; ++i) memoryPoolNames_.push_back(r.cstr());
            r.expect(start + size, "memory pool names");
        }

        if (!flags_.sharedEntries) fail("non-shared directory entries (flag bit 29 clear) not supported");

        // --- entry buffer (FUN_002f8f10 / FUN_002f8380): igDirEntry templates --------------------------
        {
            const size_t start = r.p;
            for (uint32_t i = 0; i < entryCount_; ++i) {
                auto e = std::make_unique<IgObject>();
                e->file = this;
                e->slot = -1;
                readRecord(r, *e, false);
                entries_.push_back(std::move(e));
            }
            r.expect(start + entryBufSize_, "entry buffer");
        }

        // --- index buffer (FUN_002f9180): slot -> entry template --------------------------------------
        {
            const size_t start = r.p;
            const uint32_t size = r.u32();
            const uint32_t count = r.u32();
            if (size < 8) fail("index buffer size");
            const size_t n = (size - 8) / 2;
            const uint8_t* b = r.take(2 * n);
            index_.resize(n);
            for (size_t i = 0; i < n; ++i) index_[i] = rd16(b + 2 * i);
            r.expect(start + size, "index buffer");
            if (count != n) fail("index count mismatch");
        }

        // --- info list place (FUN_002fa940) -----------------------------------------------------------
        if (version_ >= 5 && flags_.hasInfo) infoListPlace_ = r.s32();

        // instantiate slots (FUN_002f9400 -> igObjectDirEntry::FUN_0031bf60 / igMemoryDirEntry::FUN_00300b90)
        slots_.resize(index_.size());
        for (size_t s = 0; s < index_.size(); ++s) {
            if (index_[s] >= entries_.size()) fail("index entry out of range");
            const IgObject& e = *entries_[index_[s]];
            Slot& slot = slots_[s];
            if (e.isA("igObjectDirEntry")) {
                const int ti = e.getInt("_typeMetaIndex", -1);
                const MetaObject* meta = nullptr;
                if (ti < 0) {
                    meta = metaobject(e.getString("_typeMetaName"));
                    if (meta == nullptr) fail("object entry with unknown class");
                } else {
                    if (size_t(ti) >= metaobjects_.size()) fail("object entry class index out of range");
                    meta = metaobjects_[size_t(ti)].get();
                }
                auto o = std::make_unique<IgObject>();
                o->file = this;
                o->meta = meta;
                o->slot = int(s);
                slot.kind = SlotKind::Object;
                slot.object = o.get();
                objectStore_.push_back(std::move(o));
            } else if (e.isA("igExternalDirEntry") || e.isA("igExternalInfoEntry") || e.isA("igExternalIndexedEntry")) {
                // resolved from another .IGB at load time (FUN_002f3440 / FUN_002f3bf0 / FUN_002f3b80)
                auto x = std::make_unique<IgExternal>();
                x->slot = int(s);
                x->entryClass = e.meta->name;
                x->key = std::string(e.getString("_key"));
                x->infoName = std::string(e.getString("_infoName"));
                x->directoryIndex = e.getInt("_directoryIndex", -1);
                x->directoryName = std::string(e.getString("_directoryName"));
                x->entryName = std::string(e.getString("_entryName"));
                x->entryIndex = e.getInt("_entryIndex", -1);
                slot.kind = SlotKind::External;
                slot.external = x.get();
                externalStore_.push_back(std::move(x));
            } else if (e.isA("igMemoryDirEntry")) {
                auto m = std::make_unique<IgMemory>();
                m->slot = int(s);
                m->size = e.getUInt("_memSize", 0);
                const int mti = e.getInt("_memTypeIndex", -1);
                if (mti >= 0 && size_t(mti) < metafieldTypes_.size()) m->elemType = metafieldTypes_[size_t(mti)];
                const int ati = e.getInt("_alignmentTypeIndex", -1);
                if (ati >= 0 && size_t(ati) < alignmentNames_.size()) m->alignmentType = alignmentNames_[size_t(ati)];
                m->refCounted = e.getBool("_refCounted", false);
                slot.kind = SlotKind::Memory;
                slot.memory = m.get();
                memoryStore_.push_back(std::move(m));
            } else {
                fail("unsupported directory entry class " + e.meta->name);
            }
        }

        // --- object buffer (FUN_002fa1d0 -> FUN_0031bee0 -> FUN_0031a5c0), slot order ------------------
        {
            const size_t start = r.p;
            for (Slot& s : slots_) {
                if (s.kind == SlotKind::Object) {
                    readRecord(r, *s.object, true);
                    objects_.push_back(s.object);
                }
            }
            r.expect(start + objectBufSize_, "object buffer");
        }

        // --- memory buffer (FUN_002fbf00 -> igMemoryDirEntry::FUN_00301530), slot order ---------------
        {
            const size_t start = r.p;
            for (Slot& s : slots_) {
                if (s.kind == SlotKind::Memory) {
                    IgMemory& m = *s.memory;
                    m.fileOffset = r.p;
                    const uint8_t* b = r.take(m.size);
                    m.data.assign(b, b + m.size);
                    memories_.push_back(&m);
                    r.p = start + ((r.p - start + 3) & ~size_t(3));
                    if (r.p > d.size()) r.p = d.size();
                } else if (s.kind == SlotKind::External) {
                    externals_.push_back(s.external);
                }
            }
            if (r.p > start + memrefBufSize_) fail("memory buffer overrun");
            // the last block's 4-byte padding may be omitted
            if (start + memrefBufSize_ - r.p > 3) fail("memory buffer: unread bytes");
            r.p = start + memrefBufSize_;
            if (r.p > d.size()) fail("memory buffer past end of file");
        }
        trailing_ = d.size() - r.p;

        // --- resolve reference-typed memory blocks (list storage, vertex stream tables) ----------------
        for (IgMemory* m : memories_) {
            if (m->isRefType() && m->size % 4 == 0) {
                m->refSlots.resize(m->size / 4);
                for (size_t i = 0; i < m->refSlots.size(); ++i) {
                    const int32_t v = int32_t(rd32(&m->data[i * 4]));
                    if (v >= int32_t(slots_.size())) fail("reference to slot out of range");
                    m->refSlots[i] = v < 0 ? -1 : v;
                }
            }
        }
        for (IgObject* o : objects_) {
            for (Value& v : o->values) {
                if (v.refSlot >= int(slots_.size())) fail("reference to slot out of range");
                for (int rs : v.refSlots)
                    if (rs >= int(slots_.size())) fail("reference to slot out of range");
            }
        }
        if (infoListPlace_ >= int(slots_.size())) fail("info list place out of range");
    } catch (const std::exception& e) {
        if (error) *error = e.what();
        return false;
    }
    return true;
}

// Object record: {u32 metaObjectIndex, u32 recordSize, fields...} (FUN_0031a5c0); each field consumes
// (size + 3) & ~3 bytes (FUN_00316490).
void IgbFile::readRecord(Reader& r, IgObject& obj, bool checkMeta) {
    const size_t p0 = r.p;
    const uint32_t mi = r.u32();
    const uint32_t size = r.u32();
    if (mi >= metaobjects_.size()) fail("record at " + hex(p0) + ": metaobject index out of range");
    const MetaObject* meta = metaobjects_[mi].get();
    if (!checkMeta) {
        obj.meta = meta;
    } else if (obj.meta != meta) {
        fail("record at " + hex(p0) + ": class " + meta->name + " but directory says " + obj.meta->name);
    }
    obj.values.reserve(meta->fields.size());
    for (const MetaField& mf : meta->fields) {
        Value v;
        v.field = &mf;
        const uint8_t* raw = r.take(mf.size);
        v.raw.assign(raw, raw + mf.size);
        r.p += (4 - (size_t(mf.size) & 3)) & 3;
        if (mf.type == "igStringMetaField") {
            v.stringIndex = mf.size >= 4 ? int32_t(rd32(raw)) : -1;
            if (v.stringIndex >= int(strings_.size())) fail("string index out of range");
            if (v.stringIndex >= 0 && stringNull_[size_t(v.stringIndex)]) v.stringIndex = -1;
        } else if (isRefMetaType(mf.type)) {
            const size_t n = mf.size / 4;
            if (endsWith(mf.type, "ArrayMetaField") || n != 1) {
                for (size_t i = 0; i < n; ++i) {
                    const int32_t s = int32_t(rd32(raw + 4 * i));
                    v.refSlots.push_back(s < 0 ? -1 : s);
                }
            } else {
                const int32_t s = int32_t(rd32(raw));
                v.refSlot = s < 0 ? -1 : s;
            }
        }
        obj.values.push_back(std::move(v));
    }
    if (r.p != p0 + size) fail("record at " + hex(p0) + " (" + meta->name + "): fields end does not match record size");
}

// Attach field names from the ELF class registry: runtime ordinals are igObject 0..1 (two hidden fields,
// FUN_003184f0), then each class' own fields in registration order (FUN_00318080) occupying
// [parent.firstFreeOrdinal, firstFreeOrdinal). Ranges come from the file, names from the ELF.
void IgbFile::nameFields() {
    for (auto& m : metaobjects_) {
        std::map<uint16_t, const elf::FieldDef*> names;
        for (const MetaObject* c = m.get(); c != nullptr; c = c->parent) {
            const elf::ClassDef* d = elf::findClass(c->name);
            const uint32_t hi = c->firstFreeOrdinal;
            const uint32_t lo = c->parent != nullptr ? c->parent->firstFreeOrdinal : 2;
            if (d != nullptr && hi >= lo && hi - lo == uint32_t(d->fieldCount)) {
                for (int k = 0; k < d->fieldCount; ++k) names[uint16_t(lo + uint32_t(k))] = &d->fields[k];
            }
        }
        std::map<std::string, int> seen;
        for (MetaField& mf : m->fields) {
            auto it = names.find(mf.ordinal);
            if (it != names.end() && it->second->name[0] != '\0') {
                mf.name = it->second->name;
                mf.elfType = it->second->type;
            } else {
                mf.name = "ord" + std::to_string(mf.ordinal) + "_" + mf.type;
            }
            // make names unique within the object (tools/igb.py: "%s_%d" % (name, ordinal))
            if (seen.count(mf.name) != 0) mf.name = mf.name + "_" + std::to_string(mf.ordinal);
            seen[mf.name] = 1;
        }
    }
}

// ---------------------------------------------------------------------------------------------
// Accessors
// ---------------------------------------------------------------------------------------------

std::string_view IgbFile::string(int index) const {
    if (index < 0 || size_t(index) >= strings_.size() || stringNull_[size_t(index)]) return {};
    return strings_[size_t(index)];
}

const Slot* IgbFile::slot(int index) const {
    if (index < 0 || size_t(index) >= slots_.size()) return nullptr;
    return &slots_[size_t(index)];
}
const IgObject* IgbFile::object(int s) const {
    const Slot* sl = slot(s);
    return sl != nullptr && sl->kind == SlotKind::Object ? sl->object : nullptr;
}
const IgMemory* IgbFile::memory(int s) const {
    const Slot* sl = slot(s);
    return sl != nullptr && sl->kind == SlotKind::Memory ? sl->memory : nullptr;
}
const IgExternal* IgbFile::external(int s) const {
    const Slot* sl = slot(s);
    return sl != nullptr && sl->kind == SlotKind::External ? sl->external : nullptr;
}

std::vector<const IgObject*> IgbFile::find(std::string_view cls) const {
    std::vector<const IgObject*> out;
    for (const IgObject* o : objects_)
        if (o->isA(cls)) out.push_back(o);
    return out;
}

const MetaObject* IgbFile::metaobject(std::string_view name) const {
    for (const auto& m : metaobjects_)
        if (m->name == name) return m.get();
    return nullptr;
}

std::vector<int> IgbFile::listSlots(const IgObject* list) const {
    std::vector<int> out;
    if (list == nullptr) return out;
    const IgMemory* m = memory(list->getRef("_data"));
    if (m == nullptr) return out;
    const int count = list->getInt("_count", int(m->refSlots.size()));
    for (size_t i = 0; i < m->refSlots.size() && int(i) < count; ++i) out.push_back(m->refSlots[i]);
    return out;
}

std::vector<const IgObject*> IgbFile::listObjects(const IgObject* list) const {
    std::vector<const IgObject*> out;
    for (int s : listSlots(list)) {
        const IgObject* o = object(s);
        if (o != nullptr) out.push_back(o);
    }
    return out;
}

std::map<std::string, int> IgbFile::classCounts() const {
    std::map<std::string, int> c;
    for (const IgObject* o : objects_) c[o->meta->name] += 1;
    return c;
}

}  // namespace baroque::alchemy
