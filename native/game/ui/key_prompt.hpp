#pragma once
// PORT: keyboard button prompts. Not part of the original game.
//
// While keyboard / mouse is the last-used input device (platform::lastInputGamepad() == false, the same detector the
// free mouse camera uses), every PS2 button glyph the game draws (FUN_00166eb0 glyph cells of TEX_UI05 / TEX_UI04,
// table 0x491ef0) is presented as a keycap with the PC key bound to that PS2 button in controls.ini
// (game::Renderer2D::drawSprites asks game::keycapLabel). With the gamepad active nothing changes: the original glyph
// is drawn. This module installs the label provider at static init (Baroque-Native only; targets without it keep the
// original glyphs).
#include <string>
namespace baroque::game::ui {
// Short keycap label of a raylib KeyboardKey ("E", "Space", "Shift", "Enter", "F1", ...); "" for 0.
std::string keycapKeyName(int key);
// Short keycap label of a platform::MouseBind code ("LMB", "RMB", "MMB", "Wheel+", ...); "" for MB_NONE.
std::string keycapMouseName(int mouseBind);
// Label for PS2 pad bit `padBit` (SCE_PAD layout, platform::PadBits 0x1..0x8000) or nullptr when the original glyph
// should be shown (gamepad active, or nothing bound). The pointer stays valid until the next call for the same bit.
const char* keyPromptLabel(int padBit);
}
