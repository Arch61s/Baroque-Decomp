#include "game/pad.hpp"
#include <cmath>
namespace baroque::game {
namespace {
// table 0x49e060: 8 unit directions (up, up-right, right, down-right, down, down-left, left, up-left); y positive = down
const float kDirs[8][2] = {{0.0f, -1.0f}, {0.70710677f, -0.70710677f}, {1.0f, 0.0f}, {0.70710677f, 0.70710677f},
                           {0.0f, 1.0f}, {-0.70710677f, 0.70710677f}, {-1.0f, 0.0f}, {-0.70710677f, -0.70710677f}};
const uint32_t kRightMasks[8] = {0x100000, 0x900000, 0x800000, 0xa00000, 0x200000, 0x600000, 0x400000, 0x500000}; // 0x49e020
const uint32_t kLeftMasks[8] = {0x10000, 0x90000, 0x80000, 0xa0000, 0x20000, 0x60000, 0x40000, 0x50000};         // 0x49e040
PadMgr gPad;
}
PadMgr& pad() { return gPad; }

// orig FUN_0020bc90 (work/decompiled/0020bc90.c): dead zone r < 80 (r^2 < 0x1901), mag = (r-80)/48
void PadMgr::stickToFloat(float out[3], int h, int v) {
    const int dx = h - 0x80, dy = v - 0x80;
    const uint32_t r2 = static_cast<uint32_t>(dx * dx + dy * dy);
    if (r2 < 0x1901) { out[0] = out[1] = out[2] = 0.0f; return; }   // FUN_0020bc80 = zero
    const float r = std::sqrt(static_cast<float>(static_cast<int>(r2)));
    const float m = r - 80.0f;
    out[2] = m;
    if (m <= 0.0f) { out[0] = out[1] = out[2] = 0.0f; return; }
    out[2] = m / 48.0f;
    out[0] = static_cast<float>(dx) / r;
    out[1] = static_cast<float>(dy) / r;
}
// orig FUN_0020bd50 (work/decompiled/0020bd50.c): closest of 8 directions when mag >= 0.5
uint32_t PadMgr::stickToDigital(const float dir[3], bool rightStick) {
    if (dir[2] < 0.5f) return 0;
    uint32_t best = 0;
    float bestDot = -1.0f;
    for (uint32_t i = 0; i < 8; ++i) {
        const float d = dir[0] * kDirs[i][0] + dir[1] * kDirs[i][1];
        if (d > bestDot) { bestDot = d; best = i; }
    }
    return rightStick ? kRightMasks[best] : kLeftMasks[best];
}
// orig FUN_0020b860 -> FUN_0020c360 (read) + FUN_0020b910 (repeat) for port 0; port 1 stays disconnected.
void PadMgr::update(const platform::PadSample& sample) {
    PadPort& p = port[0];
    const uint32_t prev = p.held;
    stickToFloat(p.rstick, sample.rx, sample.ry);
    stickToFloat(p.lstick, sample.lx, sample.ly);
    // FUN_0020c360: held = (~raw16 & 0xffff) | rstickDigital | lstickDigital  (sample is already active-high)
    p.held = (sample.buttons & 0xffffu) | stickToDigital(p.rstick, true) | stickToDigital(p.lstick, false);
    p.pressed = p.held & ~prev;
    p.released = ~p.held & prev;
    // FUN_0020b910 (work/decompiled/0020b910.c)
    PadRepeatState& r = rep[0];
    if (p.held == 0) {
        p.repeat = 0; r.last = 0; r.counter = 0;
    } else if (p.held == r.last) {
        r.counter = static_cast<uint8_t>(r.counter - 1);
        if (r.counter == 0) { p.repeat = r.last; r.counter = r.rate; }
        else p.repeat = 0;
    } else {
        p.repeat = p.pressed; r.last = p.pressed; r.counter = r.initialDelay;
    }
}
}
