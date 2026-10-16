#include "data/map_formats.hpp"

#include <cstring>

namespace baroque::data {
namespace {

struct Reader {
    const std::vector<uint8_t>& b;
    std::string* err;
    bool fail(const std::string& m) const { if (err) *err = m; return false; }
    bool has(size_t off, size_t n) const { return off + n <= b.size() && off + n >= off; }
    uint8_t u8(size_t o) const { return b[o]; }
    uint32_t u32(size_t o) const {
        return static_cast<uint32_t>(b[o]) | (static_cast<uint32_t>(b[o + 1]) << 8) |
               (static_cast<uint32_t>(b[o + 2]) << 16) | (static_cast<uint32_t>(b[o + 3]) << 24);
    }
    int32_t s32(size_t o) const { return static_cast<int32_t>(u32(o)); }
    float f32(size_t o) const { const uint32_t v = u32(o); float f; std::memcpy(&f, &v, 4); return f; }
};

std::string hex(uint64_t v) { char buf[32]; std::snprintf(buf, sizeof buf, "0x%llx", static_cast<unsigned long long>(v)); return buf; }
size_t align(size_t v, size_t a) { return (v + a - 1) & ~(a - 1); }
bool asciiTag(const uint8_t* p, size_t n) {
    for (size_t i = 0; i < n; ++i) if (!((p[i] >= 32 && p[i] < 127) || p[i] == 0)) return false;
    return true;
}

} // namespace

std::string tagToString(uint32_t tag) {
    std::string s;
    for (int i = 0; i < 4; ++i) {
        const char c = static_cast<char>((tag >> (8 * i)) & 0xff);
        if (c == 0) break;
        s.push_back((c >= 32 && c < 127) ? c : '?');
    }
    return s;
}

// --------------------------------------------------------------------------------------------- FDT
// Layout from FUN_0011fa00 / FUN_00122d40 / FUN_00123dc0 / FUN_00121a00 / FUN_00121f80 / FUN_001d2f30.
std::optional<Fdt> parseFdt(const std::vector<uint8_t>& b, std::string* err) {
    Reader r{b, err};
    if (b.size() < 0x1c) { r.fail("FDT too short"); return std::nullopt; }
    Fdt f{};
    f.mapSet = r.u8(0);
    for (int i = 0; i < 4; ++i) f.roomCountChoice[i] = r.u8(1 + static_cast<size_t>(i));
    f.roomCount = r.u8(5);
    f.entityTableCount = r.u8(6);
    f.randomItemCount = r.u8(7);
    for (int i = 0; i < 12; ++i) f.itemCategoryWeight[i] = r.u8(8 + static_cast<size_t>(i));
    f.roomTableOffset = r.u32(0x14);
    f.entityTableOffset = r.u32(0x18);
    if (f.roomTableOffset != 0x1c) { r.fail("FDT roomTableOffset " + hex(f.roomTableOffset)); return std::nullopt; }
    if (f.entityTableOffset != align(0x1c + f.roomCount, 4)) { r.fail("FDT entityTableOffset " + hex(f.entityTableOffset)); return std::nullopt; }
    if (f.roomCount < 1 || f.roomCount > 16) { r.fail("FDT roomCount out of range"); return std::nullopt; }  // 0x50a640 holds 16 slots
    const size_t end = f.entityTableOffset + 8u * f.entityTableCount;
    if (!r.has(0, end)) { r.fail("FDT truncated"); return std::nullopt; }
    f.roomIds.assign(b.begin() + 0x1c, b.begin() + 0x1c + f.roomCount);
    for (size_t i = 0; i < f.roomIds.size(); ++i)
        for (size_t j = i + 1; j < f.roomIds.size(); ++j)
            if (f.roomIds[i] == f.roomIds[j]) { r.fail("FDT duplicate room id"); return std::nullopt; }
    for (size_t o = 0x1c + f.roomCount; o < f.entityTableOffset; ++o)
        if (b[o] != 0) { r.fail("FDT non-zero pad"); return std::nullopt; }
    for (uint32_t i = 0; i < f.entityTableCount; ++i) {
        const size_t o = f.entityTableOffset + 8u * i;
        if (!asciiTag(&b[o], 4)) { r.fail("FDT non-ascii tag"); return std::nullopt; }
        FdtEntity e{r.u32(o), r.u32(o + 4)};
        if (e.count >= 256) { r.fail("FDT entity count >= 256"); return std::nullopt; }
        f.entities.push_back(e);
    }
    f.parsedEnd = end;
    return f;
}

// --------------------------------------------------------------------------------------------- RDT
// Layout from FUN_00122e20 / FUN_00125420 / FUN_00125fc0 / FUN_00120a70 / FUN_0011edf0 / FUN_0011f160.
std::optional<Rdt> parseRdt(const std::vector<uint8_t>& b, std::string* err) {
    Reader r{b, err};
    if (b.size() < 0x20) { r.fail("RDT too short"); return std::nullopt; }
    Rdt d{};
    d.roomId = r.u8(0); d.w8 = r.u8(1); d.h8 = r.u8(2); d.doorCount = r.u8(3);
    d.spawnCount = r.u8(4); d.emitterCount = r.u8(5); d.lightCount = r.u8(6); d.flags = r.u8(7);
    d.flagId = r.u32(8);
    d.gridOffset = r.u32(0xc); d.doorOffset = r.u32(0x10); d.spawnOffset = r.u32(0x14);
    d.emitterOffset = r.u32(0x18); d.lightOffset = r.u32(0x1c);
    const size_t w = d.w8 * 8u, h = d.h8 * 8u;
    if (!(d.w8 >= 1 && d.w8 <= 6 && d.h8 >= 1 && d.h8 <= 5)) { r.fail("RDT size out of range"); return std::nullopt; }
    if (d.gridOffset != 0x20) { r.fail("RDT gridOffset " + hex(d.gridOffset)); return std::nullopt; }
    if (d.flags & ~0x07u) { r.fail("RDT flags " + hex(d.flags)); return std::nullopt; }
    if ((d.flagId != 0) != ((d.flags & 6) != 0)) { r.fail("RDT flagId/flags mismatch"); return std::nullopt; }
    size_t pos = d.gridOffset + w * h * 4;
    if (!r.has(0, pos)) { r.fail("RDT grid truncated"); return std::nullopt; }
    d.cells.resize(w * h);
    for (size_t i = 0; i < w * h; ++i) {
        const uint32_t c = r.u32(d.gridOffset + 4 * i);
        const uint32_t t = c & kCellTypeMask;
        if (t != 0 && t != kCellFloor) { r.fail("RDT cell type " + hex(c)); return std::nullopt; }   // 0x2000 only at runtime
        if (c & 0x0f) { r.fail("RDT cell low nibble " + hex(c)); return std::nullopt; }                // room index written at load
        if (c & 0xffff0000u & ~0x0f070000u) { r.fail("RDT cell high bits " + hex(c)); return std::nullopt; } // only bits 16-18, 24-27 occur
        if (t == 0 && c != 0) { r.fail("RDT void cell with bits " + hex(c)); return std::nullopt; }
        d.cells[i] = c;
    }
    // doors
    if (d.doorCount) { if (d.doorOffset != pos) { r.fail("RDT doorOffset"); return std::nullopt; } }
    else if (d.doorOffset != 0) { r.fail("RDT doorOffset != 0"); return std::nullopt; }
    if (!r.has(d.doorOffset, 4u * d.doorCount)) { r.fail("RDT doors truncated"); return std::nullopt; }
    for (uint32_t i = 0; i < d.doorCount; ++i) {
        const size_t o = d.doorOffset + 4u * i;
        RdtDoor dr{r.u8(o), r.u8(o + 1), r.u8(o + 2), r.u8(o + 3)};
        if (!(dr.dir < 4 && dr.x < w && dr.y < h)) { r.fail("RDT door out of range"); return std::nullopt; }
        if (dr.flags & ~0x0fu) { r.fail("RDT door flags " + hex(dr.flags)); return std::nullopt; }      // bit4 'connected' is runtime only
        const uint32_t c = d.cells[dr.y * w + dr.x];
        if ((c & kCellTypeMask) != kCellFloor) { r.fail("RDT door not on a floor cell"); return std::nullopt; }
        if ((((c >> 8) & 0xf) != 0) != ((dr.flags & 0xf) != 0)) { r.fail("RDT door level != cell level"); return std::nullopt; }
        d.doors.push_back(dr);
    }
    if (d.doorCount) pos = d.doorOffset + 4u * d.doorCount;
    // spawns (0x18)
    if (d.spawnCount) { if (d.spawnOffset != pos) { r.fail("RDT spawnOffset"); return std::nullopt; } }
    else if (d.spawnOffset != 0) { r.fail("RDT spawnOffset != 0"); return std::nullopt; }
    if (!r.has(d.spawnOffset, 0x18u * d.spawnCount)) { r.fail("RDT spawns truncated"); return std::nullopt; }
    for (uint32_t i = 0; i < d.spawnCount; ++i) {
        const size_t o = d.spawnOffset + 0x18u * i;
        if (!asciiTag(&b[o], 4)) { r.fail("RDT non-ascii spawn tag"); return std::nullopt; }
        d.spawns.push_back(RdtSpawn{r.u32(o), r.f32(o + 4), r.f32(o + 8), r.f32(o + 12), r.f32(o + 16), r.s32(o + 20)});
    }
    if (d.spawnCount) pos = d.spawnOffset + 0x18u * d.spawnCount;
    // emitters (0x18)
    if (d.emitterCount) { if (d.emitterOffset != pos) { r.fail("RDT emitterOffset"); return std::nullopt; } }
    else if (d.emitterOffset != 0) { r.fail("RDT emitterOffset != 0"); return std::nullopt; }
    if (!r.has(d.emitterOffset, 0x18u * d.emitterCount)) { r.fail("RDT emitters truncated"); return std::nullopt; }
    for (uint32_t i = 0; i < d.emitterCount; ++i) {
        const size_t o = d.emitterOffset + 0x18u * i;
        d.emitters.push_back(RdtEmitter{r.u32(o), r.f32(o + 4), r.f32(o + 8), r.f32(o + 12), r.f32(o + 16), r.f32(o + 20)});
    }
    if (d.emitterCount) pos = d.emitterOffset + 0x18u * d.emitterCount;
    // lights (0x20)
    if (d.lightCount) { if (d.lightOffset != pos) { r.fail("RDT lightOffset"); return std::nullopt; } }
    else if (d.lightOffset != 0) { r.fail("RDT lightOffset != 0"); return std::nullopt; }
    if (!r.has(d.lightOffset, 0x20u * d.lightCount)) { r.fail("RDT lights truncated"); return std::nullopt; }
    for (uint32_t i = 0; i < d.lightCount; ++i) {
        const size_t o = d.lightOffset + 0x20u * i;
        d.lights.push_back(RdtLight{r.u32(o), r.f32(o + 4), r.f32(o + 8), r.f32(o + 12), r.f32(o + 16), r.f32(o + 20), r.f32(o + 24), r.f32(o + 28)});
    }
    d.parsedEnd = d.lightCount ? d.lightOffset + 0x20u * d.lightCount : pos;
    return d;
}

// --------------------------------------------------------------------------------------------- CLS
// Layout from FUN_0013fd30 -> FUN_0020b6e0 (relocation) / FUN_0020b630 / FUN_00209fa0. File offsets are turned
// into indices here instead of pointers.
std::optional<Cls> parseCls(const std::vector<uint8_t>& b, std::string* err) {
    Reader r{b, err};
    constexpr size_t kFace = 0x44;
    if (b.size() < 0x64) { r.fail("CLS too short"); return std::nullopt; }
    if (!(r.u32(0) == 0 && r.u32(4) == 0x01000000 && r.u32(8) == 100)) { r.fail("bad CLS header"); return std::nullopt; }
    Cls c{};
    const uint32_t vertOff = r.u32(8); c.vertexCount = r.u32(0xc);
    const uint32_t vecOff = r.u32(0x10); c.vectorCount = r.u32(0x14);
    const uint32_t faceOff = r.u32(0x18); c.faceCount = r.u32(0x1c);
    c.bboxMin = {r.f32(0x20), r.f32(0x24), r.f32(0x28)};
    c.bboxMax = {r.f32(0x2c), r.f32(0x30), r.f32(0x34)};
    const uint32_t cellOff = r.u32(0x38); c.cellCount = r.u32(0x3c);
    for (int i = 0; i < 3; ++i) c.cellSize[i] = r.f32(0x40 + 4u * static_cast<uint32_t>(i));
    c.nx = r.u32(0x4c); c.ny = r.u32(0x50); c.nz = r.u32(0x54);
    c.originX = r.s32(0x58); c.originY = r.s32(0x5c); c.originZ = r.s32(0x60);
    if (vertOff != 0x64) { r.fail("CLS vertOff"); return std::nullopt; }
    if (vecOff != vertOff + 12ull * c.vertexCount) { r.fail("CLS vecOff"); return std::nullopt; }
    if (faceOff != align(vecOff + 12ull * c.vectorCount, 16)) { r.fail("CLS faceOff"); return std::nullopt; }
    if (!r.has(0, faceOff)) { r.fail("CLS truncated before faces"); return std::nullopt; }
    for (size_t o = vecOff + 12ull * c.vectorCount; o < faceOff; ++o)
        if (b[o] != 0) { r.fail("CLS non-zero pad before faces"); return std::nullopt; }
    if (cellOff != faceOff + kFace * static_cast<uint64_t>(c.faceCount)) { r.fail("CLS cellOff"); return std::nullopt; }
    if (static_cast<uint64_t>(c.nx) * c.ny * c.nz != c.cellCount) { r.fail("CLS grid dims != cellCount"); return std::nullopt; }
    if (!(c.ny == 1 && c.originY == 0)) { r.fail("CLS ny/originY"); return std::nullopt; }
    const size_t cellTableEnd = cellOff + 8ull * c.cellCount;
    if (!r.has(0, cellTableEnd)) { r.fail("CLS truncated before cell table end"); return std::nullopt; }

    c.vertices.resize(c.vertexCount);
    for (uint32_t i = 0; i < c.vertexCount; ++i) { const size_t o = vertOff + 12ull * i; c.vertices[i] = {r.f32(o), r.f32(o + 4), r.f32(o + 8)}; }
    c.vectors.resize(c.vectorCount);
    for (uint32_t i = 0; i < c.vectorCount; ++i) { const size_t o = vecOff + 12ull * i; c.vectors[i] = {r.f32(o), r.f32(o + 4), r.f32(o + 8)}; }

    auto vecIdx = [&](uint32_t p, uint32_t* out) {
        if (!(p >= vecOff && p < faceOff && (p - vecOff) % 12 == 0)) return false;
        *out = (p - vecOff) / 12; return true;
    };
    auto vertIdx = [&](uint32_t p, uint32_t* out) {
        if (!(p >= vertOff && p < vecOff && (p - vertOff) % 12 == 0)) return false;
        *out = (p - vertOff) / 12; return true;
    };
    auto faceIdx = [&](uint32_t p, int32_t* out) {
        if (p == 0) { *out = -1; return true; }
        if (!(p >= faceOff && p < cellOff && (p - faceOff) % kFace == 0)) return false;
        *out = static_cast<int32_t>((p - faceOff) / kFace); return true;
    };
    c.faces.resize(c.faceCount);
    for (uint32_t i = 0; i < c.faceCount; ++i) {
        const size_t o = faceOff + kFace * i;
        ClsFace& f = c.faces[i];
        f.type = static_cast<char>(b[o]);
        if (!(f.type == 'W' || f.type == 'F' || f.type == 'L')) { r.fail("CLS face type"); return std::nullopt; }
        if (!asciiTag(&b[o + 1], 7)) { r.fail("CLS face attr"); return std::nullopt; }
        std::memcpy(f.attr, &b[o + 1], 7); f.attr[7] = 0;
        if (!(r.u32(o + 8) == 0 && r.u32(o + 0x40) == 0)) { r.fail("CLS face runtime fields"); return std::nullopt; }
        if (!vecIdx(r.u32(o + 0xc), &f.normal)) { r.fail("CLS face normal ptr"); return std::nullopt; }
        for (uint32_t k = 0; k < 3; ++k) {
            if (!vertIdx(r.u32(o + 0x10 + 4 * k), &f.verts[k])) { r.fail("CLS face vertex ptr"); return std::nullopt; }
            if (!vecIdx(r.u32(o + 0x1c + 12 * k), &f.edges[k].vecA)) { r.fail("CLS face edge vecA"); return std::nullopt; }
            if (!vecIdx(r.u32(o + 0x20 + 12 * k), &f.edges[k].vecB)) { r.fail("CLS face edge vecB"); return std::nullopt; }
            if (!faceIdx(r.u32(o + 0x24 + 12 * k), &f.edges[k].neighbor)) { r.fail("CLS face neighbour ptr"); return std::nullopt; }
        }
    }
    // grid cells: {u32 faceListOffset, u32 count}; face lists follow the table in cell order (FUN_0020b6e0)
    size_t end = cellTableEnd;
    c.cells.resize(c.cellCount);
    for (uint32_t i = 0; i < c.cellCount; ++i) {
        const uint32_t lp = r.u32(cellOff + 8ull * i), lc = r.u32(cellOff + 8ull * i + 4);
        if (lp == 0) { if (lc != 0) { r.fail("CLS empty cell with count"); return std::nullopt; } c.cells[i] = {0, 0}; continue; }
        if (lp != end) { r.fail("CLS cell list offset " + hex(lp) + " != " + hex(end)); return std::nullopt; }
        if (!r.has(lp, 4ull * lc)) { r.fail("CLS cell list truncated"); return std::nullopt; }
        c.cells[i] = {static_cast<uint32_t>(c.faceRefs.size()), lc};
        for (uint32_t j = 0; j < lc; ++j) {
            int32_t fi;
            const uint32_t fp = r.u32(lp + 4ull * j);
            if (fp == 0 || !faceIdx(fp, &fi)) { r.fail("CLS cell face ptr"); return std::nullopt; }
            c.faceRefs.push_back(static_cast<uint32_t>(fi));
        }
        end += 4ull * lc;
    }
    c.parsedEnd = end;
    return c;
}

// orig FUN_001400b0 (work/decompiled/001400b0.c)
bool clsFaceWalkable(const ClsFace& f) {
    // original byte offsets: [0] type, [1] attr[0], [2] attr[1], [3] attr[2]
    if (f.attr[2] == 'S') return false;
    if (f.type != 'F') return false;
    if (f.attr[1] == 'E') return false;
    if (f.attr[1] == 'D') return false;
    if (f.attr[0] == 'W') return false;
    if (f.attr[0] == 'D') return false;
    return true;
}

} // namespace baroque::data
