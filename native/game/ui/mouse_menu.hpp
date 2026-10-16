#pragma once
// PORT: mouse input for the menus. The original is pad-only; nothing here is a translation.
//
// How it works (work/reports/MOUSE_MENUS_NATIVE.md):
//  - The host (native/main.cpp) feeds the cursor once per presented frame, already converted from window pixels to
//    the 512x448 UI raster of the 4:3 pillarboxed presentation (uiFromWindow), plus button/wheel edges.
//  - During a tick, every page that is drawn registers its selectable items as rectangles in UI raster coordinates
//    (item / row), tagged with an owner pointer and the page's own cursor index.
//  - At the end of the tick the engine reads the pad (FUN_0020b860 -> platform::pollInput). pollInput calls padPoll()
//    through platform::setPollHook: it hit-tests the cursor against the rectangles of this tick, latches the result
//    and returns synthesised pad bits (left click on an item = that item's button, normally Cross; right click =
//    Circle; one wheel notch = one D-pad press) that are ORed into the same pad sample the keyboard produces.
//  - In the next tick a page's mouseInput() (called by MenuObject::update right before input()) asks hovered(this):
//    the latched item index when the mouse moved (or clicked) since the previous sample, -1 otherwise. A still mouse
//    therefore never moves the cursor, and the keyboard/pad path is untouched when the mouse is not used.
#include "platform/input.hpp"
#include <cstdint>
namespace baroque::game::ui::mouse {

// Pad buttons used for clicks (SCE_PAD layout, platform::PadBits). In the USA build Cross confirms and Circle
// cancels (every page input handler: pressed & 0x40 -> decide, pressed & 0x20 -> back).
constexpr uint32_t kConfirm = platform::PAD_CROSS;
constexpr uint32_t kCancel = platform::PAD_CIRCLE;
// Layers: only items of the highest layer registered in a tick can be hit (an open message window is modal).
constexpr int kLayerPage = 0;
constexpr int kLayerDialog = 1;

// ---- host side ---------------------------------------------------------------------------------------------------
// Window pixel (framebuffer pixel, top-left origin) -> UI raster coordinate of the viewport43() presentation.
// Returns false when the point lies in the pillarbox/letterbox bars.
bool uiFromWindow(float px, float py, int windowW, int windowH, float* uiX, float* uiY);
// One presented frame: cursor position in UI raster coordinates (inside = within the 4:3 area), whether the
// physical cursor moved (window pixels, so a window resize alone is not a move), button edges (pressed this frame)
// and wheel movement in notches (positive = away from the user).
void hostFrame(bool inside, float uiX, float uiY, bool moved, bool leftPressed, bool rightPressed, float wheel);
// false while the Game scene captures the mouse for camera look: no hit tests, no synthesised buttons.
void setEnabled(bool on);
bool enabled();
// A menu page or a message window is open (the host releases a captured cursor while this is true).
bool wantsCursor();
bool consumeMovieClick();
bool lastPressFromWheel();  // PORT: the current pad press was synthesised from a wheel notch (pages may clamp instead of wrapping)   // PORT: a left click since the last call (movie skip)
void clearMovieClick();
// Last cursor position (for the --shot debug marker); false if it is outside the 4:3 area or unknown.
bool cursor(float* uiX, float* uiY);
// Installed with platform::setPollHook: hit test + latch + pad bits for this pad sample.
uint32_t padPoll();
uint32_t padPollMouse();   // the mouse part of padPoll (padPoll adds the PORT Esc mapping)

// ---- page side ---------------------------------------------------------------------------------------------------
// A selectable item: rectangle (x, y, w, h) in UI raster coordinates, `index` in the owner's own cursor space,
// `click` = pad button a left click on it produces.
void item(const void* owner, int index, float x, float y, float w, float h, uint32_t click = kConfirm, int layer = kLayerPage);
// A row of a text list whose 16x18 glyphs are drawn at text position (x0, textY): the row spans x0..x1 and is
// `pitch` tall, centred on the glyphs.
void row(const void* owner, int index, float x0, float x1, float textY, float pitch, uint32_t click = kConfirm,
         int layer = kLayerPage);
// A region that only blocks lower layers (a window without selectable entries).
void block(float x, float y, float w, float h, int layer);
// For this tick, the wheel produces Left/Right (value sliders) instead of Up/Down.
void wheelHorizontal();
// PORT: for this tick, wheel notches are dropped (unless wheelHorizontal() is also set).
void wheelOff();
// Index of the owner's item under the mouse at the last pad sample if the mouse moved or clicked since the sample
// before; -1 otherwise (or when the item belongs to another owner).
int hovered(const void* owner);
// Width of a string with the text renderer's current proportional metrics at 16x18 glyphs (state is preserved).
float textWidth(const char* s);

}  // namespace baroque::game::ui::mouse
