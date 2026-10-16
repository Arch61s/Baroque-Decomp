#pragma once
// 2-D screen sprites of the UI layer 0x4e68c0 (SCENES.md 0.3, RUNTIME.md 4.2 rows l/m).
// Sprite (0x44 bytes, FUN_00107180): +0 next, +4 prev, +8 layer flag, +0xc texture id, +0x1c centre x, +0x20 centre y,
// +0x24 z (ortho depth, larger = in front; the brightness overlay uses 50, the fade quad 30), +0x28 ABGR colour
// (0x7f7f7f = full RGB, alpha 0x80 = opaque), +0x2c half width, +0x30 half height, +0x34..+0x40 u0 v0 u1 v1.
// Layer object 0x4e68c0: +4/+8 list for flag != 0 sprites, +0xc/+0x10 untextured (0x129) list, +0x14.. 20 buckets
// {head, count} keyed by texture id (FUN_001070a0). New sprites are pushed at the head (FUN_00107070).
#include <cstdint>
#include <vector>
namespace baroque::game::ui {
struct Sprite {
    Sprite* next = nullptr;   // +0
    Sprite* prev = nullptr;   // +4
    int layerFlag = 0;        // +8
    int texId = 0;            // +0xc
    float cx = 0, cy = 0;     // +0x1c, +0x20
    float z = 0;              // +0x24
    uint32_t color = 0x7f7f7f;// +0x28
    float hw = 0, hh = 0;     // +0x2c, +0x30
    float u0 = 0, v0 = 0, u1 = 0, v1 = 0;  // +0x34..+0x40
    int anchor = 0;           // PORT: widescreen UI anchor (game::kAnchor*), stamped from the layer's anchor scope
    int wide = 0;             // PORT: widescreen menu-frame role (kWide* below), presentation only
    int keepGlyph = 0;        // PORT: 1 = always the PS2 button glyph, never a keyboard keycap (game::keycapLabel)
};
// PORT widescreen menu frames (Sprite::wide; honoured by game::Renderer2D::drawSprites only while the UI edge delta
// ScreenLayout::hudDelta is > 0, i.e. never at 4:3 or with the classic 4:3 HUD; the sprite itself keeps its original
// position). Edge caps use Sprite::anchor (kAnchorLeft / kAnchorRight) instead.
enum : int {
    kWideTileL = 1,    // straight segment next to the left caps: repeated leftwards (same UVs and size) over the delta
    kWideTileR = 2,    // the same rightwards
    kWidePadL = 4,     // flat fill patch (the help-bar background): split at its centre, the left half moves with the left
                       // caps and the centre texel column fills the gap
    kWidePadR = 8,     // the same on the right
    kWideLine = 16,    // untextured rule line: x mapped from the raster 0..512 to the extended raster -delta..512+delta
    kWideSheen = 32,   // the title underline sheen (FUN_00164dd0 scroll): its travel is moved / remapped to the area
    kWideFill = 64,    // full-screen 4:3 backdrop tile (the BG02 menu background): the raster edge it touches is
                       // continued into the side area with the art's edge colour (opt-in; the title BG stays pillarboxed)
    kWideArtTitle = 128,   // PORT: tile of the 2x2 title backdrop (BG00A); while the area is wider than 4:3 the
    kWideArtMenu = 256,    // tile of the 2x2 BG02 backdrop; both are drawn from the 16:9 art <exe>/widescreen/*.png
                           // (original art in the 4:3 middle) when that file is there, else as before
};
struct SpriteBucket { Sprite* head = nullptr; int count = 0; int texId = 0; };
struct SpriteDraw {           // flattened copy used by the renderer (built at the end of the tick)
    int texId; float cx, cy, z, hw, hh, u0, v0, u1, v1; uint32_t color;
    int flag;                 // +8 layer flag: blend / z-write mode of FUN_001026e0
    int anchor = 0;           // PORT: Sprite::anchor
    int wide = 0;             // PORT: Sprite::wide
    int keepGlyph = 0;        // PORT: Sprite::keepGlyph
};
class SpriteLayer {
public:
    // FUN_00166820(x, y, z, w, h, u0, v0, u1, texId, flag, v1, texW, texH): the texture size is taken from the
    // loaded texture (callers pass the matching constants). Returns nullptr when the texture is not loaded.
    Sprite* create(float x, float y, float z, float w, float h, float u0, float v0, float u1, float v1, int texId, int flag);
    // FUN_00107130 + FUN_001070a0 + FUN_00107180 + FUN_00107070: allocate and link; fields left for the caller
    Sprite* alloc(int texId, int flag);
    // the inline unlink idiom (bucket lookup by flag/texture, unlink, FUN_001053d0 free, count--)
    void destroy(Sprite* s);
    template <typename T> void destroyRef(T*& s) { if (s) { destroy(s); s = nullptr; } }
    // list +4 first, then the flag-0 lists in the original batch order (texture slot 19 .. 0, then 0x129); each list
    // newest first
    void build(std::vector<SpriteDraw>& out) const;
    int liveCount() const;
    void clearAll();
    // PORT (widescreen HUD anchoring, presentation only): sprites allocated while a scope is set carry its anchor;
    // polygons of game/render2d.hpp read the same scope. The HUD sets it around its element groups (AnchorScope).
    void setAnchorScope(int anchor) { anchorScope_ = anchor; }
    int anchorScope() const { return anchorScope_; }
private:
    int anchorScope_ = 0;
    SpriteBucket* bucketFor(int texId, int flag, bool allocate);
    SpriteBucket list1_;                 // +4
    SpriteBucket untextured_;            // +0xc
    SpriteBucket textured_[20];          // +0x14 .. +0xac
};
SpriteLayer& sprites();                  // 0x4e68c0
// PORT: RAII anchor scope of the sprite layer (restores the previous scope)
struct AnchorScope {
    explicit AnchorScope(int anchor) : prev_(sprites().anchorScope()) { sprites().setAnchorScope(anchor); }
    ~AnchorScope() { sprites().setAnchorScope(prev_); }
    AnchorScope(const AnchorScope&) = delete;
    AnchorScope& operator=(const AnchorScope&) = delete;
private:
    int prev_;
};
// colour helpers (PS2 0x80 = 1.0)
inline uint32_t spriteColor(uint32_t alpha, uint32_t rgb = 0x7f7f7f) { return (alpha << 24) | rgb; }
}
