#pragma once
// Text object 0x4e7e90 (GAME_FORMATS.md 5; FUN_001129c0 ctor, FUN_00112e80 draw, FUN_00113cf0 measure,
// FUN_00112800 glyph lookup, FUN_00112b30 lead-byte cache, FUN_00112c60 end-of-tick release).
// Glyphs are UI sprites (layer 0x4e68c0) created when drawn and freed at the end of the engine tick, so every
// frame redraws its text. Field offsets are int indices into the object: [0] mode, [0x601] count,
// [0x602] x, [0x603] y, [0x604] z, [0x605] half width (8 = 16 px glyph), [0x606] half height (9),
// [0x607] colour, [0x608] layer flag, [0x609] spacing, [0x60a] proportional (EN metrics).
#include <cstdarg>
#include <cstdint>
namespace baroque::game { struct Polygon2D; }
namespace baroque::game::ui {
struct Sprite;
class TextRenderer {
public:
    void init();                                     // FUN_001129c0(0x4e7e90)
    void draw(const char* fmt, ...);                 // FUN_00112e80
    void drawV(const char* fmt, va_list ap);
    float measure(const char* fmt, ...);             // FUN_00113cf0: widest line
    float measureV(const char* fmt, va_list ap);
    void endOfTick();                                // FUN_00112c60: free this tick's glyph sprites
    int glyphIndex(const unsigned char* sjis) const; // FUN_00112800 (0 when not found)
    // state (globals the pages write directly)
    int mode = 0;                                    // [0] uRam004e7e90: 0 sprites (0x4e68c0), 1 rotated polygons (0x4e6990)
    float x = 0, y = 0, z = 0;                       // 0x4e9698 / 0x4e969c / 0x4e96a0
    float halfW = 8.0f, halfH = 8.0f;                // 0x4e96a4 / 0x4e96a8
    uint32_t color = 0xffffffff;                     // 0x4e96ac
    int layerFlag = 0;                               // 0x4e96b0
    float spacing = 1.0f;                            // 0x4e96b4
    int proportional = 1;                            // 0x4e96b8
    int glyphCount() const { return count_; }
    // PORT: the ASCII / Shift-JIS -> glyph mapping of FUN_00112e80 for the keyboard keycaps (game/render2d.cpp)
    int glyphForChar(const char* p, int* advance) const { return asciiToGlyph(p, advance); }
    static constexpr int kMaxGlyphs = 0x200;
private:
    int asciiToGlyph(const char* p, int* advance) const;   // the switch in FUN_00112e80 (shared with measure)
    // FUN_00112e80 mode 1 glyph buffer (FUN_00114bf0, 0x6c bytes): 4 vertices, 4 UV pairs, 4 colours, position
    struct GlyphBuf {
        float vert[12];                              // +0x00
        float uv[8];                                 // +0x30
        uint32_t colour[4];                          // +0x50
        float pos[3];                                // +0x60
    };
    void drawGlyphMode1(int g, float& cx, float& cy, float cz);
    Sprite* glyphs_[kMaxGlyphs] = {};                // [0x201 + i] (mode 0)
    Polygon2D* polys_[kMaxGlyphs] = {};              // [0x201 + i] (mode 1)
    GlyphBuf* bufs_[kMaxGlyphs] = {};                // [0x401 + i] (mode 1)
    int glyphMode_[kMaxGlyphs] = {};                 // [1 + i] mode at creation
    int count_ = 0;                                  // [0x601]
    short leadStart_[256] = {};                      // 0x4e9540 / 0x4e95be caches
};
TextRenderer& text();
// FUN_00167250: centre a width/height inside 512x448 (only when > 0)
void centreOnScreen(float w, float h, float* x, float* y);
}
