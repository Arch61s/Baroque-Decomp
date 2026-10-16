#pragma once
// Keyboard / mouse / gamepad -> original libpad sample (RUNTIME.md 2.1/2.2). The sample is what
// FUN_0020c360 would have read from scePadRead: active-high 16-bit button word in SCE_PAD layout plus
// raw stick axes 0..255 (128 = centre). game/pad.* turns it into held/pressed/released/repeat words.
//
// PORT: the PC inputs that produce the sample are data-driven: platform/controls.* (<exe>/controls.ini, defaults in
// input.cpp defaultControls(), table in work/reports/CONTROLS_NATIVE.md). Summary of the defaults:
//   W A S D = left stick (full deflection, diagonals on the circle); Left Ctrl held = walk (reduced magnitude)
//   Arrows = D-pad; I J K L = right stick; Space = Cross; C = Square; F / Backspace = Circle; Tab = Triangle;
//   Q / E = L1 / R1; Left Shift = L2; G = R2; R = R3; M = SELECT; Enter / Esc = START.
//   Mouse while captured for mouse look: LMB Cross, RMB Square, MMB R3, wheel up/down L1/R1. With a free cursor the
//   menu layer game/ui/mouse_menu.* (PORT) adds its pad bits through setPollHook.
//   Gamepad (first connected, raylib/XInput): 1:1 PS2 layout, sticks raw (the PS2 dead zone applies in game/pad.cpp).
#include <cstdint>
namespace baroque::platform {
struct PadSample {
    uint32_t buttons = 0;   // SCE_PAD bit layout, active-high (bits 0..15)
    int lx = 128, ly = 128; // left stick raw 0..255 (libpad ljoy h/v)
    int rx = 128, ry = 128; // right stick raw 0..255 (libpad rjoy h/v)
};
enum PadBits : uint32_t {
    PAD_L2 = 0x1, PAD_R2 = 0x2, PAD_L1 = 0x4, PAD_R1 = 0x8, PAD_TRIANGLE = 0x10, PAD_CIRCLE = 0x20,
    PAD_CROSS = 0x40, PAD_SQUARE = 0x80, PAD_SELECT = 0x100, PAD_L3 = 0x200, PAD_R3 = 0x400, PAD_START = 0x800,
    PAD_UP = 0x1000, PAD_RIGHT = 0x2000, PAD_DOWN = 0x4000, PAD_LEFT = 0x8000,
    PAD_LS_UP = 0x10000, PAD_LS_DOWN = 0x20000, PAD_LS_LEFT = 0x40000, PAD_LS_RIGHT = 0x80000,
    PAD_RS_UP = 0x100000, PAD_RS_DOWN = 0x200000, PAD_RS_LEFT = 0x400000, PAD_RS_RIGHT = 0x800000,
};
PadSample pollInput();   // reads raylib keyboard / mouse / gamepad through the bindings; call once per logic tick
void injectButtons(uint32_t mask, int polls);   // test hook: OR `mask` into the next `polls` pollInput() results
void injectStick(int lx, int ly, int polls);   // test hook: left stick raw (0..255) for the next `polls` pollInput() results
// PORT: extra button source sampled inside every pollInput() (the mouse menu layer); its result is ORed in.
void setPollHook(uint32_t (*hook)());
}
