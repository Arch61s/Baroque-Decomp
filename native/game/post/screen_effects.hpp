#pragma once
// Screen-effects object 0x4e7b80 (ctor FUN_0010db20, dtor FUN_0010def0): the three full-screen passes of the
// FUN_00101bd0 2-D phase (work/reports/POST_NATIVE.md):
//   pass e  FUN_0010ede0  "effect 3" = distance blur (depth-of-field): the frame, blurred into the 128x128 target,
//                         is drawn back as six camera-facing planes at 0.98 .. 0.5 x distance (+0x60) with the depth
//                         test on, so only pixels farther than each plane are covered (alpha 0.5 .. 0.05).
//   pass i  FUN_0010e0c0  bloom: frame -> 128x128, minus SUBTRACT.PNG x (+0x24), 3 rings x 6 taps additive blur,
//                         added back to the frame with colour/alpha (+0x30).
//   pass k  FUN_0010fb90  film grain: NOISE_T.PNG tiled 4x4 over the screen, scrolled (+17, +27) texels per frame.
// This header holds the object's state and the translated setters / per-frame ramps (no GL). The draws are in
// post_render.hpp. PORT split (as effects_gl.hpp): the original ramps the values at the start of each pass, once per
// displayed frame = once per tick; the port steps them in tick() (called at the render position of the Game tick)
// and records the values the passes use (Frame), which every presented host frame then draws.
#include <cstdint>

namespace baroque::game::post {

struct ScreenEffects {
    // +0x0 host, +0x4 display target, +0x8 128x128 render target (+0xc its texture), +0x18 128x128 texture,
    // +0x1c 256x256 texture: GL objects in post_render.cpp.
    uint32_t constructed = 0;      // +0x10 bit0 (set at the end of FUN_0010db20, cleared by FUN_0010def0)

    // bloom (FUN_0010dfd0 init, FUN_0010e010 set, FUN_0010e0a0 off)
    uint32_t bloomFlags = 0;       // +0x20 bit0 enabled
    float bloomSub = 0.6f;         // +0x24 current colour of the SUBTRACT.PNG pass (0x3f19999a)
    float bloomSubTarget = 0.6f;   // +0x28 (second float argument of FUN_0010e010)
    float bloomSubDelta = 0.0f;    // +0x2c
    float bloomAdd = 0.6f;         // +0x30 current colour/alpha of the final additive composite
    float bloomAddTarget = 0.6f;   // +0x34 (first float argument of FUN_0010e010)
    float bloomAddDelta = 0.0f;    // +0x38
    int bloomFrames = 0;           // +0x3c

    // effect 3 = distance blur (FUN_0010e910 init, FUN_0010ed60 set, FUN_0010edc0 off)
    uint32_t dofFlags = 0;         // +0x40 bit0 enabled
    // +0x44..+0x58 six plane vertex arrays, +0x5c the unit quad (FUN_0010e910): built per draw in the port
    float dofDist = 850.0f;        // +0x60 current plane distance (0x44548000)
    float dofDistTarget = 850.0f;  // +0x64
    float dofDelta = 0.0f;         // +0x68
    int dofFrames = 0;             // +0x6c

    // film grain (FUN_0010f8e0 init, FUN_0010fb00 set, FUN_0010fb70 off)
    uint32_t noiseFlags = 0x10;    // +0x70 bit0 enabled (FUN_0010f8e0 writes 0x10)
    float noiseAlpha = 0.0f;       // +0xb8 current
    float noiseAlphaTarget = 0.0f; // +0xbc
    float noiseAlphaDelta = 0.0f;  // +0xc0
    float noiseX = 0.0f;           // +0xc4 scroll (texels of the 64x64 NOISE_T)
    float noiseY = 0.0f;           // +0xc8
    int noiseFrames = 0;           // +0xcc

    // Values the passes of the current tick use (PORT: recorded by tick(), drawn by every presented frame).
    struct Frame {
        bool dof = false;
        float dofDist = 0.0f;
        bool bloom = false;
        float bloomSub = 0.0f, bloomAdd = 0.0f;
        bool noise = false;
        float noiseAlpha = 0.0f, noiseX = 0.0f, noiseY = 0.0f;
    };
    Frame frame;

    void construct();              // orig FUN_0010db20 (state part; textures 0x1a / 0x1b via post_render)
    void destroy();                // orig FUN_0010def0
    void bloomOn(float add, float sub, int frames);   // orig FUN_0010e010(add f12 -> +0x34, sub f13 -> +0x28, obj, frames)
    void bloomOff();               // orig FUN_0010e0a0
    void dofOn(float dist, int frames);               // orig FUN_0010ed60
    void dofOff();                 // orig FUN_0010edc0
    void noiseOn(float alpha, int frames);            // orig FUN_0010fb00
    void noiseOff();               // orig FUN_0010fb70
    // Per displayed frame: the ramp heads of FUN_0010ede0 / FUN_0010e0c0 / FUN_0010fb90 (in that pass order) and the
    // grain scroll of FUN_0010fb90; fills `frame`.
    void tick();
};

ScreenEffects& screenEffects();   // 0x4e7b80

}  // namespace baroque::game::post
