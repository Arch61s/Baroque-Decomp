#pragma once
// Text window object 0x5180f0 (FUN_0015c490 set / FUN_0015c3f0 clear / FUN_0015c5f0 query / FUN_0015c620 alpha /
// FUN_0015c380 update -> FUN_0015c650 -> FUN_0015c710 horizontal or FUN_0015cac0 rotated typewriter draw). Ten slots;
// the front-end pages use slot 0 as the help bar at (32, 403, 448x40), the talk manager 0x53c6e0 slot 3, the event
// text op 27 any slot. Field offsets per slot i (4-byte stride within the object): +4 x, +0x2c y, +0x54 w, +0x7c h,
// +0xa4 visible chars (-1 = all), +0xcc chars per frame, +0x2900 pause, +0x2928 enabled, +0x2950 (cleared by set),
// +0x2978 alpha, +0x29a0 alpha delta (-1 = none), +0x29c8 scale w, +0x29f0 scale h, +0x2a18 colour, +0x2a40 style
// (0 horizontal FUN_0015c710, 1 rotated FUN_0015cac0), +0x2a68 proportional; text at +0xf4 + i*0x401.
#include <cstdint>
namespace baroque::game::ui {
class HelpBar {
public:
    static constexpr int kSlots = 10;
    void set(float x, float y, float w, float h, int slot, const char* fmt, ...);   // FUN_0015c490
    void clear(int slot);                                                             // FUN_0015c3f0 (-1 = all)
    bool isSet(int slot) const;                                                       // FUN_0015c5f0
    void update();                                                                    // FUN_0015c380
    void setSpeed(int slot, int charsPerFrame) { s_[slot].speed = charsPerFrame; }    // +0xcc (uRam005181bc = 4 idiom)
    // direct field writes of the event text handler FUN_0016f690 (+0x29c8 / +0x29f0 scale, +0x2a18 colour) and
    // orig FUN_0015c620 (work/decompiled/0015c620.c): isAlpha != 0 -> +0x2978 alpha, else +0x29a0 alpha delta
    void setScale(int slot, float w, float h) { s_[slot].scaleW = w; s_[slot].scaleH = h; }
    void setColor(int slot, uint32_t rgb) { s_[slot].color = rgb; }
    void setAlphaField(float v, int slot, int isAlpha) { if (isAlpha == 0) s_[slot].alphaDelta = v; else s_[slot].alpha = v; }
    // direct field writes of the talk manager (FUN_001a77c0 / FUN_001a8750 / FUN_001a2110 / FUN_001a2910)
    void setStyle(int slot, int style) { s_[slot].style = style; }                    // +0x2a40 (uRam0051ab3c = 1)
    void setProportional(int slot, int v) { s_[slot].proportional = v; }              // +0x2a68 (uRam0051ab64 = 0)
    void setFreeze(int slot, int v) { s_[slot].freeze = v; }                          // +0x2900 (iRam0051a9fc)
    void setEnabled(int slot, int v) { s_[slot].enabled = v; }                        // +0x2928 (iRam0051aa24)
    // field reads (talk manager, checks)
    int freeze(int slot) const { return s_[slot].freeze; }
    int enabled(int slot) const { return s_[slot].enabled; }
    int style(int slot) const { return s_[slot].style; }
    int speed(int slot) const { return s_[slot].speed; }
    int visible(int slot) const { return s_[slot].visible; }
    int proportional(int slot) const { return s_[slot].proportional; }
    float alpha(int slot) const { return s_[slot].alpha; }
    uint32_t color(int slot) const { return s_[slot].color; }
    float x(int slot) const { return s_[slot].x; }
    float y(int slot) const { return s_[slot].y; }
    float w(int slot) const { return s_[slot].w; }
    float h(int slot) const { return s_[slot].h; }
    float scaleW(int slot) const { return s_[slot].scaleW; }
    float scaleH(int slot) const { return s_[slot].scaleH; }
    const char* slotText(int slot) const { return s_[slot].text; }
private:
    struct Slot {
        float x = 0, y = 0, w = 0, h = 0;
        int visible = 0;        // +0xa4
        int speed = 1;          // +0xcc
        int freeze = 0;         // +0x2900
        int enabled = 0;        // +0x2928
        int f2950 = 0;          // +0x2950 (zeroed by FUN_0015c490; no reader in the text window code)
        float alpha = 1.0f;     // +0x2978
        float alphaDelta = -1.0f; // +0x29a0
        float scaleW = 1.0f;    // +0x29c8
        float scaleH = 1.0f;    // +0x29f0
        uint32_t color = 0xbbbbbb; // +0x2a18 (DAT_0044c190)
        int style = 0;          // +0x2a40
        int proportional = 1;   // +0x2a68
        char text[0x402] = {};  // +0xf4 + i*0x401 (0x401 bytes; one spare for the terminator FUN_0015c490 writes)
    };
    void drawSlot(int i);       // FUN_0015c710
    void drawSlotRotated(int i);   // FUN_0015cac0
    Slot s_[kSlots];
};
HelpBar& helpBar();
}
