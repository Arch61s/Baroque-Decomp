// Font glyph tables and lookups (GAME_FORMATS.md section 5): glyph code tables 0x48b470 (JP) / 0x48bf00 (EN),
// lead-byte start cache (FUN_00112b30 builds, FUN_00112800 looks up), EN proportional metrics 0x48bff0, the
// ASCII -> full-width mapping of the renderer FUN_00112e80, and the texture registry 0x48a280.
#pragma once
#include "data/common.hpp"
#include <array>

namespace baroque::data {

inline constexpr int kGlyphCountJp = 1344;   // 0xa80 bytes; the lookup loop bound 0xa81 also compares the 0 pad
inline constexpr int kGlyphCountEn = 115;    // 0xe6 bytes; loop bound 0xe7 likewise
extern const uint16_t kGlyphCodesJp[kGlyphCountJp + 1];   // big-endian Shift-JIS code (lead<<8 | trail), +1 = 0 pad
extern const uint16_t kGlyphCodesEn[kGlyphCountEn + 1];
extern const uint16_t kGlyphUnicodeJp[kGlyphCountJp];     // cp932 decoding of each code (0xfffd = none)
extern const uint16_t kGlyphUnicodeEn[kGlyphCountEn];

struct GlyphMetric { float xofs, yofs, advance; };     // 16-px units; scaled by (2*halfW)/16 in the renderer
// One row per EN glyph (0x48bff0..0x48c554; the next ELF table starts there). FUN_00112e80 only guards the index
// with `glyph < 0x159` (kGlyphMetricsLimit): glyphs >= 0x159 use the fixed advance 2*halfW; EN glyph indices are
// always < 115, so rows 115..344 (other tables in the original) are never read.
inline constexpr int kGlyphMetricsCount = kGlyphCountEn;
inline constexpr int kGlyphMetricsLimit = 0x159;
extern const GlyphMetric kGlyphMetricsEn[kGlyphMetricsCount];

struct AsciiGlyph { const char* ascii; uint16_t sjis; };  // '0','a','A' are bases, the rest map 1:1
inline constexpr int kAsciiGlyphCount = 27;
extern const AsciiGlyph kAsciiGlyphMap[kAsciiGlyphCount];

// orig FUN_001127b0: 1 when the byte is a Shift-JIS lead byte (0x81..0x9f, 0xe0..0xfc).
bool isSjisLead(uint8_t b);

// Per-lead-byte start cache: int16 byte offsets at 0x4e95be (lead < 0xa0) / 0x4e9540 (lead >= 0xa0); bss zero
// before FUN_00112b30 runs. Both arrays are disjoint for real lead bytes, so one 256-entry table models them.
struct FontIndex {
    std::array<int16_t, 256> start{};
    void build(bool japanese);                              // orig FUN_00112b30
    int lookup(const uint8_t* sjis, bool japanese) const;   // orig FUN_00112800: glyph index, 0 when not found
    int lookup(uint16_t code, bool japanese) const;         // same for a big-endian code
};

// Atlas placement of glyph g (FUN_00112e80): texture registry slot 1 + (g>>3)/7 (FONT00..), 8x7 cells of 16x18 px
// in a 128x128 atlas.
struct GlyphCell { int fontIndex; float u0, v0, u1, v1; float px, py; };
GlyphCell glyphCell(int g);

// ASCII handling of FUN_00112e80 for one byte: returns the glyph index, 0 for ' ' and other printable characters
// without a mapping, or -1 when the character is skipped (control codes). '\n' is handled by the caller.
int asciiGlyph(char c, const FontIndex& index, bool japanese);

// Shift-JIS -> UTF-8 through the glyph tables (ASCII and half-width katakana pass through; unknown -> U+FFFD).
std::string sjisToUtf8(Bytes sjis);
void appendUtf8(std::string& out, uint32_t codepoint);

// Texture registry 0x48a280: {nameEN, groupEN, nameJP, groupJP}; FUN_00106090 loads entry idx for the language into
// slot 0x4e6a70 + idx*4. Indices: 0 TEXASCII, 1..24 FONT00..23, 25 TEX_UI27, 26 SUBTRACT, 27 NOISE_T, 28 EXI/BQ.PNG, ...
struct TextureEntry { const char* nameEn; const char* groupEn; const char* nameJp; const char* groupJp; };
inline constexpr int kTextureCount = 286;
extern const TextureEntry kTextureTable[kTextureCount];
const char* textureName(int idx, bool japanese);   // nullptr when the language has no file for it

}  // namespace baroque::data
