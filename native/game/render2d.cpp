// 2-D sprite / polygon presentation (see render2d.hpp).
#include "game/render2d.hpp"

#include "game/ui/sprite.hpp"
#include "game/ui/strings.hpp"
#include "game/ui/text.hpp"
#include "game/ui/texture_registry.hpp"
#include "game/ui/ui_tables.hpp"
#include "platform/platform.hpp"

#include "rlgl.h"

#include <algorithm>
#include <cmath>
#include <vector>
#include <unordered_map>

namespace baroque::game {

namespace {

Renderer2D gRenderer;
PolygonLayer2D gPolygons;

const char* kVs = R"GLSL(
#version 330
in vec3 vertexPosition;
in vec2 vertexTexCoord;
in vec4 vertexColor;
uniform mat4 mvp;
out vec2 uv;
out vec4 col;
void main() { uv = vertexTexCoord; col = vertexColor; gl_Position = mvp * vec4(vertexPosition, 1.0); }
)GLSL";

// mode 1 = textured (TME=1, TFX modulate), 0 = untextured (TME=0: colour only)
const char* kFs = R"GLSL(
#version 330
in vec2 uv;
in vec4 col;
uniform sampler2D texture0;
uniform int mode;
out vec4 finalColor;
void main() {
    vec4 cf = col * 255.0;                       // GS vertex colour units
    if (mode == 1) {
        vec4 ct = texture(texture0, uv) * 255.0; // PNG texel, alpha kept 8-bit
        vec3 rgb = min(floor(ct.rgb * cf.rgb / 128.0), vec3(255.0));
        float a = min(floor(ct.a * cf.a / 128.0), 255.0);
        if (a <= 2.0) discard;                   // alpha test of FUN_001020a0: GREATER, AREF (int)(0x3c808081 * 128) = 2
        finalColor = vec4(rgb / 255.0, min(a / 128.0, 1.0));
    } else {
        if (cf.a <= 2.0) discard;
        finalColor = vec4(min(cf.rgb, vec3(255.0)) / 255.0 * (255.0 / 128.0), min(cf.a / 128.0, 1.0));
        finalColor.rgb = min(finalColor.rgb, vec3(1.0));
    }
}
)GLSL";

// orig FUN_001026e0 (work/decompiled/001026e0.c), blend part, selected per sprite by FUN_001032e0 from the layer flag +8:
//   3: GS ALPHA A=2 B=1 C=0 D=1 (FIX 0x80) -> Cd * (1 - As)      = (ZERO, ONE_MINUS_SRC_ALPHA)
//   2: igBlendFunction (4, 1)                                    = (SRC_ALPHA, ONE), additive, z-write off
//   1: (4, 5)                                                    = (SRC_ALPHA, ONE_MINUS_SRC_ALPHA), z-write off
//   0: (4, 5), z-write on (0x378(1)); flags 1..3 run 0x378(0) = ZBUF.ZMSK 1 (FUN_00383cc0 -> FUN_002488f0(func, 1)).
//      The depth test is on for the whole pass (FUN_001020a0 0x368(1)), depth cleared first (FUN_001050f0 +0x298(2)).
// The shader's alpha is As / 128 (0x80 = 1.0), as the GS uses it.
void spriteBlend(int flag) {
    if (flag == 3) {
        rlSetBlendFactors(RL_ZERO, RL_ONE_MINUS_SRC_ALPHA, RL_FUNC_ADD);
        rlSetBlendMode(RL_BLEND_CUSTOM);
    } else if (flag == 2) {
        rlSetBlendFactors(RL_SRC_ALPHA, RL_ONE, RL_FUNC_ADD);
        rlSetBlendMode(RL_BLEND_CUSTOM);
    } else {
        rlSetBlendMode(RL_BLEND_ALPHA);
    }
}

// one entry of the merged draw list: a sprite of 0x4e68c0 or a polygon node of 0x4e6990
struct DrawItem {
    float z;
    const ui::SpriteDraw* sprite;
    const Polygon2D* poly;
    size_t age = 0;             // list +4 only: position in the list (0 = newest, FUN_00107070 links at the head)
    int flag = 0;               // layer flag (node +8)
    int tex = ui::kTexNone;     // texture id (node +0xc; 0x129 = untextured)
};

// orig FUN_00102b30 (work/decompiled/00102b30.c): z bucket of a list +4 sprite, (int) z clamped to 0..0x1f
int zBucket(float z) {
    int b = static_cast<int>(z);   // cvt.w.s (truncation)
    if (b < 0x20) {
        if (b < 0) b = 0;
    } else {
        b = 0x1f;
    }
    return b;
}

}  // namespace

Renderer2D& renderer2d() { return gRenderer; }
PolygonLayer2D& polygons() { return gPolygons; }

// ---- PORT: keyboard button prompts (render2d.hpp keycapLabel, game/ui/key_prompt.*) --------------------------------
namespace {
KeycapLabelFn gKeycapFn = nullptr;
// The PS2 button glyph cells FUN_00166eb0 draws (table 0x491ef0, texels of the 128x128 sheets) and their pad bits.
// Every glyph sprite in the game uses one of these cells, whoever creates it (FUN_00166eb0 callers, the item screen's
// sort hint), so the cell identifies the button.
struct GlyphCell { int tex; float u0, v0, u1, v1; int bit; };
const GlyphCell kGlyphCells[8] = {
    {0x28, 0, 0, 16, 17, 0x20},    // TEX_UI05 circle
    {0x28, 16, 0, 32, 17, 0x40},   // cross
    {0x28, 32, 0, 48, 17, 0x80},   // square
    {0x28, 48, 0, 64, 17, 0x10},   // triangle
    {0x27, 94, 26, 112, 41, 0x4},  // TEX_UI04 L1
    {0x27, 94, 11, 112, 26, 0x8},  // R1
    {0x27, 94, 58, 112, 75, 0x1},  // L2
    {0x27, 94, 41, 112, 58, 0x2},  // R2
};
int glyphCellBit(const ui::SpriteDraw& s, const Texture2D& t) {
    if (s.texId != 0x28 && s.texId != 0x27) return 0;
    auto same = [](float a, float b) { return std::fabs(a - b) < 0.05f; };
    const float tw = float(t.width), th = float(t.height);
    for (const GlyphCell& c : kGlyphCells) {
        if (c.tex != s.texId) continue;
        if (same(s.u0 * tw, c.u0) && same(s.v0 * th, c.v0) && same(s.u1 * tw, c.u1) && same(s.v1 * th, c.v1)) return c.bit;
    }
    return 0;
}
// Keycap in place of a glyph sprite: a rounded box (border ring + dark fill) centred on the glyph, the glyph's height,
// as wide as the label needs (at least square), and the label in the game font (FUN_00112e80 glyph cells / EN
// metrics). Alpha follows the sprite (textured glyph opacity = min(255 * A / 128, 255) / 128, so the untextured parts
// use min(A * 255 / 128, 0x80)). All in raster units, so it is drawn at the window's resolution.
void drawKeycap(const ui::SpriteDraw& s, const char* label, float dx, const Shader& shader, int locMode, const Texture2D& white) {
    const float h = s.hh * 2.0f, w = s.hw * 2.0f;
    const bool jp = ui::language() == 1;
    ui::TextRenderer& tr = ui::text();
    struct Ch { int g; float adv; };
    Ch chars[16];
    int n = 0;
    float units = 0.0f;   // label width in font units (16 = one full-width cell)
    for (const char* p = label; *p != '\0' && n < 16;) {
        int adv = 1;
        const int g = tr.glyphForChar(p, &adv);
        p += adv;
        if (g < 0) continue;
        const float a = (!jp && g < 0x159) ? ui::kGlyphMetricsEN[g][2] : 16.0f;
        chars[n++] = {g, a};
        units += a;
    }
    float k = h * 0.7f / 18.0f;            // font cell scale (glyph cell 16x18)
    const float pad = std::max(1.5f, h * 0.16f);
    const float maxW = std::max(w * 2.6f, h * 2.6f);
    if (units * k + pad * 2.0f > maxW && units > 0.0f) k = (maxW - pad * 2.0f) / units;
    const float labelW = units * k;
    const float boxW = std::max(std::max(w, h), labelW + pad * 2.0f), boxH = h;
    const float x0 = s.cx + dx - boxW * 0.5f, y0 = s.cy - boxH * 0.5f, x1 = x0 + boxW, y1 = y0 + boxH;
    const unsigned A = s.color >> 24;
    const unsigned aU = std::min(A * 255u / 128u, 0x80u);
    const unsigned rgb = s.color & 0xffffffu;
    auto chan = [&](int shift, float f) { return (unsigned char)std::min(255.0f, float((rgb >> shift) & 0xff) * f); };
    // perimeter of the rounded rectangle at radius r (4 segments per corner), clockwise from the top-left corner
    const float b = std::max(0.75f, h * 0.075f), R = std::max(std::min(boxW, boxH) * 0.24f, b + 0.25f);
    constexpr int kSeg = 4, kPts = (kSeg + 1) * 4;
    Vector2 outer[kPts], inner[kPts];
    const float ccx[4] = {x0 + R, x1 - R, x1 - R, x0 + R}, ccy[4] = {y0 + R, y0 + R, y1 - R, y1 - R};
    for (int c = 0, i = 0; c < 4; ++c) {
        for (int sg = 0; sg <= kSeg; ++sg, ++i) {
            const float ang = (180.0f + 90.0f * float(c) + 90.0f * float(sg) / float(kSeg)) * 3.14159265f / 180.0f;
            outer[i] = {ccx[c] + std::cos(ang) * R, ccy[c] + std::sin(ang) * R};
            inner[i] = {ccx[c] + std::cos(ang) * (R - b), ccy[c] + std::sin(ang) * (R - b)};
        }
    }
    int mode = 0;
    SetShaderValue(shader, locMode, &mode, SHADER_UNIFORM_INT);
    rlBegin(RL_TRIANGLES);
    rlSetTexture(white.id);
    // dark fill (fan over the inner outline)
    const unsigned char fa = (unsigned char)(aU * 7u / 8u);
    const float mx = (x0 + x1) * 0.5f, my = (y0 + y1) * 0.5f;
    for (int i = 0; i < kPts; ++i) {
        const Vector2& p = inner[i];
        const Vector2& q = inner[(i + 1) % kPts];
        rlColor4ub(0x0c, 0x0c, 0x0e, fa);
        rlTexCoord2f(0, 0); rlVertex3f(mx, my, s.z);
        rlTexCoord2f(0, 0); rlVertex3f(p.x, p.y, s.z);
        rlTexCoord2f(0, 0); rlVertex3f(q.x, q.y, s.z);
    }
    // border ring (glyph colour, a little dimmer)
    const unsigned char br = chan(0, 0.8f), bg = chan(8, 0.8f), bb = chan(16, 0.8f), ba = (unsigned char)aU;
    for (int i = 0; i < kPts; ++i) {
        const int j = (i + 1) % kPts;
        const Vector2 tri[6] = {outer[i], outer[j], inner[i], inner[i], outer[j], inner[j]};
        for (const Vector2& v : tri) {
            rlColor4ub(br, bg, bb, ba);
            rlTexCoord2f(0, 0);
            rlVertex3f(v.x, v.y, s.z);
        }
    }
    rlEnd();
    rlDrawRenderBatchActive();
    // label (textured font glyphs, modulated by the sprite's colour and alpha like a glyph of FUN_00112e80)
    mode = 1;
    SetShaderValue(shader, locMode, &mode, SHADER_UNIFORM_INT);
    float cx = mx - labelW * 0.5f;
    const float cellW = 16.0f * k, cellH = 18.0f * k;
    for (int i = 0; i < n; ++i) {
        const int g = chars[i].g;
        const Texture2D* ft = ui::textures().texture(1 + (g >> 3) / 7);
        if (ft != nullptr && ft->width > 0) {
            const float gu = float((g & 7) << 4), gv = float(((g >> 3) % 7) * 0x12);
            const float ox = (!jp && g < 0x159) ? ui::kGlyphMetricsEN[g][0] * k : 0.0f;
            const float oy = (!jp && g < 0x159) ? ui::kGlyphMetricsEN[g][1] * k : 0.0f;
            const float gx0 = std::floor(cx + ox), gy0 = my - cellH * 0.5f + oy;
            const float tw = float(ft->width), th = float(ft->height);
            rlBegin(RL_QUADS);
            rlSetTexture(ft->id);
            rlColor4ub(chan(0, 1.0f), chan(8, 1.0f), chan(16, 1.0f), (unsigned char)A);
            rlTexCoord2f(gu / tw, gv / th); rlVertex3f(gx0, gy0, s.z);
            rlTexCoord2f(gu / tw, (gv + 18.0f) / th); rlVertex3f(gx0, gy0 + cellH, s.z);
            rlTexCoord2f((gu + 16.0f) / tw, (gv + 18.0f) / th); rlVertex3f(gx0 + cellW, gy0 + cellH, s.z);
            rlTexCoord2f((gu + 16.0f) / tw, gv / th); rlVertex3f(gx0 + cellW, gy0, s.z);
            rlEnd();
        }
        cx += chars[i].adv * k;
    }
    rlDrawRenderBatchActive();
}
}  // namespace
void setKeycapLabelProvider(KeycapLabelFn fn) { gKeycapFn = fn; }
const char* keycapLabel(int padBit) { return gKeycapFn != nullptr ? gKeycapFn(padBit) : nullptr; }

// orig FUN_001051d0 (work/decompiled/001051d0.c)
void polygonSetVerts(Polygon2D& p, const PolyVert* src, const float* uv, uint32_t colour, uint16_t count, PolyVert* dst) {
    p.flags = 0;
    p.src = src;
    p.uv = uv;
    p.colour = colour;
    p.count = count;
    p.dst = dst;
    if (dst != nullptr) for (uint16_t i = 0; i < count; ++i) dst[i] = src[i];
}

// orig FUN_001051d0 with a3 = per-vertex colour array (flag bit 0 stays clear)
void polygonSetVertsColours(Polygon2D& p, const PolyVert* src, const float* uv, const uint32_t* colours, uint16_t count,
                            PolyVert* dst) {
    polygonSetVerts(p, src, uv, 0, count, dst);
    p.vertexColours = colours;
}

// orig FUN_00105240 (work/decompiled/00105240.c) -> FUN_00207090 (rotation matrix; FUN_002386a0 = sinf,
// FUN_00238400 = cosf) -> FUN_001052c0 (FUN_00206b40 per vertex: v * M, translation row 0)
void polygonRotate(Polygon2D& p, const float anglesDeg[3]) {
    const float ax = (anglesDeg[0] * 3.1415927f) / 180.0f;
    const float ay = (anglesDeg[1] * 3.1415927f) / 180.0f;
    const float az = (anglesDeg[2] * 3.1415927f) / 180.0f;
    const float s1 = std::sin(ax), s2 = std::sin(ay), s3 = std::sin(az);
    const float c4 = std::cos(ax), c5 = std::cos(ay), c6 = std::cos(az);
    float m[16];
    m[0] = s1 * s2 * s3 + c5 * c6 + 0.0f;
    m[1] = c4 * s3;
    m[2] = s1 * c5 * s3 - s2 * c6;
    m[3] = 0.0f;
    m[4] = s1 * s2 * c6 - c5 * s3;
    m[5] = c4 * c6;
    m[6] = s1 * c5 * c6 + s2 * s3 + 0.0f;
    m[7] = 0.0f;
    m[8] = c4 * s2;
    m[9] = -s1;
    m[10] = c4 * c5;
    m[11] = 0.0f;
    m[12] = 0.0f; m[13] = 0.0f; m[14] = 0.0f; m[15] = 1.0f;
    if (p.dst == nullptr || p.src == nullptr) return;
    for (uint16_t i = 0; i < p.count; ++i) {
        const PolyVert& s = p.src[i];
        PolyVert& d = p.dst[i];
        d.x = m[12] + s.z * m[8] + s.x * m[0] + s.y * m[4];
        d.y = m[13] + s.z * m[9] + s.x * m[1] + s.y * m[5];
        d.z = m[14] + s.z * m[10] + s.x * m[2] + s.y * m[6];
    }
}

// FUN_00107130(texture loaded: 0x129 always) / FUN_00113c60(0x4e6990, 0x129, 0) / FUN_00114c30 / FUN_00107070
Polygon2D* PolygonLayer2D::create(int texId, int layerFlag) {
    Polygon2D* p = new Polygon2D();
    p->texId = texId;
    p->layerFlag = layerFlag;
    p->anchor = ui::sprites().anchorScope();   // PORT: widescreen anchor (presentation only)
    nodes_.insert(nodes_.begin(), p);
    return p;
}
void PolygonLayer2D::destroy(Polygon2D* p) {
    if (p == nullptr) return;
    auto it = std::find(nodes_.begin(), nodes_.end(), p);
    if (it != nodes_.end()) nodes_.erase(it);
    delete p;
}

Viewport43 viewport43(int screenW, int screenH) {
    int w = screenW, h = screenW * 3 / 4;
    if (h > screenH) {
        h = screenH;
        w = screenH * 4 / 3;
    }
    return {(screenW - w) / 2, (screenH - h) / 2, w, h};
}

namespace {
// default provider: the plain 4:3 presentation (area = box)
ScreenLayout layout43(int screenW, int screenH) {
    const Viewport43 b = viewport43(screenW, screenH);
    ScreenLayout L;
    L.x = L.boxX = b.x;
    L.y = L.boxY = b.y;
    L.w = L.boxW = b.w;
    L.h = L.boxH = b.h;
    return L;
}
ScreenLayoutFn gLayoutFn = &layout43;

// flat full-screen quad of the original (fade FUN_00106c00 -8..520, pause dim 0..512, event colour quad / letterbox
// bars / item-screen backdrop -5..517): untextured, spanning the whole raster width, taller than a rule line
bool fullBleed(const ui::SpriteDraw& s) {
    return s.texId == ui::kTexNone && s.cx - s.hw <= 0.0f && s.cx + s.hw >= 512.0f && s.hh * 2.0f >= 16.0f;
}
}  // namespace

void setScreenLayoutProvider(ScreenLayoutFn fn) { gLayoutFn = fn != nullptr ? fn : &layout43; }
ScreenLayout screenLayout(int screenW, int screenH) { return gLayoutFn(screenW, screenH); }

bool Renderer2D::init() {
    shader_ = LoadShaderFromMemory(kVs, kFs);
    locMode_ = GetShaderLocation(shader_, "mode");
    Image img = GenImageColor(1, 1, WHITE);
    white_ = LoadTextureFromImage(img);
    UnloadImage(img);
    return shader_.id != 0;
}

void Renderer2D::shutdown() {
    UnloadShader(shader_);
    UnloadTexture(white_);
}

namespace {
// PORT: 21:9 backdrop art (the original 4:3 art in the middle, the sides painted out), loaded on first use
// from <exe>/widescreen; nullptr when the file is missing (the tiles are drawn as before). Narrower windows show the
// middle of it (16:9 shows the former 16:9 picture exactly: it is pasted unchanged into the centre).
const Texture2D* wideArt(int which) {
    static Texture2D tex[2] = {};
    static bool tried[2] = {};
    if (!tried[which]) {
        tried[which] = true;
        const auto path = platform::exeDir() / "widescreen" / (which == 0 ? "title_wide.png" : "options_wide.png");
        std::error_code ec;
        if (std::filesystem::exists(path, ec)) {
            tex[which] = LoadTexture(path.string().c_str());
            if (tex[which].id != 0) {
                GenTextureMipmaps(&tex[which]);
                SetTextureFilter(tex[which], TEXTURE_FILTER_TRILINEAR);
                SetTextureWrap(tex[which], TEXTURE_WRAP_CLAMP);
            }
        }
        platform::log("widescreen art " + path.string() + (tex[which].id != 0 ? "" : ": not used"));
    }
    return tex[which].id != 0 ? &tex[which] : nullptr;
}
}  // namespace

// orig FUN_001050f0: FUN_00105370 (polygon layer 0x4e6990, FUN_00104000) then FUN_00105350 (sprite layer)
namespace {
// PORT: median colour of one texel column of a texture (cached per texture/column), for the widescreen backdrop
// continuation in drawSprites.
Color edgeColumnColour(const Texture2D& t, float u, float vA, float vB) {
    static std::unordered_map<unsigned long long, Color> cache;
    const int col = std::clamp(int(u * float(t.width)), 0, t.width - 1);
    const unsigned long long key = (static_cast<unsigned long long>(t.id) << 32) | unsigned(col);
    if (auto it = cache.find(key); it != cache.end()) return it->second;
    Image img = LoadImageFromTexture(t);
    const int r0 = std::clamp(int(vA * float(t.height)), 0, t.height - 1);
    const int r1 = std::clamp(int(vB * float(t.height)), r0 + 1, t.height);
    // per-channel median: the art's base colour, robust against glyph strokes that cross the edge
    std::vector<unsigned char> cr, cg, cb;
    for (int y = r0; y < r1; ++y) {
        const Color c = GetImageColor(img, col, y);
        cr.push_back(c.r); cg.push_back(c.g); cb.push_back(c.b);
    }
    UnloadImage(img);
    auto med = [](std::vector<unsigned char>& v) -> unsigned char {
        if (v.empty()) return 0;
        std::nth_element(v.begin(), v.begin() + std::ptrdiff_t(v.size() / 2), v.end());
        return v[v.size() / 2];
    };
    const Color out{med(cr), med(cg), med(cb), 255};
    cache.emplace(key, out);
    return out;
}
}  // namespace

void Renderer2D::drawSprites(int screenW, int screenH) {
    std::vector<ui::SpriteDraw> list;
    ui::sprites().build(list);
    // Draw order of the original pass (FUN_001050f0: FUN_00105370 polygon layer 0x4e6990 by FUN_00104000, then
    // FUN_00105350 sprite layer 0x4e68c0 by FUN_001032e0; the batch chains were built by FUN_00105360 /
    // FUN_00105340). Each layer draws its flag-0 lists first, then its list +4:
    // - flag 0 (FUN_001026e0 0x378(1)) writes z; every item is depth tested (FUN_001020a0 0x368(1); the context's
    //   depth function is LEQUAL, 0x370(3) in FUN_00374d10, and the ortho 0x4e668c maps larger z nearer), so between
    //   different z the nearer one wins whatever the order, and the order only decides equal z and blending.
    //   Polygon layer (FUN_00104aa0, FUN_001028c0 appends): the 0x129 list, then the 8 texture slots in slot order;
    //   sprite layer: ui::SpriteLayer::build order (texture slot 19 .. 0, then 0x129). Inside a list newest first.
    // - list +4 (layer flag != 0, no z-write: 0x378(0) -> FUN_00383cc0 -> ZMSK 1), in the order of FUN_00103410 /
    //   FUN_00104430: z bucket (FUN_00102b30, (int) z clamped 0..31) ascending, then layer flag ascending and texture
    //   id ascending (FUN_00102990 sorts the slots with the comparators 0x102920 / 0x102900), then, inside a slot,
    //   oldest first (each node is pushed at the head of its slot while the list is walked from its newest entry).
    //   Their z does not order them inside a bucket: e.g. the talk-mode strip of FUN_00150680 (tex 0x25, z -1.0 ..
    //   -0.88) is drawn under its button glyph (tex 0x28) and "Zoom" label (tex 0x29, z -1.0).
    auto list4Less = [](const DrawItem& a, const DrawItem& b) {
        const int ba = zBucket(a.z), bb = zBucket(b.z);
        if (ba != bb) return ba < bb;
        if (a.flag != b.flag) return a.flag < b.flag;
        if (a.tex != b.tex) return a.tex < b.tex;
        return a.age > b.age;
    };
    std::vector<DrawItem> items;
    items.reserve(list.size() + gPolygons.nodes().size());
    {
        const std::vector<Polygon2D*>& nodes = gPolygons.nodes();   // head (newest) first
        std::vector<DrawItem> list4;
        for (size_t i = 0; i < nodes.size(); ++i) {
            const Polygon2D* p = nodes[i];
            const int tex = p->texId < 0 ? ui::kTexNone : p->texId;
            if (p->layerFlag != 0) list4.push_back({p->z, nullptr, p, i, p->layerFlag, tex});
        }
        for (const Polygon2D* p : nodes)   // the 0x129 list first
            if (p->layerFlag == 0 && p->texId < 0) items.push_back({p->z, nullptr, p, 0, 0, ui::kTexNone});
        // UNVERIFIED: textured flag-0 polygons (none are created by the translated callers) are kept in node order;
        // the original groups them by its 8 texture slots (FUN_00113c60 first matching / first empty slot).
        for (const Polygon2D* p : nodes)
            if (p->layerFlag == 0 && p->texId >= 0) items.push_back({p->z, nullptr, p, 0, 0, p->texId});
        std::sort(list4.begin(), list4.end(), list4Less);
        items.insert(items.end(), list4.begin(), list4.end());
    }
    {
        std::vector<DrawItem> list4;
        for (size_t i = 0; i < list.size(); ++i) {
            const ui::SpriteDraw& s = list[i];
            if (s.flag != 0) list4.push_back({s.z, &s, nullptr, i, s.flag, s.texId});   // build(): list +4 first, newest first
            else items.push_back({s.z, &s, nullptr, 0, 0, s.texId});                    // flag-0 lists in batch order
        }
        std::sort(list4.begin(), list4.end(), list4Less);
        items.insert(items.end(), list4.begin(), list4.end());
    }
    // PORT widescreen: the presented area with the extended raster -delta .. 512+delta (= 0x4e668c at 4:3)
    const ScreenLayout L = screenLayout(screenW, screenH);
    const double d = static_cast<double>(L.delta);
    const bool wide = L.delta > 0.0f;
    // clip of the items that stay in the 4:3 box (sprites sliding in from outside the original raster stay hidden)
    auto clip = [&](bool toBox) {
        if (!wide) return;
        if (toBox) {
            rlEnableScissorTest();
            rlScissor(L.boxX, screenH - L.boxY - L.boxH, L.boxW, L.boxH);
        } else {
            rlDisableScissorTest();
        }
    };
    auto anchorDx = [&](int anchor) {
        return anchor == kAnchorLeft ? -L.hudDelta : (anchor == kAnchorRight ? L.hudDelta : 0.0f);
    };
    rlDrawRenderBatchActive();
    rlViewport(L.x, screenH - L.y - L.h, L.w, L.h);
    rlMatrixMode(RL_PROJECTION);
    rlPushMatrix();
    rlLoadIdentity();
    rlOrtho(-0.5 - d, 511.5 + d, 447.5, -0.5, -1000.0, 1000.0);   // 0x4e668c (-0.5, 511.5, 447.5, -0.5) widened
    rlMatrixMode(RL_MODELVIEW);
    rlLoadIdentity();   // BeginMode3D pattern: only the projection is pushed (rlgl has one stack)
    // FUN_001050f0: +0x298(2) clears the depth buffer only (the colour mask keeps the 3-D picture), then the pass runs
    // with the depth test on (rlgl's GL_LEQUAL; the ortho maps larger z nearer)
    rlEnableDepthMask();
    rlColorMask(false, false, false, false);
    rlClearScreenBuffers();
    rlColorMask(true, true, true, true);
    rlEnableDepthTest();
    rlDisableBackfaceCulling();   // GS sprites / strips have no culling
    rlSetBlendMode(RL_BLEND_ALPHA);
    BeginShaderMode(shader_);
    for (size_t n = 0; n < items.size(); ++n) {
        const DrawItem& item = items[n];
        // FUN_001026e0: z-write for flag 0 only (0x378(1)), 0x378(0) for the other flags
        if (item.flag == 0) rlEnableDepthMask();
        else rlDisableDepthMask();
        if (item.poly != nullptr) {
            // FUN_00104100: one strip, vertex = position + (dst ? dst : src)[i], colour +0x30 (constant with flag
            // bit 0, else per vertex), UVs +0x2c for the textured lists (text mode 1 glyphs, FUN_00112e80).
            const Polygon2D& p = *item.poly;
            const PolyVert* v = p.dst != nullptr ? p.dst : p.src;
            if (v == nullptr || p.count < 3) continue;
            if ((p.flags & 1u) == 0 && p.vertexColours == nullptr) continue;
            const bool textured = p.texId >= 0 && p.uv != nullptr;
            const Texture2D* t = textured ? ui::textures().texture(p.texId) : nullptr;
            if (textured && t == nullptr) continue;
            const int mode = textured ? 1 : 0;
            SetShaderValue(shader_, locMode_, &mode, SHADER_UNIFORM_INT);
            spriteBlend(p.layerFlag);   // FUN_00104000 -> FUN_001026e0(node +8)
            clip(p.anchor == kAnchorCentre);
            const float pdx = anchorDx(p.anchor);
            rlBegin(RL_TRIANGLES);
            // after rlBegin: switching the batch mode (the flushed batch restarts in RL_QUADS) resets the draw's texture
            // to rlgl's 1x1 white default, so a texture set before rlBegin(RL_TRIANGLES) was lost and every text mode 1
            // glyph (C08A / talk layout 2) came out as an opaque white cell
            rlSetTexture(textured ? t->id : white_.id);
            for (int i = 0; i + 2 < p.count; ++i) {
                for (int k = 0; k < 3; ++k) {
                    const int vi = i + k;
                    const PolyVert& q = v[vi];
                    const uint32_t c = (p.flags & 1u) != 0 ? p.colour : p.vertexColours[vi];
                    rlColor4ub(static_cast<unsigned char>(c & 0xff), static_cast<unsigned char>((c >> 8) & 0xff),
                               static_cast<unsigned char>((c >> 16) & 0xff), static_cast<unsigned char>(c >> 24));
                    if (textured) rlTexCoord2f(p.uv[vi * 2], p.uv[vi * 2 + 1]);
                    else rlTexCoord2f(0.0f, 0.0f);
                    rlVertex3f(p.x + q.x + pdx, p.y + q.y, p.z + q.z);
                }
            }
            rlEnd();
            rlDrawRenderBatchActive();
            continue;
        }
        const ui::SpriteDraw& s = *item.sprite;
        const bool textured = s.texId != ui::kTexNone;
        const Texture2D* t = textured ? ui::textures().texture(s.texId) : nullptr;
        if (textured && t == nullptr) continue;
        const int mode = textured ? 1 : 0;
        SetShaderValue(shader_, locMode_, &mode, SHADER_UNIFORM_INT);
        spriteBlend(s.flag);
        const unsigned char r = (unsigned char)(s.color & 0xff), g = (unsigned char)((s.color >> 8) & 0xff),
                            b = (unsigned char)((s.color >> 16) & 0xff), a = (unsigned char)(s.color >> 24);
        rlSetTexture(textured ? t->id : white_.id);
        // PORT widescreen: flat full-screen quads cover the area, HUD sprites move toward their edge, the rest is
        // clipped to the 4:3 box
        const bool bleed = wide && fullBleed(s);
        // PORT widescreen: the original pads 4:3 backdrop art with a single texel stretched over the side columns
        // (e.g. FUN_00164910: BG01 corner texel over x 0..136 and 392..512). Such flat pads touching a raster edge are
        // extended to the window edge -- one stretched texel is a solid colour, so nothing is distorted.
        bool padL = false, padR = false;
        if (wide && !bleed && textured && s.anchor == kAnchorCentre && s.hh * 2.0f >= 400.0f) {
            const float tw = std::fabs(s.u1 - s.u0) * float(t->width), th = std::fabs(s.v1 - s.v0) * float(t->height);
            if (tw <= 1.01f && th <= 1.01f) {
                padL = s.cx - s.hw <= 0.5f;
                padR = s.cx + s.hw >= 511.5f;
            }
        }
        // PORT widescreen menu frames (ui::Sprite::wide): active with the UI edge delta only (0 at 4:3 / classic HUD)
        const float wd = L.hudDelta;
        const int wf = wd > 0.0f ? s.wide : 0;
        // PORT widescreen backdrop (ui::kWideFill, a backdrop tile, not a frame piece): the art itself stays in the box
        const bool fillTile = wide && textured && (s.wide & ui::kWideFill) != 0;
        clip(!bleed && !padL && !padR && s.anchor == kAnchorCentre && (wf & ~ui::kWideFill) == 0);
        const float sdx = anchorDx(s.anchor);
        // PORT: widescreen backdrop art: the 2x2 tile draws its own part of the 16:9 picture, and the
        // tiles on the outer columns also the side area next to them (up to the art's own width; beyond it the
        // presented area stays black). The tile's vertex colour (the title fade) applies as to the original tile.
        if (wide && textured && s.anchor == kAnchorCentre && (s.wide & (ui::kWideArtTitle | ui::kWideArtMenu)) != 0) {
            if (const Texture2D* art = wideArt((s.wide & ui::kWideArtTitle) != 0 ? 0 : 1)) {
                const int col = s.cx > 256.0f ? 1 : 0, row = s.cy > 256.0f ? 1 : 0;
                const float ox = s.cx - s.hw - float(col) * 256.0f, oy = s.cy - s.hh - float(row) * 256.0f;
                // raster px are not square: the 4:3 raster 512 x 448 spans the art's middle at its height
                const float artDelta = (512.0f * (float(art->width) / float(art->height)) * 0.75f - 512.0f) * 0.5f;
                const float side = std::min(artDelta, L.delta);
                float ax0 = s.cx - s.hw, ax1 = s.cx + s.hw;
                if (col == 0) ax0 = ox - side;
                if (col == 1) ax1 = ox + 512.0f + side;
                const float ay0 = std::max(s.cy - s.hh, oy), ay1 = std::min(s.cy + s.hh, oy + 448.0f);
                if (ay1 > ay0 && ax1 > ax0) {
                    const float span = 512.0f + 2.0f * artDelta;
                    const float u0 = (ax0 - ox + artDelta) / span, u1 = (ax1 - ox + artDelta) / span;
                    const float v0 = (ay0 - oy) / 448.0f, v1 = (ay1 - oy) / 448.0f;
                    clip(false);
                    rlSetTexture(art->id);
                    rlBegin(RL_QUADS);
                    rlColor4ub(r, g, b, a);
                    rlTexCoord2f(u0, v0); rlVertex3f(ax0, ay0, s.z);
                    rlTexCoord2f(u0, v1); rlVertex3f(ax0, ay1, s.z);
                    rlTexCoord2f(u1, v1); rlVertex3f(ax1, ay1, s.z);
                    rlTexCoord2f(u1, v0); rlVertex3f(ax1, ay0, s.z);
                    rlEnd();
                    rlDrawRenderBatchActive();
                }
                continue;
            }
        }
        // PORT keyboard button prompts: a PS2 button glyph is shown as the bound key while keyboard / mouse is active
        // (keycapLabel null = the gamepad is active or nothing is bound: the original glyph below)
        if (textured && s.keepGlyph == 0 && gKeycapFn != nullptr) {
            if (const int bit = glyphCellBit(s, *t); bit != 0) {
                if (const char* label = keycapLabel(bit); label != nullptr) {
                    drawKeycap(s, label, sdx, shader_, locMode_, white_);
                    continue;
                }
            }
        }
        rlBegin(RL_QUADS);
        rlColor4ub(r, g, b, a);
        float x0 = s.cx - s.hw + sdx, x1 = s.cx + s.hw + sdx;
        const float y0 = s.cy - s.hh, y1 = s.cy + s.hh;
        if (bleed) {
            x0 -= L.delta;
            x1 += L.delta;
        }
        if (padL) x0 -= L.delta;
        if (padR) x1 += L.delta;
        if ((wf & ui::kWideLine) != 0) {
            // flat rule line (the page title bar 0..512 of FUN_00164a30, sliding in from the right): the raster span
            // 0..512 maps to the extended raster -wd..512+wd, so the settled line joins both area edges
            const float k = (512.0f + 2.0f * wd) / 512.0f;
            x0 = x0 * k - wd;
            x1 = x1 * k - wd;
        }
        if ((wf & ui::kWideSheen) != 0) {
            // FUN_00164dd0 scrolls the underline by -16 from cx 512 + hw and wraps below -512, i.e. it is hidden at both
            // ends of the 4:3 raster. Over the extended raster the same travel is moved right by wd (hidden at both
            // area edges while wd <= (512 - hw) / 2), else stretched linearly over -wd - hw .. 512 + hw + wd.
            const float lo = -512.0f, hi = 512.0f + s.hw;
            float cx = s.cx + wd;
            if (wd > (512.0f - s.hw) * 0.5f) cx = (-wd - s.hw) + (s.cx - lo) * ((hi + wd) - (-wd - s.hw)) / (hi - lo);
            x0 = cx - s.hw;
            x1 = cx + s.hw;
        }
        // PORT: stretched texel patches (e.g. the 8x8 window / panel / help-bar fill at (50, 42)-(58, 50) of TEX_UI02 /
        // TEX_UI04): on every axis drawn at 4x its texel size or more, sampled half a texel inside the rect. At the exact
        // edge UVs the bilinear filter mixed in the frame texels next to the patch, which the stretch turned into soft
        // light bands along the inside of message windows, menu panels and the help bar. Axes drawn near 1:1 (art,
        // frame pieces, glyphs) are untouched.
        float pu = 0.0f, pv = 0.0f;   // half-texel inset (normalised), signed with the UV direction
        if (textured) {
            const float tw = std::fabs(s.u1 - s.u0) * float(t->width), th = std::fabs(s.v1 - s.v0) * float(t->height);
            if (tw >= 2.0f && s.hw * 2.0f >= tw * 4.0f) pu = (s.u1 >= s.u0 ? 0.5f : -0.5f) / float(t->width);
            if (th >= 2.0f && s.hh * 2.0f >= th * 4.0f) pv = (s.v1 >= s.v0 ? 0.5f : -0.5f) / float(t->height);
        }
        auto quad = [&](float qx0, float qx1, float qu0, float qu1) {
            const float v0 = s.v0 + pv, v1 = s.v1 - pv;
            rlTexCoord2f(qu0, v0); rlVertex3f(qx0, y0, s.z);
            rlTexCoord2f(qu0, v1); rlVertex3f(qx0, y1, s.z);
            rlTexCoord2f(qu1, v1); rlVertex3f(qx1, y1, s.z);
            rlTexCoord2f(qu1, v0); rlVertex3f(qx1, y0, s.z);
        };
        if (textured && (wf & (ui::kWidePadL | ui::kWidePadR)) != 0) {
            // flat fill patch (the help-bar background, 8x8 texels drawn 460 wide): split at its centre, the outer
            // halves move with the caps and the centre texel column fills the gap (ends inset like above)
            const float xm = (x0 + x1) * 0.5f, um = (s.u0 + s.u1) * 0.5f;
            const float ls = (wf & ui::kWidePadL) != 0 ? wd : 0.0f, rs = (wf & ui::kWidePadR) != 0 ? wd : 0.0f;
            quad(x0 - ls, xm - ls, s.u0 + pu, um);
            quad(xm - ls, xm + rs, um, um);
            quad(xm + rs, x1 + rs, um, s.u1 - pu);
        } else {
            // PORT widescreen: a full-screen backdrop tile continued past a raster edge (block below) samples its
            // outermost texel column from the texel centre there; at the exact edge u the filter mixes in the
            // wrapped opposite column, which showed as a dark 1-px line along the 4:3 box edge (wide only)
            float qu0 = s.u0 + pu, qu1 = s.u1 - pu;
            if (fillTile) {
                const float half = (s.u1 >= s.u0 ? 0.5f : -0.5f) / float(t->width);
                if (s.cx - s.hw <= 1.5f) qu0 += half;
                if (s.cx + s.hw >= 510.5f) qu1 -= half;
            }
            quad(x0, x1, qu0, qu1);
        }
        // straight frame segment next to edge caps that moved by wd: repeated at its own size and UVs to fill the gap
        // (the outermost copy is cut at the area edge with its UVs cut in proportion; nothing is stretched)
        const float segW = x1 - x0;
        if ((wf & (ui::kWideTileL | ui::kWideTileR)) != 0 && segW >= 1.0f) {
            if ((wf & ui::kWideTileL) != 0) {
                for (float right = x0; right > x0 - wd + 0.001f; right -= segW) {
                    const float left = right - segW, cut = std::max(left, x0 - wd);
                    quad(cut, right, s.u0 + (s.u1 - s.u0) * ((cut - left) / segW), s.u1);
                }
            }
            if ((wf & ui::kWideTileR) != 0) {
                for (float left = x1; left < x1 + wd - 0.001f; left += segW) {
                    const float right = left + segW, cut = std::min(right, x1 + wd);
                    quad(left, cut, s.u0, s.u0 + (s.u1 - s.u0) * ((cut - left) / segW));
                }
            }
        }
        rlEnd();
        rlDrawRenderBatchActive();   // per-sprite mode uniform
        // PORT widescreen: full-screen 4:3 backdrop art marked ui::kWideFill (the BG02 tiles of Option / NameEntr /
        // GameLoad) is continued into the side area with the art's own base colour (median of its outermost texel
        // column), and the edge column fades into it over 16 raster px. No art is stretched. Opt-in only: a size / z
        // heuristic also caught the title BG00A tiles (flag 1, faded by the title), whose unfaded fill stayed as a
        // flat block in the left bar into NAME ENTRY. The 1-px slack covers bias -1 (tiles -1..511).
        if (fillTile) {
            const bool edgeL = s.cx - s.hw <= 1.5f, edgeR = s.cx + s.hw >= 510.5f;
            for (int side = 0; side < 2; ++side) {
                if ((side == 0 && !edgeL) || (side == 1 && !edgeR)) continue;
                const float uEdge = side == 0 ? std::min(s.u0, s.u1) + 0.5f / float(t->width)
                                              : std::max(s.u0, s.u1) - 0.5f / float(t->width);
                const Color base = edgeColumnColour(*t, uEdge, std::min(s.v0, s.v1), std::max(s.v0, s.v1));
                // the fill reaches the tile's own edge: a tile starting at raster x 0 (GameLoad bias 0, FUN_00201380)
                // left the half pixel -0.5..0 uncovered, a black 1-px line along the 4:3 box edge
                const float inL = std::max(-0.5f, s.cx - s.hw), inR = std::min(511.5f, s.cx + s.hw);
                const float bx0 = side == 0 ? -0.5f - L.delta : inR, bx1 = side == 0 ? inL : 511.5f + L.delta;
                const float fadeIn = side == 0 ? inL : inR, fadeOut = side == 0 ? inL - 16.0f : inR + 16.0f;
                clip(false);
                // flat base fill (untextured mode: colour in GS units, 0x80 = 1.0)
                int m0 = 0;
                SetShaderValue(shader_, locMode_, &m0, SHADER_UNIFORM_INT);
                rlSetTexture(white_.id);
                rlBegin(RL_QUADS);
                // modulated by the tile's own vertex colour (0x80 = 1.0), so a fade of the tile fades the fill too
                rlColor4ub((unsigned char)(base.r * r / 256), (unsigned char)(base.g * g / 256), (unsigned char)(base.b * b / 256), 0x80);
                rlTexCoord2f(0, 0); rlVertex3f(bx0, y0, s.z);
                rlTexCoord2f(0, 0); rlVertex3f(bx0, y1, s.z);
                rlTexCoord2f(0, 0); rlVertex3f(bx1, y1, s.z);
                rlTexCoord2f(0, 0); rlVertex3f(bx1, y0, s.z);
                rlEnd();
                rlDrawRenderBatchActive();
                // edge column fading into the base
                int m1 = 1;
                SetShaderValue(shader_, locMode_, &m1, SHADER_UNIFORM_INT);
                rlSetTexture(t->id);
                rlBegin(RL_QUADS);
                rlColor4ub(r, g, b, a);   rlTexCoord2f(uEdge, s.v0); rlVertex3f(fadeIn, y0, s.z);
                rlColor4ub(r, g, b, a);   rlTexCoord2f(uEdge, s.v1); rlVertex3f(fadeIn, y1, s.z);
                rlColor4ub(r, g, b, 0);   rlTexCoord2f(uEdge, s.v1); rlVertex3f(fadeOut, y1, s.z);
                rlColor4ub(r, g, b, 0);   rlTexCoord2f(uEdge, s.v0); rlVertex3f(fadeOut, y0, s.z);
                rlEnd();
                rlDrawRenderBatchActive();
            }
        }
    }
    rlEnableDepthMask();
    rlDisableDepthTest();
    rlSetTexture(0);
    rlSetBlendMode(RL_BLEND_ALPHA);
    EndShaderMode();
    if (wide) rlDisableScissorTest();
    rlEnableBackfaceCulling();
    rlMatrixMode(RL_PROJECTION);
    rlPopMatrix();
    rlMatrixMode(RL_MODELVIEW);
    rlLoadIdentity();
    rlViewport(0, 0, screenW, screenH);
}

// PORT: the max-aspect cap leaves black bars beside the presented area (the 3-D pass draws the whole window)
void Renderer2D::drawPillars(int screenW, int screenH) {
    const ScreenLayout L = screenLayout(screenW, screenH);
    if (L.x <= 0 && L.y <= 0 && L.w >= screenW && L.h >= screenH) return;
    rlDrawRenderBatchActive();
    rlViewport(0, 0, screenW, screenH);
    rlMatrixMode(RL_PROJECTION);
    rlPushMatrix();
    rlLoadIdentity();
    rlOrtho(0.0, screenW, screenH, 0.0, -1.0, 1.0);
    rlMatrixMode(RL_MODELVIEW);
    rlLoadIdentity();   // BeginMode3D pattern: only the projection is pushed (rlgl has one stack)
    rlDisableDepthTest();
    rlSetBlendMode(RL_BLEND_ALPHA);
    rlSetTexture(white_.id);
    rlBegin(RL_QUADS);
    rlColor4ub(0, 0, 0, 255);
    auto rect = [](float x0, float y0, float x1, float y1) {
        if (x1 <= x0 || y1 <= y0) return;
        rlTexCoord2f(0.0f, 0.0f); rlVertex2f(x0, y0);
        rlTexCoord2f(0.0f, 1.0f); rlVertex2f(x0, y1);
        rlTexCoord2f(1.0f, 1.0f); rlVertex2f(x1, y1);
        rlTexCoord2f(1.0f, 0.0f); rlVertex2f(x1, y0);
    };
    const float W = static_cast<float>(screenW), H = static_cast<float>(screenH);
    const float ax0 = static_cast<float>(L.x), ay0 = static_cast<float>(L.y);
    const float ax1 = static_cast<float>(L.x + L.w), ay1 = static_cast<float>(L.y + L.h);
    rect(0.0f, 0.0f, ax0, H);
    rect(ax1, 0.0f, W, H);
    rect(ax0, 0.0f, ax1, ay0);
    rect(ax0, ay1, ax1, H);
    rlEnd();
    rlDrawRenderBatchActive();
    rlSetTexture(0);
    rlMatrixMode(RL_PROJECTION);
    rlPopMatrix();
    rlMatrixMode(RL_MODELVIEW);
    rlLoadIdentity();
}

}  // namespace baroque::game
