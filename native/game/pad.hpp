#pragma once
// Pad object 0x4e6280 (RUNTIME.md 2.1-2.3): per-port held/pressed/released/repeat words, stick floats and the
// stick -> digital emulation. Sampled at the END of the engine tick (FUN_0020b860 in FUN_00101bd0).
#include "platform/input.hpp"
#include <cstdint>
namespace baroque::game {
struct PadPort {                 // 0x4e6284 + port*0x5c
    uint32_t held = 0;           // +0x00 buttons incl. stick-direction bits
    uint32_t pressed = 0;        // +0x04 held & ~prev
    uint32_t released = 0;       // +0x08 ~held & prev
    uint32_t repeat = 0;         // +0x0c auto-repeat filtered (FUN_0020b910)
    float pressure[12] = {};     // +0x10 (not produced by the port: no DualShock2 pressure data)
    float rstick[3] = {};        // +0x40 dir.x, dir.y, magnitude
    float lstick[3] = {};        // +0x4c dir.x, dir.y, magnitude
    uint32_t flags = 0;          // +0x58 bit0 = pressure mode
};
struct PadRepeatState {          // 0x4e6280 + 0x304 + port*0xc
    uint32_t last = 0;           // +0x304
    uint8_t counter = 0;         // +0x308
    uint8_t initialDelay = 0x0f; // +0x309 (FUN_0020b790)
    uint8_t rate = 2;            // +0x30a
};
struct PadMgr {
    PadPort port[2];
    PadRepeatState rep[2];
    // FUN_0020b860: per-frame read of both ports. Port 1 is never connected in the port.
    void update(const platform::PadSample& sample);
    // FUN_0020bc90 / FUN_0020bd50 exposed for tests
    static void stickToFloat(float out[3], int h, int v);
    static uint32_t stickToDigital(const float dir[3], bool rightStick);
};
// Global pad object (0x4e6280). Menus read port 0: pressed = uRam004e6288, repeat = uRam004e6290.
PadMgr& pad();
}
