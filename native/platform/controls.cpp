#include "platform/controls.hpp"
#include "platform/platform.hpp"
#include "raylib.h"
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
// PORT: controls.ini <-> Controls (bindings of PC inputs to PS2 pad inputs). Not part of the original game.
namespace baroque::platform {
namespace {
struct Name { int code; const char* name; };
const Name kKeys[] = {
    {KEY_APOSTROPHE, "APOSTROPHE"}, {KEY_COMMA, "COMMA"}, {KEY_MINUS, "MINUS"}, {KEY_PERIOD, "PERIOD"},
    {KEY_SLASH, "SLASH"}, {KEY_ZERO, "0"}, {KEY_ONE, "1"}, {KEY_TWO, "2"}, {KEY_THREE, "3"}, {KEY_FOUR, "4"},
    {KEY_FIVE, "5"}, {KEY_SIX, "6"}, {KEY_SEVEN, "7"}, {KEY_EIGHT, "8"}, {KEY_NINE, "9"}, {KEY_SEMICOLON, "SEMICOLON"},
    {KEY_EQUAL, "EQUAL"}, {KEY_A, "A"}, {KEY_B, "B"}, {KEY_C, "C"}, {KEY_D, "D"}, {KEY_E, "E"}, {KEY_F, "F"},
    {KEY_G, "G"}, {KEY_H, "H"}, {KEY_I, "I"}, {KEY_J, "J"}, {KEY_K, "K"}, {KEY_L, "L"}, {KEY_M, "M"}, {KEY_N, "N"},
    {KEY_O, "O"}, {KEY_P, "P"}, {KEY_Q, "Q"}, {KEY_R, "R"}, {KEY_S, "S"}, {KEY_T, "T"}, {KEY_U, "U"}, {KEY_V, "V"},
    {KEY_W, "W"}, {KEY_X, "X"}, {KEY_Y, "Y"}, {KEY_Z, "Z"}, {KEY_LEFT_BRACKET, "LEFT_BRACKET"},
    {KEY_BACKSLASH, "BACKSLASH"}, {KEY_RIGHT_BRACKET, "RIGHT_BRACKET"}, {KEY_GRAVE, "GRAVE"}, {KEY_SPACE, "SPACE"},
    {KEY_ESCAPE, "ESCAPE"}, {KEY_ENTER, "ENTER"}, {KEY_TAB, "TAB"}, {KEY_BACKSPACE, "BACKSPACE"},
    {KEY_INSERT, "INSERT"}, {KEY_DELETE, "DELETE"}, {KEY_RIGHT, "RIGHT"}, {KEY_LEFT, "LEFT"}, {KEY_DOWN, "DOWN"},
    {KEY_UP, "UP"}, {KEY_PAGE_UP, "PAGE_UP"}, {KEY_PAGE_DOWN, "PAGE_DOWN"}, {KEY_HOME, "HOME"}, {KEY_END, "END"},
    {KEY_CAPS_LOCK, "CAPS_LOCK"}, {KEY_SCROLL_LOCK, "SCROLL_LOCK"}, {KEY_NUM_LOCK, "NUM_LOCK"},
    {KEY_PRINT_SCREEN, "PRINT_SCREEN"}, {KEY_PAUSE, "PAUSE"}, {KEY_F1, "F1"}, {KEY_F2, "F2"}, {KEY_F3, "F3"},
    {KEY_F4, "F4"}, {KEY_F5, "F5"}, {KEY_F6, "F6"}, {KEY_F7, "F7"}, {KEY_F8, "F8"}, {KEY_F9, "F9"}, {KEY_F10, "F10"},
    {KEY_F11, "F11"}, {KEY_F12, "F12"}, {KEY_LEFT_SHIFT, "LEFT_SHIFT"}, {KEY_LEFT_CONTROL, "LEFT_CONTROL"},
    {KEY_LEFT_ALT, "LEFT_ALT"}, {KEY_LEFT_SUPER, "LEFT_SUPER"}, {KEY_RIGHT_SHIFT, "RIGHT_SHIFT"},
    {KEY_RIGHT_CONTROL, "RIGHT_CONTROL"}, {KEY_RIGHT_ALT, "RIGHT_ALT"}, {KEY_RIGHT_SUPER, "RIGHT_SUPER"},
    {KEY_KB_MENU, "MENU"}, {KEY_KP_0, "KP_0"}, {KEY_KP_1, "KP_1"}, {KEY_KP_2, "KP_2"}, {KEY_KP_3, "KP_3"},
    {KEY_KP_4, "KP_4"}, {KEY_KP_5, "KP_5"}, {KEY_KP_6, "KP_6"}, {KEY_KP_7, "KP_7"}, {KEY_KP_8, "KP_8"},
    {KEY_KP_9, "KP_9"}, {KEY_KP_DECIMAL, "KP_DECIMAL"}, {KEY_KP_DIVIDE, "KP_DIVIDE"},
    {KEY_KP_MULTIPLY, "KP_MULTIPLY"}, {KEY_KP_SUBTRACT, "KP_SUBTRACT"}, {KEY_KP_ADD, "KP_ADD"},
    {KEY_KP_ENTER, "KP_ENTER"}, {KEY_KP_EQUAL, "KP_EQUAL"},
};
const Name kMouse[] = {
    {MB_LEFT, "LEFT"}, {MB_RIGHT, "RIGHT"}, {MB_MIDDLE, "MIDDLE"}, {MB_SIDE, "SIDE"}, {MB_EXTRA, "EXTRA"},
    {MB_FORWARD, "FORWARD"}, {MB_BACK, "BACK"}, {MB_WHEEL_UP, "WHEEL_UP"}, {MB_WHEEL_DOWN, "WHEEL_DOWN"},
};
const Name kPadButtons[] = {
    {GAMEPAD_BUTTON_LEFT_FACE_UP, "LEFT_FACE_UP"}, {GAMEPAD_BUTTON_LEFT_FACE_RIGHT, "LEFT_FACE_RIGHT"},
    {GAMEPAD_BUTTON_LEFT_FACE_DOWN, "LEFT_FACE_DOWN"}, {GAMEPAD_BUTTON_LEFT_FACE_LEFT, "LEFT_FACE_LEFT"},
    {GAMEPAD_BUTTON_RIGHT_FACE_UP, "RIGHT_FACE_UP"}, {GAMEPAD_BUTTON_RIGHT_FACE_RIGHT, "RIGHT_FACE_RIGHT"},
    {GAMEPAD_BUTTON_RIGHT_FACE_DOWN, "RIGHT_FACE_DOWN"}, {GAMEPAD_BUTTON_RIGHT_FACE_LEFT, "RIGHT_FACE_LEFT"},
    {GAMEPAD_BUTTON_LEFT_TRIGGER_1, "LEFT_TRIGGER_1"}, {GAMEPAD_BUTTON_LEFT_TRIGGER_2, "LEFT_TRIGGER_2"},
    {GAMEPAD_BUTTON_RIGHT_TRIGGER_1, "RIGHT_TRIGGER_1"}, {GAMEPAD_BUTTON_RIGHT_TRIGGER_2, "RIGHT_TRIGGER_2"},
    {GAMEPAD_BUTTON_MIDDLE_LEFT, "MIDDLE_LEFT"}, {GAMEPAD_BUTTON_MIDDLE, "MIDDLE"},
    {GAMEPAD_BUTTON_MIDDLE_RIGHT, "MIDDLE_RIGHT"}, {GAMEPAD_BUTTON_LEFT_THUMB, "LEFT_THUMB"},
    {GAMEPAD_BUTTON_RIGHT_THUMB, "RIGHT_THUMB"},
};
// ini names, index = PadInput
const char* const kInputNames[IN_COUNT] = {
    "l2", "r2", "l1", "r1", "triangle", "circle", "cross", "square", "select", "l3", "r3", "start",
    "dpad_up", "dpad_right", "dpad_down", "dpad_left", "lstick_up", "lstick_down", "lstick_left", "lstick_right",
    "rstick_up", "rstick_down", "rstick_left", "rstick_right", "walk"};
// what the PS2 input does with the KEY ASSIGN defaults (FUN_00100f10, RUNTIME.md 2.4/2.5)
const char* const kInputDoc[IN_COUNT] = {
    "Target lock-on (held)", "Throw item (hold = aim, release = throw)", "Previous item slot", "Next item slot",
    "Item screen", "Conversation camera (talks start by walking up to an NPC's front; no talk button) / examine / menu back",
    "Special Attack (kick bare-handed) / menu confirm", "Attack (combo; hold = charge)", "Floor map", "(not used by the game)", "Camera reset behind the player",
    "Pause / skip event / PRESS START / name entry END", "Menu cursor / D-pad move", "Menu cursor / D-pad move",
    "Menu cursor / D-pad move", "Menu cursor / D-pad move", "Move", "Move", "Move", "Move", "Camera pitch",
    "Camera pitch", "Camera yaw", "Camera yaw", "Walk instead of run (reduced left-stick magnitude)"};

std::string upper(std::string s) {
    for (char& ch : s) ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
    return s;
}
std::string trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return s.substr(a, b - a);
}
int lookup(const Name* table, size_t n, const std::string& name) {
    const std::string u = upper(name);
    for (size_t i = 0; i < n; ++i) if (u == table[i].name) return table[i].code;
    return 0;
}
std::string nameOf(const Name* table, size_t n, int code) {
    for (size_t i = 0; i < n; ++i) if (table[i].code == code) return table[i].name;
    return std::to_string(code);
}
template <size_t N> int lookupIn(const Name (&t)[N], const std::string& s) { return lookup(t, N, s); }
template <size_t N> std::string nameIn(const Name (&t)[N], int code) { return nameOf(t, N, code); }

int inputFromName(const std::string& name) {
    for (int i = 0; i < IN_COUNT; ++i) if (name == kInputNames[i]) return i;
    return -1;
}
std::string joinCodes(const int* codes, int n, std::string (*name)(int)) {
    std::string out;
    for (int i = 0; i < n; ++i) {
        if (codes[i] == 0) continue;
        if (!out.empty()) out += ", ";
        out += name(codes[i]);
    }
    return out.empty() ? "NONE" : out;
}
std::string mouseName(int code) { return nameIn(kMouse, code); }
std::string padName(int code) { return nameIn(kPadButtons, code); }
std::string fmtFloat(float v) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.3f", static_cast<double>(v));
    return buf;
}
// "A, B, C" -> codes (0 = unknown name, reported); NONE / empty -> nothing
void parseList(const std::string& value, int* out, int max, int (*resolve)(const std::string&), const std::string& where,
               std::string* warn) {
    for (int i = 0; i < max; ++i) out[i] = 0;
    std::stringstream ss(value);
    std::string item;
    int n = 0;
    while (std::getline(ss, item, ',')) {
        item = trim(item);
        if (item.empty() || upper(item) == "NONE") continue;
        int code = resolve(item);
        if (code == 0 && std::isdigit(static_cast<unsigned char>(item[0]))) code = std::atoi(item.c_str());
        if (code == 0) { if (warn) *warn += where + ": unknown input '" + item + "'\n"; continue; }
        if (n >= max) { if (warn) *warn += where + ": more than " + std::to_string(max) + " inputs, '" + item + "' ignored\n"; continue; }
        out[n++] = code;
    }
}
int resolveKey(const std::string& s) { return keyFromName(s); }
int resolveMouse(const std::string& s) { return lookupIn(kMouse, s); }
int resolvePad(const std::string& s) { return lookupIn(kPadButtons, s); }
bool parseBool(const std::string& v) { const std::string u = upper(v); return u == "1" || u == "TRUE" || u == "YES" || u == "ON"; }
}

const char* padInputName(int input) { return input >= 0 && input < IN_COUNT ? kInputNames[input] : "?"; }
std::string keyName(int key) {
    if (key == 0) return "NONE";
    return nameIn(kKeys, key);
}
int keyFromName(const std::string& name) {
    const int k = lookupIn(kKeys, name);
    if (k != 0) return k;
    if (name.size() == 1) {   // single character keys equal their upper-case ASCII code in raylib (A..Z, 0..9, ...)
        const int c = std::toupper(static_cast<unsigned char>(name[0]));
        if (c > 32 && c < 97) return c;
    }
    return 0;
}

std::filesystem::path controlsPath() { return std::filesystem::path(GetApplicationDirectory()) / "controls.ini"; }

std::string formatControls(const Controls& c) {
    std::ostringstream o;
    o << "; controls.ini - Baroque native port: PC controls -> PlayStation 2 pad (port-only file, not in the original game)\n"
         ";\n"
         "; The game itself only ever reads a PS2 DualShock 2. Each line below says which PC inputs press one PS2 pad input.\n"
         "; WHICH GAME ACTION a PS2 button performs is still decided by the in-game KEY ASSIGN page\n"
         "; (Option / GAME CONFIG / KEY ASSIGN); the comments show what each PS2 input does with its defaults.\n"
         ";\n"
         "; Syntax:  name = INPUT, INPUT, ...   (up to 4 keys / 2 mouse inputs / 2 gamepad buttons; NONE = unbound)\n"
         "; A missing line keeps the built-in default. Delete this file to restore every default (it is rewritten at start).\n"
         "; Key names: A..Z 0..9 F1..F12 SPACE ENTER ESCAPE TAB BACKSPACE INSERT DELETE HOME END PAGE_UP PAGE_DOWN\n"
         ";   UP DOWN LEFT RIGHT LEFT_SHIFT RIGHT_SHIFT LEFT_CONTROL RIGHT_CONTROL LEFT_ALT RIGHT_ALT CAPS_LOCK GRAVE MINUS\n"
         ";   EQUAL LEFT_BRACKET RIGHT_BRACKET BACKSLASH SEMICOLON APOSTROPHE COMMA PERIOD SLASH KP_0..KP_9 KP_ENTER\n"
         ";   KP_ADD KP_SUBTRACT KP_MULTIPLY KP_DIVIDE KP_DECIMAL (or a raylib key number)\n"
         "\n[keyboard]\n";
    for (int i = 0; i < IN_COUNT; ++i) {
        std::string line = std::string(kInputNames[i]) + " = " + joinCodes(c.bind[i].keys, kMaxKeys, &keyName);
        if (line.size() < 40) line.append(40 - line.size(), ' ');
        o << line << "; " << kInputDoc[i] << "\n";
    }
    auto col = [](std::string s) { if (s.size() < 40) s.append(40 - s.size(), ' '); return s; };
    o << col("walk_magnitude = " + fmtFloat(c.walkMagnitude))
      << "; left-stick magnitude while walk is held (0.05..0.85; the game walks below 0.90)\n"
      << col("release_cursor = " + keyName(c.releaseCursorKey)) << "; frees / recaptures the mouse-look cursor in game\n";
    o << "\n[mouse]\n"
         "; Mouse buttons and wheel count only while the cursor is captured for mouse look (in game, no menu open).\n"
         "; With a free cursor the menus take the mouse directly (point, left click = confirm, right click = back, wheel).\n"
         "; Names: LEFT RIGHT MIDDLE SIDE EXTRA FORWARD BACK WHEEL_UP WHEEL_DOWN (one press per wheel notch)\n";
    for (int i = 0; i < IN_COUNT; ++i) {
        if (i >= IN_LS_UP && i <= IN_RS_RIGHT) continue;   // the mouse itself is the camera; no stick directions
        o << kInputNames[i] << " = " << joinCodes(c.bind[i].mouse, kMaxMouse, &mouseName) << "\n";
    }
    o << "sensitivity_x = " << fmtFloat(c.mouseSensX) << "   ; camera yaw degrees per mouse pixel\n"
      << "sensitivity_y = " << fmtFloat(c.mouseSensY) << "   ; camera pitch degrees per mouse pixel\n"
      << "invert_y = " << (c.mouseInvertY ? 1 : 0) << "\n"
      << "invert_x = " << (c.mouseInvertX ? 1 : 0) << "\n"
      << "free_camera = " << (c.freeCamera == 1 ? "on" : (c.freeCamera == 2 ? "off" : "auto"))
      << "   ; mouse orbit camera: auto (while keyboard / mouse is used; a gamepad switches to the original camera), on, off\n";
    o << "\n[gamepad]\n"
         "; raylib / XInput gamepad, PS2 layout 1:1 by default. Sticks are passed through raw (left stick = move,\n"
         "; right stick = camera, with the original PS2 dead zone). Names: LEFT_FACE_UP/RIGHT/DOWN/LEFT (D-pad),\n"
         "; RIGHT_FACE_UP/RIGHT/DOWN/LEFT (Triangle/Circle/Cross/Square = Xbox Y/B/A/X), LEFT_TRIGGER_1/2, RIGHT_TRIGGER_1/2,\n"
         "; MIDDLE_LEFT (Select/Back), MIDDLE (guide), MIDDLE_RIGHT (Start), LEFT_THUMB, RIGHT_THUMB\n"
      << "enabled = " << (c.gamepadEnabled ? 1 : 0) << "\n"
      << "index = " << c.gamepadIndex << "                 ; -1 = first connected gamepad, else 0..3\n"
      << "trigger_threshold = " << fmtFloat(c.triggerThreshold) << " ; analog trigger travel (0..1) that holds L2 / R2\n";
    for (int i = 0; i < kButtonInputs; ++i) o << kInputNames[i] << " = " << joinCodes(c.bind[i].pad, kMaxPadButtons, &padName) << "\n";
    o << "\n[aim]\n"   // PORT keyboard + mouse attacks (game/aim_assist.*)
         "; Keyboard + mouse only (while the mouse orbit camera is in use); a gamepad always plays exactly like the PS2.\n"
      << "camera_relative_attacks = " << (c.cameraRelativeAttacks ? "on" : "off")
      << "   ; on: attacks and gun shots go where the camera looks; off: along the character's facing (original)\n"
      << "aim_assist = " << (c.aimAssist == 0 ? "off" : (c.aimAssist == 2 ? "strong" : "subtle"))
      << "   ; off, subtle, strong: turn the hit slightly toward the nearest enemy in front of the camera\n";
    return o.str();
}

Controls parseControls(const std::string& text, std::string* warnings) {
    Controls c = defaultControls();
    std::istringstream in(text);
    std::string raw, section;
    int lineNo = 0;
    while (std::getline(in, raw)) {
        ++lineNo;
        std::string line = raw;
        const size_t cmt = line.find_first_of(";#");
        if (cmt != std::string::npos) line.resize(cmt);
        line = trim(line);
        if (line.empty()) continue;
        if (line.front() == '[' && line.back() == ']') { section = upper(trim(line.substr(1, line.size() - 2))); continue; }
        const size_t eq = line.find('=');
        const std::string where = "controls.ini:" + std::to_string(lineNo);
        if (eq == std::string::npos) { if (warnings) *warnings += where + ": expected name = value\n"; continue; }
        std::string key = trim(line.substr(0, eq));
        for (char& ch : key) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
        const std::string val = trim(line.substr(eq + 1));
        const int input = inputFromName(key);
        if (section == "KEYBOARD") {
            if (input >= 0) parseList(val, c.bind[input].keys, kMaxKeys, &resolveKey, where, warnings);
            else if (key == "walk_magnitude") c.walkMagnitude = static_cast<float>(std::atof(val.c_str()));
            else if (key == "release_cursor") { const int k = keyFromName(val); if (k != 0) c.releaseCursorKey = k; }
            else if (warnings) *warnings += where + ": unknown name '" + key + "'\n";
        } else if (section == "MOUSE") {
            if (input >= 0 && !(input >= IN_LS_UP && input <= IN_RS_RIGHT)) parseList(val, c.bind[input].mouse, kMaxMouse, &resolveMouse, where, warnings);
            else if (key == "sensitivity_x") c.mouseSensX = static_cast<float>(std::atof(val.c_str()));
            else if (key == "sensitivity_y") c.mouseSensY = static_cast<float>(std::atof(val.c_str()));
            else if (key == "invert_y") c.mouseInvertY = parseBool(val);
            else if (key == "invert_x") c.mouseInvertX = parseBool(val);
            else if (key == "free_camera") {   // PORT free mouse camera mode
                const std::string v = upper(val);
                c.freeCamera = (v == "ON" || v == "1" || v == "TRUE") ? 1 : ((v == "OFF" || v == "0" || v == "FALSE") ? 2 : 0);
            }
            else if (warnings) *warnings += where + ": unknown name '" + key + "'\n";
        } else if (section == "GAMEPAD") {
            if (input >= 0 && input < kButtonInputs) parseList(val, c.bind[input].pad, kMaxPadButtons, &resolvePad, where, warnings);
            else if (key == "enabled") c.gamepadEnabled = parseBool(val);
            else if (key == "index") c.gamepadIndex = std::atoi(val.c_str());
            else if (key == "trigger_threshold") c.triggerThreshold = static_cast<float>(std::atof(val.c_str()));
            else if (warnings) *warnings += where + ": unknown name '" + key + "'\n";
        } else if (section == "AIM") {   // PORT keyboard + mouse attacks (game/aim_assist.*)
            const std::string v = upper(val);
            if (key == "camera_relative_attacks") c.cameraRelativeAttacks = parseBool(val);
            else if (key == "aim_assist") c.aimAssist = (v == "OFF" || v == "0") ? 0 : ((v == "STRONG" || v == "2") ? 2 : 1);
            else if (warnings) *warnings += where + ": unknown name '" + key + "'\n";
        } else if (warnings) {
            *warnings += where + ": line outside [keyboard] / [mouse] / [gamepad] / [aim]\n";
        }
    }
    // sanitize
    if (!(c.walkMagnitude >= 0.05f)) c.walkMagnitude = 0.05f;
    if (c.walkMagnitude > 0.85f) c.walkMagnitude = 0.85f;
    if (!(c.triggerThreshold >= 0.05f)) c.triggerThreshold = 0.05f;
    if (c.triggerThreshold > 0.95f) c.triggerThreshold = 0.95f;
    if (c.gamepadIndex < -1 || c.gamepadIndex > 3) c.gamepadIndex = -1;
    if (!(c.mouseSensX > -10.0f && c.mouseSensX < 10.0f)) c.mouseSensX = 0.15f;
    if (!(c.mouseSensY > -10.0f && c.mouseSensY < 10.0f)) c.mouseSensY = 0.12f;
    return c;
}

bool saveControls(const Controls& c) {
    std::ofstream out(controlsPath(), std::ios::trunc | std::ios::binary);
    if (!out) { log("controls: cannot write " + controlsPath().string()); return false; }
    out << formatControls(c);
    return static_cast<bool>(out);
}

Controls loadControls(bool writeDefaultsIfMissing) {
    std::ifstream in(controlsPath(), std::ios::binary);
    if (!in) {
        const Controls d = defaultControls();
        if (writeDefaultsIfMissing && saveControls(d)) log("controls: wrote defaults to " + controlsPath().string());
        return d;
    }
    std::stringstream ss;
    ss << in.rdbuf();
    std::string warnings;
    Controls c = parseControls(ss.str(), &warnings);
    if (!warnings.empty()) log("controls: " + warnings);
    return c;
}

namespace {
// PORT: Baroque-Native links this file; input.cpp asks for the bindings on first use (no file I/O at static init).
struct Registrar {
    Registrar() { setControlsLoader([] { setControls(loadControls(true)); }); }
} gRegistrar;
}
}
