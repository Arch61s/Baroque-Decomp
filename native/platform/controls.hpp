#pragma once
// PORT: PC control bindings (keyboard, mouse, gamepad) -> PS2 pad sample. Not part of the original game.
//
// The game only ever sees the libpad sample (platform/input.hpp PadSample: SCE_PAD button word + raw sticks).
// This layer decides which PC inputs produce which PS2 pad input. It sits BELOW the original KEY ASSIGN page
// (game/options.cpp ButtonConfig 0x4e65c0..0x4e65f8), which maps PS2 buttons to game actions; so a PC key bound to
// "cross" performs whatever action KEY ASSIGN currently gives to Cross (Special Attack by default;
// the original's main "Attack" is Square).
//
// Storage: <exe dir>/controls.ini, written with the documented defaults when missing (work/reports/CONTROLS_NATIVE.md).
// input.cpp owns the live Controls and the built-in defaults, so targets that compile input.cpp without
// controls.cpp (Baroque-PlayerControlCheck, Baroque-TestArea) still link and simply use the defaults.
#include <cstdint>
#include <filesystem>
#include <string>
namespace baroque::platform {

// Bindable PS2 pad inputs. 0..15 are the SCE_PAD bits (1u << index, RUNTIME.md 2.2); 16..23 drive the analog sticks
// at full deflection (the stick -> digital bits then come from FUN_0020bd50 in game/pad.cpp, as on the PS2);
// Walk reduces the keyboard left-stick deflection (FUN_0013cf90: magnitude < 0.9 walks at 3.0, >= 0.9 runs at 12.0).
enum PadInput : int {
    IN_L2 = 0, IN_R2, IN_L1, IN_R1, IN_TRIANGLE, IN_CIRCLE, IN_CROSS, IN_SQUARE,
    IN_SELECT, IN_L3, IN_R3, IN_START, IN_UP, IN_RIGHT, IN_DOWN, IN_LEFT,
    IN_LS_UP, IN_LS_DOWN, IN_LS_LEFT, IN_LS_RIGHT, IN_RS_UP, IN_RS_DOWN, IN_RS_LEFT, IN_RS_RIGHT,
    IN_WALK, IN_COUNT
};
constexpr int kButtonInputs = 16;   // inputs that are plain SCE_PAD bits (the only ones a gamepad button can feed)

// Mouse binding codes (0 = empty). Buttons are raylib MouseButton + 1; the wheel gives one press per notch.
enum MouseBind : int {
    MB_NONE = 0, MB_LEFT, MB_RIGHT, MB_MIDDLE, MB_SIDE, MB_EXTRA, MB_FORWARD, MB_BACK, MB_WHEEL_UP, MB_WHEEL_DOWN
};

constexpr int kMaxKeys = 4, kMaxMouse = 2, kMaxPadButtons = 2;
struct InputBinding {
    int keys[kMaxKeys] = {};          // raylib KeyboardKey, 0 = empty
    int mouse[kMaxMouse] = {};        // MouseBind, 0 = empty (only while the cursor is captured for mouse look)
    int pad[kMaxPadButtons] = {};     // raylib GamepadButton, 0 = empty (button inputs 0..15 only)
};

struct Controls {
    InputBinding bind[IN_COUNT];
    float walkMagnitude = 0.6f;       // libpad magnitude ((r-80)/48) of the keyboard stick while Walk is held, 0.05..0.85
    float mouseSensX = 0.15f;         // camera yaw degrees per mouse pixel (main.cpp mouse look)
    float mouseSensY = 0.12f;         // camera pitch degrees per mouse pixel
    bool mouseInvertY = false;
    bool mouseInvertX = false;        // PORT: reverse the mouse-look yaw
    int freeCamera = 0;               // PORT free mouse camera (game/free_camera.hpp): 0 auto (keyboard / mouse was the
                                      // last input device), 1 always, 2 never (original camera only)
    bool gamepadEnabled = true;
    int gamepadIndex = -1;            // -1 = first connected gamepad, else raylib gamepad 0..3
    float triggerThreshold = 0.3f;    // analog trigger travel (0..1) that counts as L2/R2 held
    int releaseCursorKey = 0x122;     // KEY_F1: toggles the captured mouse-look cursor (main.cpp)
    // PORT keyboard + mouse attacks (game/aim_assist.*; only while the free mouse camera is active, never on a pad):
    int aimAssist = 1;                // [aim] aim_assist: 0 off, 1 subtle, 2 strong
    bool cameraRelativeAttacks = true;// [aim] camera_relative_attacks: attacks / shots go where the camera looks
};

// --- input.cpp (always linked with the pad sampler) ---
Controls defaultControls();
const Controls& controls();           // live bindings (loads controls.ini on first use when controls.cpp is linked)
void setControls(const Controls& c);  // e.g. from a future in-game settings page (call saveControls to persist)
// PORT: once per presented host frame (after raylib polled events, after the cursor capture is decided): latches
// taps shorter than one 30 Hz tick and queues mouse wheel notches. Without it, short taps can be missed and the
// wheel bindings are inert; everything else works.
void inputHostFrame();
// PORT: mouse-look scaling for main.cpp: window pixels -> camera degrees (sensitivity + invert from controls.ini).
// yawDeg > 0 = turn the view right (mouse moved right), pitchDeg > 0 = look down (mouse moved toward the user).
void mouseLookDegrees(float dxPixels, float dyPixels, float* yawDeg, float* pitchDeg);
// PORT: free mouse camera mode. inputHostFrame() tracks the last input device (gamepad button / stick vs keyboard key /
// mouse motion or button); freeCameraEnabled() = controls.ini free_camera (auto: last device != gamepad) unless a test
// override is set (setFreeCameraOverride: -1 none, 1 on, 2 off).
bool lastInputGamepad();
void setLastInputGamepad(bool gamepad);   // PORT (media captures): --input-device
bool freeCameraEnabled();
void setFreeCameraOverride(int mode);
// PORT (window focus, main.cpp / PC_SETTINGS.md "Window focus"): setInputBlocked(true) while the window is unfocused
// (no keyboard / mouse / gamepad in the pad sample; gamepad too unless `gamepad` is set = baroque_port.ini
// background_gamepad); test injections and the poll hook still pass. swallowMouseUntilRelease(): mouse buttons are
// ignored until every button is up (the click that activates the window or recaptures the cursor is not a game
// press). swallowKeyUntilRelease(key): the same for one key (Alt+Enter must not also press START).
// anyGameInputPressed(): a bound key or gamepad button was pressed this host frame (the focus-regain recapture).
void setInputBlocked(bool blocked, bool gamepad = false);
void swallowMouseUntilRelease();
bool mouseSwallowed();
void swallowKeyUntilRelease(int key);
bool anyGameInputPressed();
// PORT (main.cpp cursor capture): the mouse-look capture state (DisableCursor). The mouse buttons and wheel are game
// inputs only while it is set; IsCursorHidden() is not used for that because a movie also hides the free cursor.
void setMouseLookCaptured(bool captured);
// Installed by controls.cpp at static init; input.cpp calls it once before the first use of the bindings.
void setControlsLoader(void (*loader)());

// --- controls.cpp (Baroque-Native only) ---
std::filesystem::path controlsPath();                       // <exe dir>/controls.ini
Controls loadControls(bool writeDefaultsIfMissing);         // defaults for anything missing / malformed
bool saveControls(const Controls& c);
std::string formatControls(const Controls& c);              // the documented ini text
Controls parseControls(const std::string& text, std::string* warnings);
const char* padInputName(int input);                        // ini name ("cross", "lstick_up", "walk", ...)
std::string keyName(int key);                               // "SPACE", "A", "F1", ... or the number
int keyFromName(const std::string& name);                   // 0 if unknown
}
