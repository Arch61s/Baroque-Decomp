#include "game/ui/key_prompt.hpp"
#include "game/render2d.hpp"
#include "platform/controls.hpp"
#include "platform/input.hpp"
#include "raylib.h"
// PORT: the whole file is port-only (keyboard button prompts, see key_prompt.hpp).
namespace baroque::game::ui {
namespace {
struct KeyLabel { int key; const char* label; };
// Only characters the game font has (TextRenderer ASCII map: A-Z a-z 0-9 and "+-*/=.,:';\?!#$%&"()<>@|").
const KeyLabel kKeyLabels[] = {
    {KEY_SPACE, "Space"}, {KEY_ENTER, "Enter"}, {KEY_KP_ENTER, "Enter"}, {KEY_ESCAPE, "Esc"}, {KEY_TAB, "Tab"},
    {KEY_BACKSPACE, "BkSp"}, {KEY_INSERT, "Ins"}, {KEY_DELETE, "Del"}, {KEY_HOME, "Home"}, {KEY_END, "End"},
    {KEY_PAGE_UP, "PgUp"}, {KEY_PAGE_DOWN, "PgDn"}, {KEY_UP, "Up"}, {KEY_DOWN, "Down"}, {KEY_LEFT, "Left"},
    {KEY_RIGHT, "Right"}, {KEY_LEFT_SHIFT, "Shift"}, {KEY_RIGHT_SHIFT, "Shift"}, {KEY_LEFT_CONTROL, "Ctrl"},
    {KEY_RIGHT_CONTROL, "Ctrl"}, {KEY_LEFT_ALT, "Alt"}, {KEY_RIGHT_ALT, "Alt"}, {KEY_LEFT_SUPER, "Win"},
    {KEY_RIGHT_SUPER, "Win"}, {KEY_KB_MENU, "Menu"}, {KEY_CAPS_LOCK, "Caps"}, {KEY_SCROLL_LOCK, "ScrLk"},
    {KEY_NUM_LOCK, "NumLk"}, {KEY_PRINT_SCREEN, "PrtSc"}, {KEY_PAUSE, "Pause"}, {KEY_APOSTROPHE, "'"},
    {KEY_COMMA, ","}, {KEY_MINUS, "-"}, {KEY_PERIOD, "."}, {KEY_SLASH, "/"}, {KEY_SEMICOLON, ";"}, {KEY_EQUAL, "="},
    {KEY_BACKSLASH, "\\"}, {KEY_LEFT_BRACKET, "LBr"}, {KEY_RIGHT_BRACKET, "RBr"}, {KEY_GRAVE, "Grave"},
    {KEY_KP_DECIMAL, "Num."}, {KEY_KP_DIVIDE, "Num/"}, {KEY_KP_MULTIPLY, "Num*"}, {KEY_KP_SUBTRACT, "Num-"},
    {KEY_KP_ADD, "Num+"}, {KEY_KP_EQUAL, "Num="},
};
std::string gLabels[16];
}

std::string keycapKeyName(int key) {
    if (key == 0) return "";
    for (const KeyLabel& k : kKeyLabels) if (k.key == key) return k.label;
    if ((key >= KEY_A && key <= KEY_Z) || (key >= KEY_ZERO && key <= KEY_NINE)) return std::string(1, static_cast<char>(key));
    if (key >= KEY_F1 && key <= KEY_F12) return "F" + std::to_string(key - KEY_F1 + 1);
    if (key >= KEY_KP_0 && key <= KEY_KP_9) return "Num" + std::to_string(key - KEY_KP_0);
    return "?";
}

std::string keycapMouseName(int m) {
    switch (m) {
        case platform::MB_LEFT: return "LMB";
        case platform::MB_RIGHT: return "RMB";
        case platform::MB_MIDDLE: return "MMB";
        case platform::MB_SIDE: return "M4";
        case platform::MB_EXTRA: return "M5";
        case platform::MB_FORWARD: return "M6";
        case platform::MB_BACK: return "M7";
        case platform::MB_WHEEL_UP: return "Wheel+";
        case platform::MB_WHEEL_DOWN: return "Wheel-";
        default: return "";
    }
}

const char* keyPromptLabel(int padBit) {
    if (platform::lastInputGamepad()) return nullptr;   // gamepad active: the original PS2 glyph
    int input = -1;
    for (int i = 0; i < platform::kButtonInputs; ++i) if (padBit == (1 << i)) input = i;
    if (input < 0) return nullptr;
    const platform::InputBinding& b = platform::controls().bind[input];
    std::string label;
    for (int k : b.keys) if (k != 0) { label = keycapKeyName(k); break; }   // the first bound key (controls.ini order)
    if (label.empty())
        for (int m : b.mouse) if (m != 0) { label = keycapMouseName(m); break; }   // else the first mouse input
    if (label.empty()) return nullptr;   // nothing on keyboard / mouse: keep the PS2 glyph
    gLabels[input] = label;
    return gLabels[input].c_str();
}

namespace {
struct Install { Install() { game::setKeycapLabelProvider(&keyPromptLabel); } } gInstall;
}
}
