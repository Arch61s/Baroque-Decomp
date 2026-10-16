#include "data/text_tables.hpp"

namespace baroque::data {

const uint16_t kGlyphCodesJp[kGlyphCountJp + 1] = {
#include "data/tables/glyph_codes_jp.inc"
    0x0000,
};
const uint16_t kGlyphCodesEn[kGlyphCountEn + 1] = {
#include "data/tables/glyph_codes_en.inc"
    0x0000,
};
const uint16_t kGlyphUnicodeJp[kGlyphCountJp] = {
#include "data/tables/glyph_unicode_jp.inc"
};
const uint16_t kGlyphUnicodeEn[kGlyphCountEn] = {
#include "data/tables/glyph_unicode_en.inc"
};
const GlyphMetric kGlyphMetricsEn[kGlyphMetricsCount] = {
#include "data/tables/glyph_metrics_en.inc"
};
const AsciiGlyph kAsciiGlyphMap[kAsciiGlyphCount] = {
#include "data/tables/ascii_glyph_map.inc"
};
const TextureEntry kTextureTable[kTextureCount] = {
#include "data/tables/texture_table.inc"
};

// orig FUN_001127b0 (work/decompiled/001127b0.c)
bool isSjisLead(uint8_t b) {
    if (b < 0x81 || b > 0x9f) {
        if (b < 0xe0) return false;
        return b < 0xfd;
    }
    return true;
}

namespace {
// Byte view of a code table as the original scans it (lead byte first).
inline uint8_t tableByte(const uint16_t* codes, size_t byteOfs) {
    const uint16_t c = codes[byteOfs >> 1];
    return (byteOfs & 1) ? static_cast<uint8_t>(c & 0xff) : static_cast<uint8_t>(c >> 8);
}
}  // namespace

// orig FUN_00112b30 (work/decompiled/00112b30.c): first byte offset of each lead byte.
void FontIndex::build(bool japanese) {
    const uint16_t* codes = japanese ? kGlyphCodesJp : kGlyphCodesEn;
    const size_t bound = japanese ? 0xa81 : 0xe7;
    uint8_t last = 0;
    for (size_t o = 0; o < bound; o += 2) {
        const uint8_t lead = tableByte(codes, o);
        if (last != lead) {
            last = lead;
            start[lead] = static_cast<int16_t>(o);
        }
    }
}

// orig FUN_00112800 (work/decompiled/00112800.c)
int FontIndex::lookup(const uint8_t* s, bool japanese) const {
    const uint16_t* codes = japanese ? kGlyphCodesJp : kGlyphCodesEn;
    const size_t bound = japanese ? 0xa81 : 0xe7;
    size_t o = static_cast<size_t>(static_cast<int32_t>(start[s[0]]));
    if (static_cast<int32_t>(start[s[0]]) < 0) return 0;  // never: the cache only holds non-negative offsets
    while (o < bound) {
        if (s[0] == tableByte(codes, o) && s[1] == tableByte(codes, o + 1)) return static_cast<int>(o >> 1);
        o += 2;
    }
    return 0;
}

int FontIndex::lookup(uint16_t code, bool japanese) const {
    const uint8_t s[2] = {static_cast<uint8_t>(code >> 8), static_cast<uint8_t>(code & 0xff)};
    return lookup(s, japanese);
}

// FUN_00112e80: fVar18 = (g & 7) << 4; tex = (g >> 3) / 7; fVar16 = ((g >> 3) % 7) * 0x12; uv = px / 128.
GlyphCell glyphCell(int g) {
    GlyphCell c{};
    c.px = static_cast<float>((g & 7) << 4);
    c.fontIndex = (g >> 3) / 7;
    c.py = static_cast<float>(((g >> 3) % 7) * 0x12);
    c.u0 = c.px / 128.0f;
    c.v0 = c.py / 128.0f;
    c.u1 = (c.px + 16.0f) / 128.0f;
    c.v1 = (c.py + 18.0f) / 128.0f;
    return c;
}

// FUN_00112e80 single-byte branch (work/decompiled/00112e80.c)
int asciiGlyph(char c, const FontIndex& index, bool japanese) {
    auto base = [&](size_t i) { return index.lookup(kAsciiGlyphMap[i].sjis, japanese); };
    if (c >= '0' && c <= '9') return base(0) + (c - '0');
    if (c >= 'a' && c <= 'z') return base(1) + (c - 'a');
    if (c >= 'A' && c <= 'Z') return base(2) + (c - 'A');
    if (c == ' ') return 0;
    for (int i = 3; i < kAsciiGlyphCount; ++i)
        if (kAsciiGlyphMap[i].ascii[0] == c) return base(static_cast<size_t>(i));
    if (c > 0x1f) return 0;
    return -1;
}

void appendUtf8(std::string& out, uint32_t cp) {
    if (cp < 0x80) out.push_back(static_cast<char>(cp));
    else if (cp < 0x800) {
        out.push_back(static_cast<char>(0xc0 | (cp >> 6)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3f)));
    } else if (cp < 0x10000) {
        out.push_back(static_cast<char>(0xe0 | (cp >> 12)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3f)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3f)));
    } else {
        out.push_back(static_cast<char>(0xf0 | (cp >> 18)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3f)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3f)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3f)));
    }
}

std::string sjisToUtf8(Bytes s) {
    std::string out;
    for (size_t i = 0; i < s.size(); ++i) {
        const uint8_t b = s[i];
        if (b < 0x80) { out.push_back(static_cast<char>(b)); continue; }
        if (b >= 0xa1 && b <= 0xdf) { appendUtf8(out, 0xff61u + (b - 0xa1u)); continue; }  // half-width katakana
        if (isSjisLead(b) && i + 1 < s.size()) {
            const uint16_t code = static_cast<uint16_t>((b << 8) | s[i + 1]);
            ++i;
            uint32_t cp = 0xfffd;
            for (int g = 0; g < kGlyphCountJp; ++g) if (kGlyphCodesJp[g] == code) { cp = kGlyphUnicodeJp[g]; break; }
            if (cp == 0xfffd) for (int g = 0; g < kGlyphCountEn; ++g) if (kGlyphCodesEn[g] == code) { cp = kGlyphUnicodeEn[g]; break; }
            appendUtf8(out, cp);
            continue;
        }
        appendUtf8(out, 0xfffd);
    }
    return out;
}

const char* textureName(int idx, bool japanese) {
    if (idx < 0 || idx >= kTextureCount) return nullptr;
    return japanese ? kTextureTable[idx].nameJp : kTextureTable[idx].nameEn;
}

}  // namespace baroque::data
