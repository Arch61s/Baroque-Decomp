#include "data/prm.hpp"
#include <algorithm>
#include <cstdio>

namespace baroque::data {

namespace {

bool isDigit(char c) { return c >= '0' && c <= '9'; }

// python rec_strings(): both offsets must be inside the file and non-zero.
void readStrings(Bytes d, size_t r, uint32_t& nameOfs, uint32_t& descOfs, SjisString& name, SjisString& desc) {
    nameOfs = u32le(d, r + 4);
    descOfs = u32le(d, r + 8);
    if (nameOfs == 0 || nameOfs >= d.size() || descOfs == 0 || descOfs >= d.size()) throw FormatError("bad name/desc offset at " + hex(r));
    name = makeSjis(cstringAt(d, nameOfs));
    desc = makeSjis(cstringAt(d, descOfs));
}

}  // namespace

// Mirrors tools/game_formats.py parse_prmchar() and the relocation walk of FUN_00133480 (0x2d words/record).
PrmCharFile parsePrmCharOrThrow(Bytes d) {
    PrmCharFile f;
    size_t off = 0;
    bool haveEnemy = false;
    while (u32le(d, off) != 0) {
        PrmChar r;
        r.fileOffset = static_cast<uint32_t>(off);
        r.id = fixedString(d, off, 4);
        if (r.id.size() != 4 || (r.id[0] != 'C' && r.id[0] != 'E') || !isDigit(r.id[1]) || !isDigit(r.id[2])) throw FormatError("bad PRMCHAR id '" + r.id + "'");
        if (!haveEnemy && r.id[0] == 'E') {  // FUN_00133480: first record whose first char is 'E'
            haveEnemy = true;
            f.firstEnemy = f.records.size();
        }
        readStrings(d, off, r.nameOfs, r.descOfs, r.name, r.desc);
        r.hpMax = s32le(d, off + 0xc);
        r.vtMax = s32le(d, off + 0x10);
        r.statA = s32le(d, off + 0x14);
        r.statB = s32le(d, off + 0x18);
        r.f1c = s32le(d, off + 0x1c);
        r.f20 = s32le(d, off + 0x20);
        r.f24 = s32le(d, off + 0x24);
        for (size_t i = 0; i < 3; ++i) r.v28[i] = s32le(d, off + 0x28 + 4 * i);
        for (size_t i = 0; i < 3; ++i) r.v34[i] = s32le(d, off + 0x34 + 4 * i);
        for (size_t i = 0; i < 14; ++i) r.resist[i] = s32le(d, off + 0x40 + 4 * i);
        for (size_t i = 0; i < 3; ++i)
            for (size_t k = 0; k < 4; ++k) r.w78[i][k] = u16le(d, off + 0x78 + 8 * i + 2 * k);
        r.fleshHp = s32le(d, off + 0x90);
        r.fleshVt = s32le(d, off + 0x94);
        r.radius = f32le(d, off + 0x98);
        r.f9c = f32le(d, off + 0x9c);
        r.height = f32le(d, off + 0xa0);
        r.fa4 = f32le(d, off + 0xa4);
        r.throwSpread = f32le(d, off + 0xa8);
        r.iconHeight = f32le(d, off + 0xac);
        r.fb0 = f32le(d, off + 0xb0);
        f.records.push_back(std::move(r));
        off += kPrmCharStride;
    }
    if (f.records.empty()) throw FormatError("PRMCHAR has no records");
    if (!haveEnemy) f.firstEnemy = f.records.size();  // orig would scan past the terminator; no file does this
    for (const auto& r : f.records)
        if (r.nameOfs <= off) throw FormatError("PRMCHAR string pool does not follow the terminator");
    f.raw.assign(d.begin(), d.end());
    return f;
}

std::optional<PrmCharFile> parsePrmChar(Bytes data, std::string* error) {
    return guarded([&] { return parsePrmCharOrThrow(data); }, error);
}

// orig FUN_00133560 (work/decompiled/00133560.c)
const PrmChar* findPrmChar(const PrmCharFile& file, uint32_t id) {
    size_t i = ((id & 0xff) == 0x43 /*'C'*/) ? 0 : file.firstEnemy;
    for (; i < file.records.size(); ++i) {
        if (id4(file.records[i].id) == id) return &file.records[i];
    }
    return nullptr;
}

// orig FUN_001d2c90: `if (0xd < cls) return 0;` then the per-class stride multiply (see prm.hpp table).
uint32_t itemRecordStride(uint32_t cls) { return cls > 0xd ? 0u : kItemStride[cls]; }

std::string PrmItem::variantId(int i) const {
    if (i < 0 || i >= 16) return {};
    return id4ToString(static_cast<uint32_t>(variant[i]));
}

// Mirrors tools/game_formats.py parse_prmitem() and the relocation walk of FUN_001d3600.
PrmItemFile parsePrmItemOrThrow(Bytes d, ItemClass cls) {
    const uint32_t ci = static_cast<uint32_t>(cls);
    const uint32_t st = itemRecordStride(ci);
    if (st == 0) throw FormatError("bad item class");
    PrmItemFile f;
    f.cls = cls;
    size_t off = 0;
    while (u32le(d, off) != 0) {
        PrmItem r;
        r.fileOffset = static_cast<uint32_t>(off);
        r.id = fixedString(d, off, 4);
        if (r.id.size() != 4 || r.id.compare(0, 2, kItemPrefix[ci]) != 0 || !isDigit(r.id[2]) || !isDigit(r.id[3])) throw FormatError("bad item id '" + r.id + "'");
        readStrings(d, off, r.nameOfs, r.descOfs, r.name, r.desc);
        for (size_t i = 0; i < 4; ++i) r.dropWeight[i] = s32le(d, off + 0xc + 4 * i);
        for (size_t i = 0; i < 4; ++i) r.f1c[i] = s32le(d, off + 0x1c + 4 * i);
        for (size_t i = 0; i < 16; ++i) r.variant[i] = s32le(d, off + 0x2c + 4 * i);
        r.th = s32le(d, off + 0x6c);
        r.f70 = s32le(d, off + 0x70);
        if (st >= 0x78) {
            r.hasChance74 = true;
            r.chance74 = s32le(d, off + 0x74);
        }
        r.effectCount = st > 0x78 ? static_cast<int>(std::min<uint32_t>(2, (st - 0x78) / 8)) : 0;
        for (int i = 0; i < r.effectCount; ++i) {
            const size_t e = off + 0x78 + 8 * static_cast<size_t>(i);
            needBytes(d, e, 8);
            r.effects[i].kind = d[e];
            r.effects[i].type = d[e + 1];
            r.effects[i].value = s16le(d, e + 2);
            r.effects[i].x = s16le(d, e + 4);
            r.effects[i].count = s16le(d, e + 6);
        }
        r.tailOffset = 0x78 + 8u * static_cast<uint32_t>(r.effectCount);
        for (uint32_t o = r.tailOffset; o + 4 <= st; o += 4) r.tail.push_back(s32le(d, off + o));
        f.records.push_back(std::move(r));
        off += st;
    }
    if (f.records.empty()) throw FormatError("PRMITEM has no records");
    for (const auto& r : f.records)
        if (r.nameOfs <= off) throw FormatError("PRMITEM string pool does not follow the terminator");
    f.raw.assign(d.begin(), d.end());
    return f;
}

std::optional<PrmItemFile> parsePrmItem(Bytes data, ItemClass cls, std::string* error) {
    return guarded([&] { return parsePrmItemOrThrow(data, cls); }, error);
}

const PrmItem* itemRecord(const PrmItemFile& file, uint32_t idx) {
    if (itemRecordStride(static_cast<uint32_t>(file.cls)) == 0 || idx >= file.records.size()) return nullptr;
    return &file.records[idx];
}

std::string prmCharPath(bool japanese) { return japanese ? "DATA/PARAM/PRMCHAR.PRM" : "DATA/PARAM_EN/PRMCHAR.PRM"; }

std::string prmItemPath(bool japanese, uint32_t idx) {
    char buf[64];
    std::snprintf(buf, sizeof buf, japanese ? "DATA/PARAM/PRMITEM%02d.PRM" : "DATA/PARAM_EN/PRMITEM%02d.PRM", static_cast<int>(idx));
    return buf;
}

}  // namespace baroque::data
