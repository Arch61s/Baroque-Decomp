#pragma once
// Fade object 0x4e7890 (SCENES.md 0.2): FUN_00106c00(fade, abgr, frames, flag), FUN_00106840 per-frame,
// FUN_00106810 pause bit. Layout: +0 flags (bit0 pause, bit1 hold), +4 sprite, +8 frames left (iRam004e7898),
// +0xc..+0x18 current r,g,b,a (floats), +0x1c..+0x28 target, +0x2c..+0x38 per-frame delta.
#include <cstdint>
namespace baroque::game {
namespace ui { struct Sprite; }
struct FadeManager {
    uint32_t flags = 0;          // +0
    ui::Sprite* sprite = nullptr;// +4
    int frames = 0;              // +8  (0 = "fade finished")
    float cur[4] = {};           // +0xc  r g b a
    float target[4] = {};        // +0x1c
    float delta[4] = {};         // +0x2c
    void set(uint32_t abgr, long frameCount, long flag);   // FUN_00106c00
    void update();                                          // FUN_00106840
    void setPause(int on);                                  // FUN_00106810
    bool finished() const { return frames == 0; }           // iRam004e7898 == 0
    void clearHold() { flags &= ~2u; }                      // uRam004e7890 &= ~2 (Title init)
private:
    uint32_t packedColor() const;
};
FadeManager& fade();
}
