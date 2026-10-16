#include "game/ui/text.hpp"

#include "game/render2d.hpp"
#include "game/ui/sprite.hpp"
#include "game/ui/strings.hpp"
#include "game/ui/texture_registry.hpp"
#include "game/ui/ui_tables.hpp"
#include <cstdio>
#include <cstring>
namespace baroque::game::ui {
namespace {
TextRenderer gText;
// FUN_001127b0: Shift-JIS lead byte test
bool isLead(unsigned char c) { return (c >= 0x81 && c <= 0x9f) || (c >= 0xe0 && c <= 0xfc); }
}
TextRenderer& text() { return gText; }
void centreOnScreen(float w, float h, float* x, float* y) {   // orig FUN_00167250 (work/decompiled/00167250.c)
    if (0.0f < w) *x = (512.0f - w) * 0.5f;
    if (0.0f < h) *y = (448.0f - h) * 0.5f;
}
// orig FUN_001129c0 (work/decompiled/001129c0.c) + FUN_00112b30 lead-byte start cache
void TextRenderer::init() {
    for (int i = 0; i < 256; ++i) leadStart_[i] = 0;
    const unsigned short* codes = language() == 1 ? kGlyphCodesJP : kGlyphCodesEN;
    const int n = language() == 1 ? 1344 : 115;
    unsigned char last = 0;
    for (int i = 0; i < n; ++i) {
        const unsigned char lead = static_cast<unsigned char>(codes[i] >> 8);
        if (lead != last) { last = lead; leadStart_[lead] = static_cast<short>(i * 2); }
    }
    for (Sprite*& g : glyphs_) g = nullptr;
    count_ = 0; x = 0; y = 0; z = 0; halfW = 8.0f; halfH = 8.0f; color = 0xffffffff; spacing = 1.0f; layerFlag = 0;
    proportional = (language() == 1) ? 0 : 1;
}
// orig FUN_00112800 (work/decompiled/00112800.c): linear search from the lead-byte start; byte offsets / 2 = glyph index
int TextRenderer::glyphIndex(const unsigned char* s) const {
    const unsigned short code = static_cast<unsigned short>((s[0] << 8) | s[1]);
    const unsigned short* codes = language() == 1 ? kGlyphCodesJP : kGlyphCodesEN;
    const int limitBytes = language() == 1 ? 0xa81 : 0xe7;
    for (int off = leadStart_[s[0]]; off < limitBytes - 1; off += 2) {
        if (codes[off / 2] == code) return off >> 1;
    }
    return 0;
}
// the ASCII -> full-width glyph switch of FUN_00112e80 / FUN_00113cf0; returns glyph index or -1 for control chars,
// sets *advance to the number of bytes consumed
int TextRenderer::asciiToGlyph(const char* p, int* advance) const {
    const unsigned char c = static_cast<unsigned char>(*p);
    *advance = 1;
    if (isLead(c)) { *advance = 2; return glyphIndex(reinterpret_cast<const unsigned char*>(p)); }
    auto mapped = [&](int tableIndex, int base) {
        unsigned char code[2] = {static_cast<unsigned char>(kAsciiMapCodes[tableIndex] >> 8), static_cast<unsigned char>(kAsciiMapCodes[tableIndex] & 0xff)};
        return glyphIndex(code) + static_cast<int>(c) - base;
    };
    if (c >= '0' && c <= '9') return mapped(0, '0');
    if (c >= 'a' && c <= 'z') return mapped(1, 'a');
    if (c >= 'A' && c <= 'Z') return mapped(2, 'A');
    if (c == ' ') return 0;
    static const char kPunct[] = "+-*/=.,:';\\?!#$%&\"()<>@|";   // table order 0x42ca58..0x42cb10
    for (int i = 0; kPunct[i] != '\0'; ++i) if (c == static_cast<unsigned char>(kPunct[i])) return mapped(3 + i, c);
    if (c > 0x1f) return 0;
    return -1;
}
void TextRenderer::draw(const char* fmt, ...) {
    va_list ap; va_start(ap, fmt); drawV(fmt, ap); va_end(ap);
}
// orig FUN_00112e80 (work/decompiled/00112e80.c), mode 0 path (UI layer 0x4e68c0)
void TextRenderer::drawV(const char* fmt, va_list ap) {
    char buf[288];
    std::vsnprintf(buf, sizeof buf, fmt, ap);
    float cx = x, cy = y;
    const float cz = z;
    const char* p = buf;
    while (*p != '\0' && count_ < kMaxGlyphs) {
        if (*p == '\n') { ++p; cx = x; cy = halfH * 2.0f + cy + 0.0f; continue; }
        int adv = 1;
        const int g = asciiToGlyph(p, &adv);
        p += adv;
        if (g < 0) continue;
        if (mode != 0) {
            if (!textures().isLoaded(1 + (g >> 3) / 7)) break;   // FUN_00107130: glyph texture loaded
            const int before = count_;
            drawGlyphMode1(g, cx, cy, cz);
            if (count_ == before) break;
            continue;
        }
        const float gu = static_cast<float>((g & 7) << 4);
        const int texSlot = (g >> 3) / 7;
        const float gv = static_cast<float>(((g >> 3) % 7) * 0x12);
        Sprite* s = sprites().alloc(1 + texSlot, layerFlag);    // 0x4e6a74 + slot*4 = texture ids 1..
        if (s == nullptr) break;
        glyphMode_[count_] = 0;
        glyphs_[count_++] = s;
        // PORT: the glyph cell is sampled half a texel inside its 16x18 rect. At the exact cell edges the bilinear
        // filter mixed in the neighbouring glyphs of the font page, which showed as small dashes above / beside
        // letters drawn larger than 1:1 (talk windows, captions).
        constexpr float kInset = 0.5f;
        s->u0 = (gu + kInset) / 128.0f; s->v0 = (gv + kInset) / 128.0f;
        s->u1 = (gu + 16.0f - kInset) / 128.0f; s->v1 = (gv + 18.0f - kInset) / 128.0f;
        s->cx = cx; s->cy = cy; s->z = cz;
        if (language() == 1) s->cx = s->cx + halfW;
        else if (proportional == 1) {
            const float px = s->cx + ((halfW * 2.0f) / 16.0f) * kGlyphMetricsEN[g][0] + halfW + 0.0f;
            s->cx = static_cast<float>(static_cast<int>(px));
        } else s->cx = s->cx + halfW;
        if (language() == 1) s->cy = s->cy + halfH;
        else s->cy = s->cy + halfH + kGlyphMetricsEN[g][1];
        s->color = color;
        s->hw = halfW; s->hh = halfH;
        if (language() == 1) cx = halfW * 2.0f * spacing + cx + 0.0f;
        else if (g < 0x159 && proportional == 1) cx = static_cast<float>(static_cast<int>(spacing * ((halfW * 2.0f) / 16.0f) * kGlyphMetricsEN[g][2] + cx + 0.0f));
        else cx = halfW * 2.0f + cx + 0.0f;
    }
    x = cx; y = cy; z = cz;   // the object keeps the cursor (param_1[0x602..0x604] written back)
}
// orig FUN_00112e80 (work/decompiled/00112e80.c), mode 1 path: the glyph is a 4-vertex strip on the polygon layer
// 0x4e6990 (FUN_00113c60 texture list, FUN_00114c30 node, FUN_00114bf0 0x6c-byte buffer, FUN_001051d0), drawn
// rotated (screen x follows texture v, screen y follows -u) and advancing upwards (y decreases). The position
// writes keep the original's slips: language 1 stores x then overwrites +0x60 with y (+0x64 is left unset: 0 here,
// UNVERIFIED stale buffer word on the console), and EN without proportional metrics stores y = x.
void TextRenderer::drawGlyphMode1(int g, float& cx, float& cy, float cz) {
    const float gu = static_cast<float>((g & 7) << 4);
    const int texId = 1 + (g >> 3) / 7;                        // *(0x4e6a74 + slot*4)
    const float gv = static_cast<float>(((g >> 3) % 7) * 0x12);
    Polygon2D* poly = polygons().create(texId, layerFlag);    // FUN_00113c60(0x4e6990, tex, [0x608]) + FUN_00114c30
    if (poly == nullptr) return;
    GlyphBuf* b = new GlyphBuf();                             // FUN_00114bf0 (UNVERIFIED: pool contents zeroed)
    *b = GlyphBuf{};
    polys_[count_] = poly;
    bufs_[count_] = b;
    glyphMode_[count_] = mode;
    ++count_;
    const bool en = language() == 0;
    if (language() == 1) b->pos[0] = cx;
    else if (en) b->pos[0] = static_cast<float>(static_cast<int>(cx - kGlyphMetricsEN[g][1]));
    if (language() == 1) b->pos[0] = cy;
    else if (en) {
        if (proportional == 1) b->pos[1] = (cy + 0.0f) - ((halfW * 2.0f) / 16.0f) * kGlyphMetricsEN[g][0];
        else b->pos[1] = cx;
    }
    b->pos[2] = cz;
    const float h = ((halfH * 2.0f) / 18.0f) * 18.0f;
    const float w = ((halfW * 2.0f) / 16.0f) * 16.0f;
    // PORT: half-texel inset as in the sprite path above (no bleed from the neighbouring glyphs)
    const float uR = (gu + 15.5f) / 128.0f, vB = (gv + 17.5f) / 128.0f;
    b->vert[0] = 0.0f; b->vert[1] = 0.0f; b->vert[2] = 0.0f;
    b->vert[3] = 0.0f; b->vert[4] = h; b->vert[5] = 0.0f;
    b->vert[6] = w; b->vert[7] = 0.0f; b->vert[8] = 0.0f;
    b->vert[9] = w; b->vert[10] = h; b->vert[11] = 0.0f;
    const float uL = (gu + 0.5f) / 128.0f, vT = (gv + 0.5f) / 128.0f;
    b->uv[6] = uL; b->uv[7] = vB;
    b->uv[4] = uR; b->uv[5] = vB;
    b->uv[2] = uL; b->uv[3] = vT;
    b->uv[0] = uR; b->uv[1] = vT;
    b->colour[0] = b->colour[1] = b->colour[2] = b->colour[3] = color;
    polygonSetVertsColours(*poly, reinterpret_cast<const PolyVert*>(b->vert), b->uv, b->colour, 4, nullptr);
    poly->x = b->pos[0];
    poly->y = b->pos[1];
    poly->z = b->pos[2];
    if (language() == 1) cy = (cy + 0.0f) - halfW * 2.0f * spacing;
    else if (en) {
        if (g < 0x159 && proportional == 1)
            cy = (cy + 0.0f) - spacing * ((halfW * 2.0f) / 16.0f) * kGlyphMetricsEN[g][2];
        else cy = (cy + 0.0f) - halfW * 2.0f;
    }
}

float TextRenderer::measure(const char* fmt, ...) {
    va_list ap; va_start(ap, fmt); const float w = measureV(fmt, ap); va_end(ap); return w;
}
// orig FUN_00113cf0 (work/decompiled/00113cf0.c): per-line widths (up to 4 lines), returns the widest
float TextRenderer::measureV(const char* fmt, va_list ap) {
    char buf[128];
    std::vsnprintf(buf, sizeof buf, fmt, ap);
    float widths[4] = {0, 0, 0, 0};
    unsigned line = 0;
    const char* p = buf;
    while (*p != '\0') {
        if (*p == '\n') { ++p; line = (line + 1) & 0xff; continue; }
        int adv = 1;
        const int g = asciiToGlyph(p, &adv);
        p += adv;
        if (g < 0) continue;
        if (line > 3) continue;   // UNVERIFIED: the original indexes a 12-float stack buffer without a bound
        if (language() == 1) widths[line] = halfW * 2.0f * spacing + widths[line] + 0.0f;
        else if (g < 0x159 && proportional == 1) widths[line] = widths[line] + static_cast<float>(static_cast<int>(spacing * ((halfW * 2.0f) / 16.0f) * kGlyphMetricsEN[g][2]));
        else widths[line] = halfW * 2.0f + widths[line] + 0.0f;
    }
    unsigned best = 0;
    for (unsigned i = 0; i < 4; ++i) if (widths[i] > widths[best]) best = i;
    return widths[best];
}
// orig FUN_00112c60 (work/decompiled/00112c60.c)
void TextRenderer::endOfTick() {
    for (int i = 0; i < count_; ++i) {
        if (glyphMode_[i] == 0) {
            sprites().destroy(glyphs_[i]);
            glyphs_[i] = nullptr;
        } else {
            polygons().destroy(polys_[i]);                    // unlink + FUN_001053a0
            polys_[i] = nullptr;
            delete bufs_[i];                                  // FUN_001149a0
            bufs_[i] = nullptr;
        }
    }
    count_ = 0;
}
}
