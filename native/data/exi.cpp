#include "data/exi.hpp"

namespace baroque::data {

namespace {
template <size_t N>
void copyRaw(std::array<uint8_t, N>& dst, Bytes d, size_t o) {
    needBytes(d, o, N);
    std::memcpy(dst.data(), d.data() + o, N);
}
}  // namespace

// orig FUN_001e8b20 (work/decompiled/001e8b20.c) + the asserts of tools/game_formats.py parse_exi().
ExiFile parseExiOrThrow(Bytes d) {
    ExiFile f;
    if (d.size() < 0x10 || u32le(d, 0) != tag4("FORM") || u32le(d, 8) != tag4("EXIF")) throw FormatError("not a FORM/EXIF file");
    f.totalSize = u32le(d, 4);
    f.version = u32le(d, 0xc);
    if (f.version != 1 || f.totalSize > d.size()) throw FormatError("bad EXIF header");
    // The original walks `uVar11 = 0x10; while (uVar11 < total)`; the python tool bounds by min(size, total).
    size_t p = 0x10;
    const size_t limit = f.totalSize < d.size() ? f.totalSize : d.size();
    while (p + 8 <= limit) {
        ExiChunk c;
        c.tag = fixedString(d, p, 4);
        c.size = u32le(d, p + 4);
        c.offset = static_cast<uint32_t>(p);
        if (c.size < 8 || p + c.size > d.size()) throw FormatError("bad chunk size for " + c.tag);
        const size_t body = p + 8, n = c.size - 8;
        if (c.tag == "VTXS") {
            for (size_t o = 0; o + 16 <= n; o += 16) { ExiVertex v; copyRaw(v.raw, d, body + o); f.vertices.push_back(v); }
        } else if (c.tag == "KFMS") {
            for (size_t o = 0; o + 8 <= n; o += 8) f.keyframes.push_back({u32le(d, body + o), u32le(d, body + o + 4)});
        } else if (c.tag == "OBJS") {
            uint32_t vtxBase = 0;
            for (size_t o = 0; o < n / 0xb4 * 0xb4; o += 0xb4) {
                ExiObject obj;
                copyRaw(obj.raw, d, body + o);
                obj.vertexCount = u16le(d, body + o + 4);
                obj.flags7 = d[body + o + 7];
                for (size_t k = 0; k < 4; ++k) obj.vertexRef[k] = u32le(d, body + o + 0x1c + 4 * k);
                for (size_t k = 0; k < 17; ++k) {
                    obj.anim[k].a = s16le(d, body + o + 0x2c + 8 * k);
                    obj.anim[k].b = s16le(d, body + o + 0x2e + 8 * k);
                    obj.anim[k].keyframe = u32le(d, body + o + 0x30 + 8 * k);
                    if (obj.anim[k].b == 0) obj.anim[k].a = 0;
                    if (obj.anim[k].b == 1) obj.anim[k].a = 1;
                }
                const auto one = [&](size_t k) { return obj.anim[k].a == 1 && obj.anim[k].b == 1; };
                obj.allOnes8 = (one(15) && one(16) && one(13) && one(14)) ? 1u : 0u;  // shorts 0xa4..0xae, 0x94..0x9e
                obj.vertexBase = vtxBase;
                vtxBase += obj.vertexCount;
                f.objects.push_back(obj);
            }
        } else if (c.tag == "EMTS") {
            for (size_t o = 0; o < n / 0xac * 0xac; o += 0xac) {
                ExiEmitter e;
                copyRaw(e.raw, d, body + o);
                e.object = u32le(d, body + o + 4);
                for (size_t k = 0; k < 16; ++k) e.keyframe[k] = u32le(d, body + o + 0x28 + 8 * k);
                e.keyframe[16] = u32le(d, body + o + 0xa8);
                f.emitters.push_back(e);
            }
        } else if (c.tag == "SEQS") {
            for (size_t o = 0; o < n / 0x50 * 0x50; o += 0x50) {
                ExiSequence s;
                copyRaw(s.raw, d, body + o);
                s.ref = u32le(d, body + o + 0x40);
                s.refIsEmitter = (s.ref & 0x8000) != 0;
                s.refIndex = s.ref & 0xfff;
                f.sequences.push_back(s);
            }
        } else if (c.tag == "RSQS") {
            for (size_t o = 0; o + 8 <= n; o += 8) f.rsqs.push_back({u32le(d, body + o), u32le(d, body + o + 4)});
        } else if (c.tag == "EFCS") {
            for (size_t o = 0; o < n / 0x28 * 0x28; o += 0x28) {
                ExiEffect e;
                copyRaw(e.raw, d, body + o);
                e.sequence = u32le(d, body + o + 0x1c);
                e.rsqCount = s32le(d, body + o + 0x20);
                e.rsq = e.rsqCount < 1 ? 0u : u32le(d, body + o + 0x24);
                f.effects.push_back(e);
            }
        } else {
            throw FormatError("unknown EXI chunk " + c.tag);
        }
        f.chunks.push_back(c);
        p += c.size;
    }
    if (f.chunks.empty()) throw FormatError("EXI has no chunks");
    // Reference checks (the original dereferences relocated pointers; out-of-range indices would be memory errors).
    for (const auto& o : f.objects) {
        for (uint32_t r : o.vertexRef) if (r >= f.vertices.size()) throw FormatError("OBJS vertex ref out of range");
        for (const auto& a : o.anim) if (a.keyframe >= f.keyframes.size()) throw FormatError("OBJS keyframe ref out of range");
    }
    for (const auto& e : f.emitters) {
        if (e.object >= f.objects.size()) throw FormatError("EMTS object ref out of range");
        for (uint32_t k : e.keyframe) if (k >= f.keyframes.size()) throw FormatError("EMTS keyframe ref out of range");
    }
    for (const auto& s : f.sequences) {
        if (s.refIsEmitter ? s.refIndex >= f.emitters.size() : s.refIndex >= f.objects.size()) throw FormatError("SEQS ref out of range");
    }
    for (const auto& e : f.effects) {
        if (e.sequence >= f.sequences.size()) throw FormatError("EFCS sequence ref out of range");
        if (e.rsqCount >= 1 && e.rsq >= f.rsqs.size()) throw FormatError("EFCS rsq ref out of range");
    }
    return f;
}

std::optional<ExiFile> parseExi(Bytes data, std::string* error) {
    return guarded([&] { return parseExiOrThrow(data); }, error);
}

}  // namespace baroque::data
