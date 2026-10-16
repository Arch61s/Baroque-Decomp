#include "platform/input.hpp"
#include "platform/controls.hpp"
#include "raylib.h"
#include <cmath>
// PORT: the whole file is the PC side of the pad. It produces the libpad sample FUN_0020c360 would have read
// (RUNTIME.md 2.1/2.2); the bindings come from platform/controls.* (controls.ini). Nothing here is original code.
namespace baroque::platform {
namespace {
// ---- live bindings -------------------------------------------------------------------------------------------------
Controls gControls = defaultControls();
void (*gLoader)() = nullptr;      // controls.cpp: load controls.ini once
bool gLoaded = false;
void ensureLoaded() {
    if (gLoaded) return;
    gLoaded = true;
    if (gLoader != nullptr) gLoader();
}

// ---- test hooks / mouse menu hook ----------------------------------------------------------------------------------
uint32_t gInjectMask = 0; int gInjectPolls = 0; uint32_t (*gPollHook)() = nullptr;
int gStickLx = 128, gStickLy = 128, gStickPolls = 0;

// ---- per host frame latches (inputHostFrame) -----------------------------------------------------------------------
uint32_t gLatched = 0;            // buttons tapped during a host frame, ORed into the next sample
int gWheelUp = 0, gWheelDown = 0; // queued wheel notches (captured cursor only)
bool gWheelGap = false;           // an empty sample after each wheel press, so consecutive notches give separate edges

void setKeys(InputBinding& b, int k0, int k1 = 0, int k2 = 0, int k3 = 0) { b.keys[0] = k0; b.keys[1] = k1; b.keys[2] = k2; b.keys[3] = k3; }

// ---- PORT (window focus): input gating (controls.hpp setInputBlocked / swallow*) -------------------------------------
bool gBlocked = false;            // window unfocused: no keyboard / mouse (and no gamepad unless gBlockedPadOk)
bool gBlockedPadOk = false;       // baroque_port.ini background_gamepad = on
bool gMouseSwallow = false;       // mouse buttons ignored until all are up
int gSwallowKeys[4] = {};         // keys ignored until released (0 = free slot)
bool gMouseLook = false;          // PORT: mouse-look capture (main.cpp DisableCursor), see setMouseLookCaptured
bool keySwallowed(int k) {
    for (int s : gSwallowKeys) if (s == k) return true;
    return false;
}
bool padAllowed() { return !gBlocked || gBlockedPadOk; }

bool anyKeyDown(const InputBinding& b) {
    for (int k : b.keys) if (k != 0 && !keySwallowed(k) && IsKeyDown(k)) return true;
    return false;
}
bool anyKeyPressed(const InputBinding& b) {
    for (int k : b.keys) if (k != 0 && !keySwallowed(k) && IsKeyPressed(k)) return true;
    return false;
}
bool mouseButtonDown(int m) { return !gMouseSwallow && m >= MB_LEFT && m <= MB_BACK && IsMouseButtonDown(m - 1); }
bool mouseButtonPressed(int m) { return !gMouseSwallow && m >= MB_LEFT && m <= MB_BACK && IsMouseButtonPressed(m - 1); }
bool anyMouseDown(const InputBinding& b) {
    for (int m : b.mouse) if (mouseButtonDown(m)) return true;
    return false;
}
bool anyMousePressed(const InputBinding& b) {
    for (int m : b.mouse) if (mouseButtonPressed(m)) return true;
    return false;
}
bool bindsMouse(const InputBinding& b, int code) {
    for (int m : b.mouse) if (m == code) return true;
    return false;
}

// Gamepad: -1 when disabled / none connected.
int activeGamepad(const Controls& c) {
    if (!c.gamepadEnabled || !padAllowed()) return -1;   // PORT (window focus): unfocused -> no gamepad by default
    if (c.gamepadIndex >= 0) return IsGamepadAvailable(c.gamepadIndex) ? c.gamepadIndex : -1;
    for (int g = 0; g < 4; ++g) if (IsGamepadAvailable(g)) return g;
    return -1;
}
// Analog triggers: raylib reports L2/R2 as held only past the middle of the travel (axis > 0.1 in -1..1), so the
// trigger axis is also tested against the configurable travel threshold.
bool padButtonDown(const Controls& c, int gp, int button) {
    if (button <= GAMEPAD_BUTTON_UNKNOWN || button > GAMEPAD_BUTTON_RIGHT_THUMB) return false;
    if (IsGamepadButtonDown(gp, button)) return true;
    const float limit = c.triggerThreshold * 2.0f - 1.0f;
    if (button == GAMEPAD_BUTTON_LEFT_TRIGGER_2) return GetGamepadAxisMovement(gp, GAMEPAD_AXIS_LEFT_TRIGGER) > limit;
    if (button == GAMEPAD_BUTTON_RIGHT_TRIGGER_2) return GetGamepadAxisMovement(gp, GAMEPAD_AXIS_RIGHT_TRIGGER) > limit;
    return false;
}
bool anyPadDown(const Controls& c, int gp, const InputBinding& b) {
    for (int p : b.pad) if (p != 0 && padButtonDown(c, gp, p)) return true;
    return false;
}
bool anyPadPressed(int gp, const InputBinding& b) {
    for (int p : b.pad) if (p > GAMEPAD_BUTTON_UNKNOWN && p <= GAMEPAD_BUTTON_RIGHT_THUMB && IsGamepadButtonPressed(gp, p)) return true;
    return false;
}

int axisToRaw(float v) {   // raylib axis -1..1 -> libpad 0..255 (128 = centre, -1 -> 0, +1 -> 255)
    int r = static_cast<int>(std::lround(v * 128.0f)) + 128;
    if (r < 0) r = 0;
    if (r > 255) r = 255;
    return r;
}
int r2(int x, int y) { return (x - 128) * (x - 128) + (y - 128) * (y - 128); }

// Keyboard stick: the held directions as a unit vector scaled to `radius` raw counts (128 = full deflection, which
// FUN_0020bc90 turns into magnitude (128-80)/48 = 1.0; a diagonal stays on the same circle, like an analog stick).
void keyStick(const Controls& c, int up, int down, int left, int right, float radius, int* outX, int* outY) {
    const float x = (anyKeyDown(c.bind[right]) ? 1.0f : 0.0f) - (anyKeyDown(c.bind[left]) ? 1.0f : 0.0f);
    const float y = (anyKeyDown(c.bind[down]) ? 1.0f : 0.0f) - (anyKeyDown(c.bind[up]) ? 1.0f : 0.0f);
    *outX = 128;
    *outY = 128;
    if (x == 0.0f && y == 0.0f) return;
    const float len = std::sqrt(x * x + y * y);
    *outX = axisToRaw((x / len) * radius / 128.0f);
    *outY = axisToRaw((y / len) * radius / 128.0f);
}
}

// ---- bindings API --------------------------------------------------------------------------------------------------
// PORT: built-in defaults (also written to controls.ini). Game actions in the comments are the KEY ASSIGN defaults
// of FUN_00100f10 (RUNTIME.md 2.4); the PS2 button is what is bound, so a remap in KEY ASSIGN follows automatically.
Controls defaultControls() {
    Controls c;
    InputBinding* b = c.bind;
    setKeys(b[IN_CROSS], KEY_SPACE);                         // Special Attack (AT12: kick bare-handed) / menu confirm
    setKeys(b[IN_SQUARE], KEY_C);                            // Attack (combo AT18/AT13/AT06, hold = charge)
    setKeys(b[IN_CIRCLE], KEY_F, KEY_BACKSPACE);             // talk / examine / conversation camera / menu back
    setKeys(b[IN_TRIANGLE], KEY_TAB);                        // Item screen
    setKeys(b[IN_L1], KEY_Q);                                // previous item slot
    setKeys(b[IN_R1], KEY_E);                                // next item slot
    setKeys(b[IN_L2], KEY_LEFT_SHIFT);                       // Target lock-on (held)
    setKeys(b[IN_R2], KEY_G);                                // Throw item (hold = aim)
    setKeys(b[IN_R3], KEY_R);                                // camera reset
    setKeys(b[IN_SELECT], KEY_M);                            // floor map
    setKeys(b[IN_START], KEY_ENTER, KEY_KP_ENTER);             // pause / skip / PRESS START (Esc: mouse_menu escBits)
    setKeys(b[IN_UP], KEY_UP);
    setKeys(b[IN_RIGHT], KEY_RIGHT);
    setKeys(b[IN_DOWN], KEY_DOWN);
    setKeys(b[IN_LEFT], KEY_LEFT);
    setKeys(b[IN_LS_UP], KEY_W);
    setKeys(b[IN_LS_DOWN], KEY_S);
    setKeys(b[IN_LS_LEFT], KEY_A);
    setKeys(b[IN_LS_RIGHT], KEY_D);
    setKeys(b[IN_RS_UP], KEY_I);
    setKeys(b[IN_RS_DOWN], KEY_K);
    setKeys(b[IN_RS_LEFT], KEY_J);
    setKeys(b[IN_RS_RIGHT], KEY_L);
    setKeys(b[IN_WALK], KEY_LEFT_CONTROL);
    // Left button = the original "Attack" (KEY ASSIGN row 0x5c, slot 0x4e65d0, default Square: the punch / sword combo,
    // hold = charge); right button = "Special Attack" (row 0x5d, slot 0x4e65c8, default Cross: AT12, the kick
    // bare-handed). Menus never see these (a free cursor uses the mouse menu layer: left = Cross, right = Circle).
    b[IN_SQUARE].mouse[0] = MB_LEFT;
    b[IN_CROSS].mouse[0] = MB_RIGHT;
    b[IN_R3].mouse[0] = MB_MIDDLE;
    b[IN_L1].mouse[0] = MB_WHEEL_UP;
    b[IN_R1].mouse[0] = MB_WHEEL_DOWN;
    // Gamepad 1:1 on the PS2 layout (raylib/GLFW SDL mapping: right face down = Cross / Xbox A, ...).
    b[IN_UP].pad[0] = GAMEPAD_BUTTON_LEFT_FACE_UP;
    b[IN_RIGHT].pad[0] = GAMEPAD_BUTTON_LEFT_FACE_RIGHT;
    b[IN_DOWN].pad[0] = GAMEPAD_BUTTON_LEFT_FACE_DOWN;
    b[IN_LEFT].pad[0] = GAMEPAD_BUTTON_LEFT_FACE_LEFT;
    b[IN_TRIANGLE].pad[0] = GAMEPAD_BUTTON_RIGHT_FACE_UP;
    b[IN_CIRCLE].pad[0] = GAMEPAD_BUTTON_RIGHT_FACE_RIGHT;
    b[IN_CROSS].pad[0] = GAMEPAD_BUTTON_RIGHT_FACE_DOWN;
    b[IN_SQUARE].pad[0] = GAMEPAD_BUTTON_RIGHT_FACE_LEFT;
    b[IN_L1].pad[0] = GAMEPAD_BUTTON_LEFT_TRIGGER_1;
    b[IN_R1].pad[0] = GAMEPAD_BUTTON_RIGHT_TRIGGER_1;
    b[IN_L2].pad[0] = GAMEPAD_BUTTON_LEFT_TRIGGER_2;
    b[IN_R2].pad[0] = GAMEPAD_BUTTON_RIGHT_TRIGGER_2;
    b[IN_SELECT].pad[0] = GAMEPAD_BUTTON_MIDDLE_LEFT;
    b[IN_START].pad[0] = GAMEPAD_BUTTON_MIDDLE_RIGHT;
    b[IN_L3].pad[0] = GAMEPAD_BUTTON_LEFT_THUMB;
    b[IN_R3].pad[0] = GAMEPAD_BUTTON_RIGHT_THUMB;
    return c;
}
const Controls& controls() { ensureLoaded(); return gControls; }
void setControls(const Controls& c) { gLoaded = true; gControls = c; }
void setControlsLoader(void (*loader)()) { gLoader = loader; }

void mouseLookDegrees(float dxPixels, float dyPixels, float* yawDeg, float* pitchDeg) {
    ensureLoaded();
    *yawDeg = dxPixels * gControls.mouseSensX * (gControls.mouseInvertX ? -1.0f : 1.0f);
    *pitchDeg = dyPixels * gControls.mouseSensY * (gControls.mouseInvertY ? -1.0f : 1.0f);
}

// PORT: free mouse camera mode (controls.hpp).
namespace {
bool gLastGamepad = false;   // last input device: true = gamepad, false = keyboard / mouse (start: keyboard / mouse)
int gFreeCamOverride = -1;   // test override (main.cpp --shot runs default to the original camera)
}
bool lastInputGamepad() { return gLastGamepad; }
void setLastInputGamepad(bool gamepad) { gLastGamepad = gamepad; }
void setFreeCameraOverride(int mode) { gFreeCamOverride = mode; }
bool freeCameraEnabled() {
    ensureLoaded();
    const int mode = gFreeCamOverride >= 0 ? gFreeCamOverride : gControls.freeCamera;
    if (mode == 1) return true;
    if (mode == 2) return false;
    return !gLastGamepad;
}

void inputHostFrame() {
    ensureLoaded();
    if (!IsWindowReady()) return;
    const Controls& c = gControls;
    const bool captured = gMouseLook;   // DisableCursor() = mouse look; the mouse menu layer owns a free cursor (PORT: flag, a movie hides the free cursor too)
    const int gp = activeGamepad(c);
    for (int i = 0; i < kButtonInputs; ++i) {
        const InputBinding& b = c.bind[i];
        if (gBlocked) {   // PORT (window focus): no keyboard / mouse taps while unfocused
            if (gp >= 0 && anyPadPressed(gp, b)) gLatched |= 1u << i;
            continue;
        }
        if (anyKeyPressed(b) || (captured && anyMousePressed(b)) || (gp >= 0 && anyPadPressed(gp, b))) gLatched |= 1u << i;
    }
    {   // PORT (window focus): swallowed mouse buttons / keys are free again once released (after the latch above)
        if (gMouseSwallow) {
            bool down = false;
            for (int m = MOUSE_BUTTON_LEFT; m <= MOUSE_BUTTON_BACK; ++m) down = down || IsMouseButtonDown(m);
            if (!down) gMouseSwallow = false;
        }
        for (int& k : gSwallowKeys)
            if (k != 0 && !IsKeyDown(k)) k = 0;
    }
    {   // PORT: last input device for the free mouse camera (freeCameraEnabled)
        bool kbm = false, pad = false;
        for (int i = 0; i < IN_COUNT && !kbm; ++i) kbm = anyKeyDown(c.bind[i]);
        if (captured) {
            const Vector2 md = GetMouseDelta();
            if (md.x != 0.0f || md.y != 0.0f) kbm = true;
            for (int m = MB_LEFT; m <= MB_BACK && !kbm; ++m) kbm = mouseButtonDown(m);
        }
        if (gp >= 0) {
            for (int i = 0; i < kButtonInputs && !pad; ++i) pad = anyPadDown(c, gp, c.bind[i]);
            const int axes[4] = {GAMEPAD_AXIS_LEFT_X, GAMEPAD_AXIS_LEFT_Y, GAMEPAD_AXIS_RIGHT_X, GAMEPAD_AXIS_RIGHT_Y};
            for (int a : axes) if (std::fabs(GetGamepadAxisMovement(gp, a)) > 0.4f) pad = true;
        }
        if (pad && !kbm) gLastGamepad = true;
        else if (kbm && !pad) gLastGamepad = false;
    }
    if (captured) {
        const float wheel = GetMouseWheelMove();
        if (wheel > 0.0f) gWheelUp += static_cast<int>(std::ceil(wheel));
        if (wheel < 0.0f) gWheelDown += static_cast<int>(std::ceil(-wheel));
        if (gWheelUp > 4) gWheelUp = 4;      // a fast spin should not queue seconds of presses
        if (gWheelDown > 4) gWheelDown = 4;
    } else {
        gWheelUp = gWheelDown = 0;
        gWheelGap = false;
    }
}

// PORT (window focus): see controls.hpp
void setInputBlocked(bool blocked, bool gamepad) {
    gBlocked = blocked;
    gBlockedPadOk = gamepad;
    if (blocked) { gLatched = 0; gWheelUp = gWheelDown = 0; gWheelGap = false; }
}
void swallowMouseUntilRelease() { gMouseSwallow = true; }
void setMouseLookCaptured(bool captured) { gMouseLook = captured; }
bool mouseSwallowed() { return gMouseSwallow; }
void swallowKeyUntilRelease(int key) {
    if (key == 0 || keySwallowed(key)) return;
    for (int& k : gSwallowKeys)
        if (k == 0) { k = key; return; }
}
bool anyGameInputPressed() {
    ensureLoaded();
    if (!IsWindowReady() || gBlocked) return false;
    const Controls& c = gControls;
    const int gp = activeGamepad(c);
    for (int i = 0; i < IN_COUNT; ++i) {
        if (anyKeyPressed(c.bind[i])) return true;
        if (gp >= 0 && i < kButtonInputs && anyPadPressed(gp, c.bind[i])) return true;
    }
    return false;
}

void injectButtons(uint32_t mask, int polls) { gInjectMask = mask; gInjectPolls = polls; }
void setPollHook(uint32_t (*hook)()) { gPollHook = hook; }   // PORT: mouse menu layer
void injectStick(int lx, int ly, int polls) { gStickLx = lx; gStickLy = ly; gStickPolls = polls; }   // test hook

PadSample pollInput() {
    ensureLoaded();
    PadSample s;
    if (gInjectPolls > 0) { s.buttons |= gInjectMask; --gInjectPolls; }
    if (gPollHook != nullptr) s.buttons |= gPollHook();   // PORT: synthesised mouse menu presses (0 when idle)
    const Controls& c = gControls;
    const bool window = IsWindowReady();
    if (window && gBlocked) {   // PORT (window focus): unfocused -> latched gamepad taps + gamepad (if allowed) only
        s.buttons |= gLatched;
        gLatched = 0;
        if (const int gp = activeGamepad(c); gp >= 0) {
            for (int i = 0; i < kButtonInputs; ++i) if (anyPadDown(c, gp, c.bind[i])) s.buttons |= 1u << i;
            s.lx = axisToRaw(GetGamepadAxisMovement(gp, GAMEPAD_AXIS_LEFT_X));
            s.ly = axisToRaw(GetGamepadAxisMovement(gp, GAMEPAD_AXIS_LEFT_Y));
            s.rx = axisToRaw(GetGamepadAxisMovement(gp, GAMEPAD_AXIS_RIGHT_X));
            s.ry = axisToRaw(GetGamepadAxisMovement(gp, GAMEPAD_AXIS_RIGHT_Y));
        }
    } else if (window) {
        const bool captured = gMouseLook;   // PORT: see setMouseLookCaptured
        uint32_t bits = gLatched;
        gLatched = 0;
        for (int i = 0; i < kButtonInputs; ++i) {
            const InputBinding& b = c.bind[i];
            if (anyKeyDown(b) || (captured && anyMouseDown(b))) bits |= 1u << i;
        }
        // wheel: one notch per sample, then one empty sample (separate pressed edges for L1/R1 item cycling)
        if (gWheelGap) {
            gWheelGap = false;
        } else if (gWheelUp > 0 || gWheelDown > 0) {
            const int code = gWheelUp > 0 ? MB_WHEEL_UP : MB_WHEEL_DOWN;
            if (gWheelUp > 0) --gWheelUp; else --gWheelDown;
            for (int i = 0; i < kButtonInputs; ++i) if (bindsMouse(c.bind[i], code)) bits |= 1u << i;
            gWheelGap = true;
        }
        s.buttons |= bits;
        // keyboard sticks; Walk = the reduced radius 80 + 48*m (FUN_0020bc90 magnitude m < 0.9 -> walk speed 3.0)
        const bool walk = anyKeyDown(c.bind[IN_WALK]) || (captured && anyMouseDown(c.bind[IN_WALK]));
        float walkMag = c.walkMagnitude;
        if (walkMag < 0.05f) walkMag = 0.05f;
        if (walkMag > 0.85f) walkMag = 0.85f;
        keyStick(c, IN_LS_UP, IN_LS_DOWN, IN_LS_LEFT, IN_LS_RIGHT, walk ? 80.0f + 48.0f * walkMag : 128.0f, &s.lx, &s.ly);
        keyStick(c, IN_RS_UP, IN_RS_DOWN, IN_RS_LEFT, IN_RS_RIGHT, 128.0f, &s.rx, &s.ry);
        // gamepad passthrough: buttons by binding, sticks raw (the PS2 dead zone r < 80 applies in game/pad.cpp)
        const int gp = activeGamepad(c);
        if (gp >= 0) {
            for (int i = 0; i < kButtonInputs; ++i) if (anyPadDown(c, gp, c.bind[i])) s.buttons |= 1u << i;
            const int lx = axisToRaw(GetGamepadAxisMovement(gp, GAMEPAD_AXIS_LEFT_X));
            const int ly = axisToRaw(GetGamepadAxisMovement(gp, GAMEPAD_AXIS_LEFT_Y));
            const int rx = axisToRaw(GetGamepadAxisMovement(gp, GAMEPAD_AXIS_RIGHT_X));
            const int ry = axisToRaw(GetGamepadAxisMovement(gp, GAMEPAD_AXIS_RIGHT_Y));
            if (r2(lx, ly) > r2(s.lx, s.ly)) { s.lx = lx; s.ly = ly; }   // the larger deflection wins
            if (r2(rx, ry) > r2(s.rx, s.ry)) { s.rx = rx; s.ry = ry; }
        }
    }
    if (gStickPolls > 0) { s.lx = gStickLx; s.ly = gStickLy; --gStickPolls; }   // test hook (injectStick)
    return s;
}
}
