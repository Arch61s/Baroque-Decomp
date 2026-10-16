#include "game/fade.hpp"
#include "game/ui/sprite.hpp"
#include "game/ui/texture_registry.hpp"
namespace baroque::game {
namespace {
FadeManager gFade;
// the float -> uint conversion the compiler emitted (values >= 2^31 wrap through the sign bit)
uint32_t ftou(float f) {
    if (f < 2.1474836e+09f) return static_cast<uint32_t>(static_cast<int>(f));
    return static_cast<uint32_t>(static_cast<int>(f - 2.1474836e+09f)) | 0x80000000u;
}
}
FadeManager& fade() { return gFade; }
uint32_t FadeManager::packedColor() const {
    return ftou(cur[0]) | (static_cast<uint32_t>(static_cast<int>(cur[1])) << 8) | (static_cast<uint32_t>(static_cast<int>(cur[3])) << 24) |
           (static_cast<uint32_t>(static_cast<int>(cur[2])) << 16);
}
// orig FUN_00106c00 (work/decompiled/00106c00.c)
void FadeManager::set(uint32_t abgr, long frameCount, long flag) {
    if (frameCount == 0) {
        cur[0] = static_cast<float>(abgr & 0xff);
        cur[1] = static_cast<float>((abgr >> 8) & 0xff);
        cur[2] = static_cast<float>((abgr >> 16) & 0xff);
        cur[3] = static_cast<float>(abgr >> 24);
    } else {
        if (sprite == nullptr) {
            sprite = ui::sprites().alloc(ui::kTexNone, 1);
            if (sprite == nullptr) return;
        }
        sprite->cx = 256.0f;   // 0x43800000
        sprite->cy = 224.0f;   // 0x43600000
        sprite->z = 30.0f;     // 0x41f00000
        sprite->hw = 264.0f;   // 0x43840000
        sprite->hh = 232.0f;   // 0x43680000
        sprite->color = packedColor();
        frames = static_cast<int>(frameCount);
        const float n = static_cast<float>(static_cast<int>(frameCount));
        target[0] = static_cast<float>(abgr & 0xff);
        target[1] = static_cast<float>((abgr >> 8) & 0xff);
        target[2] = static_cast<float>((abgr >> 16) & 0xff);
        target[3] = static_cast<float>(abgr >> 24);
        for (int i = 0; i < 4; ++i) delta[i] = (target[i] - cur[i]) / n;
    }
    if (flag == 1) flags |= 2u; else flags &= ~2u;
}
// orig FUN_00106810 (work/decompiled/00106810.c)
void FadeManager::setPause(int on) { if (on == 1) flags |= 1u; else flags &= ~1u; }
// orig FUN_00106840 (work/decompiled/00106840.c)
void FadeManager::update() {
    if (sprite == nullptr) return;
    if (flags & 1u) return;
    frames -= 1;
    if (frames < 0) {
        if (flags & 2u) { frames = 0; return; }
        ui::sprites().destroy(sprite);
        sprite = nullptr;
        frames = 0;
        return;
    }
    if (frames == 0) {
        for (int i = 0; i < 4; ++i) cur[i] = target[i];
    } else {
        for (int i = 0; i < 4; ++i) {
            cur[i] = cur[i] + delta[i];
            if (delta[i] < 0.0f) { if (cur[i] < target[i]) cur[i] = target[i]; }
            else if (target[i] < cur[i]) cur[i] = target[i];
        }
    }
    sprite->color = packedColor();
}
}
