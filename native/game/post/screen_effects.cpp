// Screen-effects object 0x4e7b80: state, setters and per-frame ramps (see screen_effects.hpp).
#include "game/post/screen_effects.hpp"

namespace baroque::game::post {

namespace {
ScreenEffects gEffects;
}

ScreenEffects& screenEffects() { return gEffects; }

// orig FUN_0010db20 (work/decompiled/0010db20.c), state part. The GL part (render target 128x128 (+0x8/+0xc),
// textures 128x128 (+0x18) and 256x256 (+0x1c), the first frame copies, FUN_00106090(0x1a / 0x1b) and the NOISE_T
// sampler settings) is postRenderer().construct().
void ScreenEffects::construct() {
    // orig FUN_0010e910 (work/decompiled/0010e910.c): effect 3
    dofFlags = 0;
    dofDistTarget = 850.0f;   // 0x44548000
    dofDist = 850.0f;
    dofFrames = 0;
    // orig FUN_0010f8e0 (work/decompiled/0010f8e0.c): noise
    noiseFlags = 0x10;
    noiseAlphaTarget = 0.0f;
    noiseAlpha = 0.0f;
    noiseFrames = 0;
    // orig FUN_0010dfd0 (work/decompiled/0010dfd0.c): bloom
    bloomFlags = 0;
    bloomSubTarget = 0.6f;    // 0x3f19999a
    bloomSub = 0.6f;
    bloomAddTarget = 0.6f;
    bloomAdd = 0.6f;
    bloomFrames = 0;
    constructed = 1;
    frame = Frame{};
}

// orig FUN_0010def0 (work/decompiled/0010def0.c), state part (texture releases in postRenderer().destroy()).
void ScreenEffects::destroy() {
    dofOff();      // FUN_0010edc0(obj, 0)
    bloomOff();    // FUN_0010e0a0(obj, 0)
    noiseOff();    // FUN_0010fb70(obj, 0)
    constructed = 0;
    frame = Frame{};
}

// orig FUN_0010e010 (work/decompiled/0010e010.c; register order checked in the disassembly at 0x10e010:
// $f12 -> +0x34, $f13 -> +0x28, $a1 -> +0x3c)
void ScreenEffects::bloomOn(float add, float sub, int frames) {
    bloomFrames = frames;
    bloomAddTarget = add;
    bloomSubTarget = sub;
    if (bloomFrames < 2) {
        bloomFrames = 0;
        bloomSubDelta = 0.0f;
        bloomSub = bloomSubTarget;
        bloomAddDelta = 0.0f;
        bloomAdd = bloomAddTarget;
    } else {
        bloomSubDelta = (bloomSubTarget - bloomSub) / static_cast<float>(bloomFrames);
        bloomAddDelta = (bloomAddTarget - bloomAdd) / static_cast<float>(bloomFrames);
    }
    bloomFlags |= 1u;
}

// orig FUN_0010e0a0 (work/decompiled/0010e0a0.c)
void ScreenEffects::bloomOff() {
    bloomFrames = 0;
    bloomFlags &= 0xfffffffeu;
}

// orig FUN_0010ed60 (work/decompiled/0010ed60.c)
void ScreenEffects::dofOn(float dist, int frames) {
    dofFrames = frames;
    dofDistTarget = dist;
    if (dofFrames < 2) {
        dofFrames = 0;
        dofDelta = 0.0f;
        dofDist = dofDistTarget;
    } else {
        dofDelta = (dofDistTarget - dofDist) / static_cast<float>(dofFrames);
    }
    dofFlags |= 1u;
}

// orig FUN_0010edc0 (work/decompiled/0010edc0.c)
void ScreenEffects::dofOff() {
    dofFrames = 0;
    dofFlags &= 0xfffffffeu;
}

// orig FUN_0010fb00 (work/decompiled/0010fb00.c)
void ScreenEffects::noiseOn(float alpha, int frames) {
    if ((noiseFlags & 1u) == 0) {
        noiseFlags |= 1u;
        noiseY = 0.0f;
        noiseX = 0.0f;
        noiseFrames = frames;
    } else {
        noiseFrames = frames;
    }
    noiseAlphaTarget = alpha;
    if (noiseFrames < 2) {
        noiseFrames = 0;
        noiseAlphaDelta = 0.0f;
        noiseAlpha = noiseAlphaTarget;
    } else {
        noiseAlphaDelta = (noiseAlphaTarget - noiseAlpha) / static_cast<float>(noiseFrames);
    }
}

// orig FUN_0010fb70 (work/decompiled/0010fb70.c)
void ScreenEffects::noiseOff() {
    noiseFrames = 0;
    noiseFlags &= 0xfffffffeu;
}

void ScreenEffects::tick() {
    frame = Frame{};
    if ((constructed & 1u) == 0) return;
    // orig FUN_0010ede0 head (work/decompiled/0010ede0.c): pass e, enabled by +0x10 & +0x40 bit0
    if ((dofFlags & 1u) != 0) {
        if (dofFrames != 0) {
            if (--dofFrames == 0) dofDist = dofDistTarget;
            else dofDist = dofDist + dofDelta;
        }
        frame.dof = true;
        frame.dofDist = dofDist;
    }
    // orig FUN_0010e0c0 head (work/decompiled/0010e0c0.c): pass i, enabled by +0x10 & +0x20 bit0
    if ((bloomFlags & 1u) != 0) {
        if (bloomFrames != 0) {
            if (--bloomFrames == 0) {
                bloomAdd = bloomAddTarget;
                bloomSub = bloomSubTarget;
            } else {
                bloomAdd = bloomAdd + bloomAddDelta;
                bloomSub = bloomSub + bloomSubDelta;
            }
        }
        frame.bloom = true;
        frame.bloomSub = bloomSub;
        frame.bloomAdd = bloomAdd;
    }
    // orig FUN_0010fb90 (work/decompiled/0010fb90.c): pass k, enabled by +0x10 & +0x70 bit0. The texture matrix of
    // this frame uses the scroll before the step; the step wraps with `64 < v` (64.0 itself is kept).
    if ((noiseFlags & 1u) != 0) {
        if (noiseFrames != 0) {
            if (--noiseFrames == 0) noiseAlpha = noiseAlphaTarget;
            else noiseAlpha = noiseAlpha + noiseAlphaDelta;
        }
        frame.noise = true;
        frame.noiseAlpha = noiseAlpha;
        frame.noiseX = noiseX;
        frame.noiseY = noiseY;
        noiseX = noiseX + 17.0f;
        noiseY = noiseY + 27.0f;
        if (64.0f < noiseX) noiseX = noiseX - 64.0f;
        if (64.0f < noiseY) noiseY = noiseY - 64.0f;
    }
}

}  // namespace baroque::game::post
