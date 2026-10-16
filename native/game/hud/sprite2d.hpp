#pragma once
// 2-D drawing helpers of the HUD / menus (ITEMS_HUD_MENUS.md 0 "Sprite API"). All coordinates are in the original
// 512x448 raster (origin top-left, Y down; RUNTIME.md 1.5), texture rectangles in texels of the 128x128 TEX_UI
// sheets, colours A<<24|B<<16|G<<8|R with RGB 0x7f = 1.0 (FUN_00166820 convention; see game/render2d.hpp for the
// alpha model of the port renderer).
//
// The status HUD (hud.cpp StatusHud) owns persistent sprites of the real UI layer (game/ui/sprite.hpp), exactly like
// the original keeps sprite pointers in 0x5179c0. The other in-game windows (item ribbon, item screen, 2D map,
// message box) keep their FUN_00166820 records in HudSprite and emit them each tick through Sprite2D; the game's
// implementation of Sprite2D is UiSprite2D (ui_sprite2d.hpp), which turns every record into a UI-layer sprite that
// lives for one presented frame (the glyph-sprite model of the text object 0x4e7e90).
#include "game/items/char_access.hpp"
#include <cstdint>

namespace baroque::game {

// texture registry slots used by these systems (0x4e6a70 indices, GAME_FORMATS.md 5)
enum : int {
    kTexUi00 = 0x23, kTexUi01 = 0x24, kTexUi02 = 0x25, kTexUi03 = 0x26, kTexUi04 = 0x27, kTexUi05 = 0x28,
    kTexUi11 = 0x29, kTexUi34 = 0x2a, kTexUi35 = 0x2b, kTexNoTexture = 0x129
};

struct Sprite2D {
    virtual ~Sprite2D() = default;
    // FUN_00166820 sprite as stored: centre, half extents, z (larger = in front), UVs already divided by the texture
    // size, layer flag (FUN_001070a0 list selector).
    virtual void drawSprite(int texSlot, float u0, float v0, float u1, float v1, float cx, float cy, float z,
                            float hw, float hh, uint32_t colourABGR, int layerFlag = 1) = 0;
    // FUN_00112e80 through the text object 0x4e7e90: raw Shift-JIS bytes, (x, y) = top-left of the first glyph,
    // half glyph size (8x9 = 16x18 px glyphs), colour in the same ABGR convention, proportional EN metrics.
    virtual void drawText(const char* sjis, float x, float y, float z, float halfW, float halfH, uint32_t colourABGR, bool proportional) = 0;
    virtual float measureText(const char* sjis, float halfW, bool proportional) = 0;    // FUN_00113cf0 (widest line)
    // dynamic RGBA textures (FUN_00106350 / FUN_00106630 / FUN_00106650): returns a slot id >= 0x11e (-1 none)
    virtual int createDynamicTexture(int w, int h) = 0;
    virtual void updateDynamicTexture(int slot, const uint32_t* rgbaPixels) = 0;
    virtual void releaseDynamicTexture(int slot) = 0;                                     // FUN_00105f00
    // (the polygon layer 0x4e6990 is game/render2d.hpp PolygonLayer2D: persistent nodes, as in the original)
};

// Sprite record as FUN_00166820 fills it (ITEMS_HUD_MENUS.md 0): kept per window element so per-tick code can
// move / recolour it exactly like the original writes to +0x1c..+0x40.
struct HudSprite {
    bool live = false;
    int tex = kTexNoTexture;   // +0xc
    int flag = 1;              // +0x8 layer flag
    float cx = 0, cy = 0;      // +0x1c, +0x20
    float z = 0;               // +0x24
    uint32_t colour = 0x7f7f7f;// +0x28
    float hw = 0, hh = 0;      // +0x2c, +0x30
    float u0 = 0, v0 = 0, u1 = 0, v1 = 0;   // +0x34..+0x40
    void draw(Sprite2D& out) const { if (live) out.drawSprite(tex, u0, v0, u1, v1, cx, cy, z, hw, hh, colour, flag); }
};

// orig FUN_00166820(x, y, z, w, h, u0, v0, u1, texSlot, flag, [stack] v1, texW, texH) (work/decompiled/00166820.c):
// centre = x + w/2, UVs = texel / texW|texH (128 for every TEX_UI sheet; callers pass the constants).
HudSprite makeSprite(float x, float y, float z, float w, float h, float u0, float v0, float u1, float v1, int tex,
                     float texW = 128.0f, float texH = 128.0f, int flag = 1);
// orig FUN_00166eb0(x, y, z, w, h, buttonBit, flag): button glyph (TEX_UI05 for triangle/circle/cross/square
// 0x10/0x20/0x40/0x80, TEX_UI04 for L1/R1/L2/R2 = 4/8/1/2); UV rows of table 0x491ef0; colour 0x888888.
HudSprite makeButtonGlyph(float x, float y, float z, float w, float h, int buttonBit, int flag = 1);
int buttonGlyphRow(int buttonBit);                 // the uVar5 selection of FUN_00166eb0
extern const float kButtonGlyphUv[8][4];           // 0x491ef0 [V floats]

// orig FUN_00166a50 (work/decompiled/00166a50.c): 7x12 digit strip of TEX_UI00 (works on HudSprite and ui::Sprite)
template <typename S>
void setDigitGlyph(S& s, char ch) {
    s.u0 = 0; s.v0 = 0; s.u1 = 0; s.v1 = 0;
    switch (ch) {
        case '/': s.u0 = 0.546875f; s.v0 = 0; s.u1 = 0.5875f; s.v1 = 0.09375f; s.hw = 2.5f; s.hh = 6.0f; return;   // 0x3f0c0000 0x3f166666 0x3dc00000
        case ' ': s.u0 = 0; s.v0 = 0; s.u1 = 0; s.v1 = 0; s.hw = 3.5f; s.hh = 6.0f; return;
        case '-': s.u0 = 0.5859375f; s.v0 = 0; s.u1 = 0.6171875f; s.v1 = 0.09375f; return;                       // 0x3f160000 0x3f1e0000
        case ':': s.u0 = 0.9609375f; s.v0 = 0; s.u1 = 1.0f; s.v1 = 0.09375f; s.hw = 3.5f; s.hh = 6.0f; return;     // 0x3f760000
        case '%': s.u0 = 0.9140625f; s.v0 = 0.59375f; s.u1 = 0.984375f; s.v1 = 0.6875f; s.hw = 3.5f; s.hh = 6.0f; return;
        case 'U': s.u0 = 0.875f; s.v0 = 0; s.u1 = 0.9609375f; s.v1 = 0.09375f; s.hw = 5.0f; s.hh = 6.0f; return;    // 0x3f600000 0x3f760000
        case 'D': s.u0 = 0.7890625f; s.v0 = 0; s.u1 = 0.875f; s.v1 = 0.09375f; s.hw = 5.0f; s.hh = 6.0f; return;   // 0x3f4a0000
        case 'K': s.u0 = 0.6328125f; s.v0 = 0; s.u1 = 0.6796875f; s.v1 = 0.09375f; s.hw = 3.5f; s.hh = 6.0f; return;   // 0x3f220000 0x3f2e0000
        case '\0': return;
        default: break;
    }
    const int d = ch + -0x30;
    if (d < 1 || 9 < d) {
        s.u0 = 0.4921875f; s.v0 = 0; s.u1 = 0.546875f; s.v1 = 0.09375f;   // 0x3efc0000 .. 0x3f0c0000 ('0' and anything else)
    } else {
        const float f = static_cast<float>(ch + -0x31) * 7.0f;
        s.u0 = f / 128.0f; s.v0 = 0; s.u1 = (f + 7.0f) / 128.0f; s.v1 = 0.09375f;
    }
    s.hw = 3.5f; s.hh = 6.0f;
}

// orig FUN_00166d20 (work/decompiled/00166d20.c): 10x23 digits of TEX_UI00 (v 25..48)
template <typename S>
void setLevelDigitGlyph(S& s, char ch) {
    const int c = ch;
    const int d = c + -0x30;
    if (d < 1 || 7 < d) {
        if (d == 8) {
            const float f = static_cast<float>(c + -0x31) * 10.0f;
            s.u0 = f / 128.0f; s.v0 = 0.1953125f; s.u1 = (f + 10.0f + 1.0f) / 128.0f; s.v1 = 0.375f;   // 0x3e480000 / 0x3ec00000
        } else if (d == 9) {
            const float f = static_cast<float>(c + -0x31) * 10.0f;
            s.u0 = (f + 1.0f) / 128.0f; s.v0 = 0.1953125f; s.u1 = (f + 10.0f + 1.0f) / 128.0f; s.v1 = 0.375f;
        } else if (c == 0x30) {
            s.u0 = 0.7109375f; s.v0 = 0.1953125f; s.u1 = 0.796875f; s.v1 = 0.375f;   // 0x3f360000 / 0x3f4c0000
        } else {
            s.u0 = 0; s.v0 = 0; s.u1 = 0; s.v1 = 0;
        }
    } else {
        const float f = static_cast<float>(c + -0x31) * 10.0f;
        s.u0 = f / 128.0f; s.v0 = 0.1953125f; s.u1 = (f + 10.0f) / 128.0f; s.v1 = 0.375f;
    }
}

}  // namespace baroque::game
