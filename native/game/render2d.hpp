#pragma once
// 2-D presentation of the UI sprite layer 0x4e68c0 and the polygon layer 0x4e6990 (RUNTIME.md 4.2 passes l/m:
// FUN_00105000 builds both, FUN_001050f0 draws the polygon layer (FUN_00105370) then the sprite layer (FUN_00105350)
// after a depth clear in the 512x448 ortho `engine.ortho` 0x4e668c = (-0.5..511.5, 447.5..-0.5)).
// GS modulate in GS units: RGB = Ct*Cf>>7 (0x80 = 1.0), A = At*Af>>7 where PNG textures keep their 8-bit alpha
// (0xFF), so the scenes' sprite alpha 0x40 shows a texture fully; untextured quads (0x129) use Af directly
// (0x80 = opaque). Blend (4,5) = src-alpha / inv-src-alpha. UNVERIFIED: GS As > 0x80 overshoot is clamped to 1.0.
// The 512x448 raster is presented as 4:3 (each source pixel 1.25 wide, RUNTIME.md 1.3), centred and pillarboxed.
#include "raylib.h"
#include <cstdint>
#include <vector>

namespace baroque::game {

struct Viewport43 { int x, y, w, h; };
Viewport43 viewport43(int screenW, int screenH);   // largest 4:3 rectangle centred in the window

// PORT widescreen layout of a frame (game/present/present_layout.hpp builds it; the default provider is the plain
// 4:3 presentation: area = box, delta 0). Area = the presented part of the window (black beyond it); box = the 4:3
// rectangle of the 512x448 UI raster; delta = raster units the area adds on each side of the box (extended raster
// -delta .. 512 + delta); hudDelta = shift of edge-anchored HUD sprites toward their edge (0 = classic 4:3 HUD).
struct ScreenLayout {
    int x = 0, y = 0, w = 0, h = 0;
    int boxX = 0, boxY = 0, boxW = 0, boxH = 0;
    float delta = 0.0f;
    float hudDelta = 0.0f;
};
using ScreenLayoutFn = ScreenLayout (*)(int screenW, int screenH);
void setScreenLayoutProvider(ScreenLayoutFn fn);
ScreenLayout screenLayout(int screenW, int screenH);
// UI anchors of sprites / polygons (ui::Sprite::anchor, Polygon2D::anchor): kept in the 4:3 box, or at their distance
// from the left / right screen edge
enum : int { kAnchorCentre = 0, kAnchorLeft = 1, kAnchorRight = 2 };

// PORT keyboard button prompts (game/ui/key_prompt.*, Baroque-Native only): the provider returns the keycap label of a
// PS2 pad bit (SCE_PAD 0x1..0x8000) or nullptr to keep the original glyph. Without a provider (the check targets)
// keycapLabel is always nullptr. drawSprites presents every PS2 button glyph cell (FUN_00166eb0, table 0x491ef0) whose
// label is non-null as a keycap at the glyph's place, size and alpha (ui::Sprite::keepGlyph opts a sprite out).
using KeycapLabelFn = const char* (*)(int padBit);
void setKeycapLabelProvider(KeycapLabelFn fn);
const char* keycapLabel(int padBit);

// Polygon node of the layer 0x4e6990 (0x3c bytes, allocated by FUN_00114c30 into the untextured list 0x4e699c by
// FUN_00113c60(0x4e6990, 0x129, 0)). Emitted by FUN_00104100 as one GS triangle strip: vertex i = position +
// (dst verts, or src verts when +0x34 is null); consecutive nodes are joined by two degenerate vertices.
struct PolyVert { float x = 0, y = 0, z = 0; };
struct Polygon2D {
    float x = 0, y = 0, z = 0;          // +0x1c / +0x20 / +0x24
    const PolyVert* src = nullptr;      // +0x28 (FUN_001051d0 a1)
    const float* uv = nullptr;          // +0x2c u,v pairs per vertex (text mode 1 glyphs; null: untextured)
    uint32_t colour = 0;                // +0x30: with flag bit 0 the ABGR colour itself, else a per-vertex array
    const uint32_t* vertexColours = nullptr;  // +0x30 as a pointer (flag bit 0 clear): ABGR per vertex
    int texId = -1;                     // texture list of FUN_00113c60(0x4e6990, tex, ..): -1 = 0x129 (untextured)
    PolyVert* dst = nullptr;            // +0x34 transformed vertices (FUN_001051d0 a5, copied from src)
    uint16_t count = 0;                 // +0x38
    uint16_t flags = 0;                 // +0x3a (bit 0: constant colour)
    int layerFlag = 0;                  // +8 layer flag (FUN_00107070 a3): blend / z-write of FUN_001026e0, list +4 if != 0
    int anchor = 0;                     // PORT: widescreen UI anchor (kAnchor*, from the sprite layer's anchor scope)
};
// orig FUN_001051d0 (work/decompiled/001051d0.c): vertex source / UVs / colour word / count / destination buffer;
// the source is copied into the destination
void polygonSetVerts(Polygon2D& p, const PolyVert* src, const float* uv, uint32_t colour, uint16_t count, PolyVert* dst);
// FUN_001051d0 with a per-vertex colour array in a3 (FUN_00112e80 mode 1 passes its glyph buffer +0x50)
void polygonSetVertsColours(Polygon2D& p, const PolyVert* src, const float* uv, const uint32_t* colours, uint16_t count,
                            PolyVert* dst);
// orig FUN_00105240 (work/decompiled/00105240.c): angles in degrees -> FUN_00207090 rotation matrix ->
// FUN_001052c0 / FUN_00206b40: dst[i] = src[i] * M (row vector, no translation)
void polygonRotate(Polygon2D& p, const float anglesDeg[3]);

// Layer 0x4e6990 (one node list; the port keeps creation order, newest first like FUN_00107070; the draw order of the
// original lists is rebuilt by Renderer2D::drawSprites)
class PolygonLayer2D {
public:
    // FUN_00107130 / FUN_00113c60(0x4e6990, tex, flag) / FUN_00114c30 / FUN_00107070; texId -1 = the untextured list
    // 0x129; flag != 0 = the list +4
    Polygon2D* create(int texId = -1, int layerFlag = 0);
    void destroy(Polygon2D* p);          // the inline unlink idiom + FUN_001053d0
    template <typename T> void destroyRef(T*& p) { if (p) { destroy(p); p = nullptr; } }
    const std::vector<Polygon2D*>& nodes() const { return nodes_; }
    int liveCount() const { return static_cast<int>(nodes_.size()); }
private:
    std::vector<Polygon2D*> nodes_;      // head first
};
PolygonLayer2D& polygons();              // 0x4e6990

class Renderer2D {
public:
    bool init();
    void shutdown();
    // Draws the polygon layer and the sprite layer into the 4:3 area of a screenW x screenH framebuffer.
    // PORT widescreen (screenLayout): over the presented area with the extended raster; centre-anchored items are
    // clipped to the 4:3 box, flat full-screen quads widen to the area, edge-anchored HUD items move by hudDelta.
    void drawSprites(int screenW, int screenH);
    // PORT: black bars over the window outside the presented area (max-aspect cap) after the 3-D pass.
    void drawPillars(int screenW, int screenH);
private:
    Shader shader_{};
    int locMode_ = -1;
    Texture2D white_{};
};
Renderer2D& renderer2d();

}  // namespace baroque::game
