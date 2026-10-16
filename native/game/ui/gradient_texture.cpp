#include "game/ui/gradient_texture.hpp"
#include "game/ui/texture_registry.hpp"
#include <cstdint>
namespace baroque::game::ui {
namespace {
int gTexId = -1;          // DAT_0044c1a0
uint32_t* gPixels = nullptr;  // iRam0051aff8
uint32_t gPhase = 0;      // uRam0051b000
}
int gradientTextureId() { return gTexId; }
// orig FUN_00163e20 (work/decompiled/00163e20.c)
void gradientCreate() {
    if (gTexId != -1) return;
    gTexId = textures().createDynamic(0x20, 0x20);
    if (gTexId < 0) return;
    gPixels = textures().dynamicPixels(gTexId);
    for (int i = 0; i < 0x20 * 0x20; ++i) gPixels[i] = 0;
    int a = 0;
    for (uint32_t i = 0; i < 0x20; ++i) {
        gPixels[i] = static_cast<uint32_t>(a) * 0x1000000u + 0xc4u;
        if (i < 0x10) a += 8; else a -= 8;
    }
    gradientAnimate();
}
// orig FUN_00164060 (work/decompiled/00164060.c)
void gradientAnimate() {
    if (gTexId == -1 || gPixels == nullptr) return;
    uint32_t v = gPhase;
    auto px = [&](int byteOffset) -> uint32_t& { return gPixels[byteOffset / 4]; };
    auto col = [&](uint32_t a) { return (a < 0x7f) ? a * 0x1000000u + 0x44ffu : (0xffu - a) * 0x1000000u + 0x44ffu; };
    for (int i = 0; i < 0x20; ++i) { px(0x80 + i * 4) = col(v); v += 2; if (v > 0xfe) v = 0; }                   // row 1, left -> right
    for (int i = 1, o = 0x20; i < 0x20; ++i, o += 0x20) { px(o * 4 + 0x7c) = col(v); v += 2; if (v > 0xfe) v = 0; } // right column, down
    for (int i = 0x1f, o = 0x7c; i >= 0; --i, o -= 4) { px(o + 0xf80) = col(v); v += 2; if (v > 0xfe) v = 0; }   // row 31, right -> left
    for (int i = 0x1f, o = 0x3e0; i > 0; --i, o -= 0x20) { px(o * 4) = col(v); v += 2; if (v > 0xfe) v = 0; }    // left column, up
    gPhase += 4;
    if (gPhase > 0xfe) gPhase = 0;
    textures().dynamicUpload(gTexId);
}
// orig FUN_00163fc0 (work/decompiled/00163fc0.c)
void gradientFree() {
    if (gTexId == -1) return;
    if (textures().isLoaded(gTexId)) {
        textures().release(gTexId);
        gTexId = -1;
        gPixels = nullptr;
    }
}
}
