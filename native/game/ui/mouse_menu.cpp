// PORT: mouse input for the menus (see mouse_menu.hpp). Not part of the original game.
#include "game/ui/mouse_menu.hpp"
#include "game/render2d.hpp"
#include "game/hud/hud_api.hpp"
#include "game/ui/menu.hpp"
#include "game/ui/message_box.hpp"
#include "game/ui/text.hpp"
#include <cmath>
namespace baroque::game::ui::mouse {
namespace {
struct Item {
    const void* owner;
    int index;
    float x0, y0, x1, y1;
    uint32_t click;
    int layer;
};
struct Pending {          // a synthesised pad press waiting for its pad sample
    uint32_t mask;
    const void* owner;    // the clicked item (its hover is latched together with the press), nullptr otherwise
    int index;
};
constexpr int kMaxItems = 256;
constexpr int kMaxPending = 8;

bool gEnabled = true;
// host input accumulated since the last pad sample
bool gInside = false;
float gX = 0.0f, gY = 0.0f;
bool gHaveLast = false;
bool gMoved = false;
bool gLeft = false, gRight = false;
float gWheel = 0.0f;
// registrations of the current tick
Item gItems[kMaxItems];
int gItemCount = 0;
int gTopLayer = -1;
bool gWheelH = false;
bool gWheelOff = false;   // PORT: this tick's page ignores the wheel
// synthesised presses: one per pad sample, followed by one empty sample so that the next identical press is a new
// "pressed" edge (PadMgr: pressed = held & ~prev) and a fresh auto-repeat (FUN_0020b910: changed held -> repeat)
Pending gQueue[kMaxPending];
int gQueueCount = 0;
bool gGap = false;
bool gWheelPress = false;   // PORT: the press emitted at the last sample came from the mouse wheel
// latched at the last pad sample, read by the pages during the following tick
const void* gHitOwner = nullptr;
int gHitIndex = -1;
bool gHitMoved = false;

void push(uint32_t mask, const void* owner, int index) {
    if (gQueueCount >= kMaxPending) return;
    gQueue[gQueueCount++] = {mask, owner, index};
}
void clearTick() {
    gItemCount = 0;
    gTopLayer = -1;
    gWheelH = false;
    gWheelOff = false;
}
}  // namespace

bool uiFromWindow(float px, float py, int windowW, int windowH, float* uiX, float* uiY) {
    // Renderer2D::drawSprites: the 512x448 raster on the 4:3 box of the presented layout (the widescreen extended
    // raster keeps that mapping; menus stay in the box). A window pixel covers [px, px+1); its centre maps to the
    // raster through the same affine transform.
    const ScreenLayout L = screenLayout(windowW, windowH);
    const Viewport43 vp{L.boxX, L.boxY, L.boxW, L.boxH};
    if (vp.w <= 0 || vp.h <= 0) return false;
    const float x = (px + 0.5f - static_cast<float>(vp.x)) * 512.0f / static_cast<float>(vp.w) - 0.5f;
    const float y = (py + 0.5f - static_cast<float>(vp.y)) * 448.0f / static_cast<float>(vp.h) - 0.5f;
    *uiX = x;
    *uiY = y;
    return x >= -0.5f && x < 511.5f && y >= -0.5f && y < 447.5f;
}

// PORT: left click latched per host frame for the movie skip (movies poll once per tick, host frames can be faster)
static bool gMovieClick = false;
bool consumeMovieClick() { const bool c = gMovieClick; gMovieClick = false; return c; }
void clearMovieClick() { gMovieClick = false; }

void hostFrame(bool inside, float uiX, float uiY, bool moved, bool leftPressed, bool rightPressed, float wheel) {
    if (leftPressed) gMovieClick = true;
    if (gHaveLast && moved) gMoved = true;
    gHaveLast = true;
    gInside = inside;
    gX = uiX;
    gY = uiY;
    gLeft = gLeft || leftPressed;
    gRight = gRight || rightPressed;
    gWheel += wheel;
}

void setEnabled(bool on) {
    if (gEnabled == on) return;
    gEnabled = on;
    // whatever happened while the mouse belonged to the camera is not a menu action
    gMoved = false;
    gLeft = gRight = false;
    gWheel = 0.0f;
    gQueueCount = 0;
    gGap = false;
}
bool enabled() { return gEnabled; }
bool wantsCursor() { return menu().isOpen() || messageBox().isOpen() || hudItemScreenOpen(); }   // + item screen
bool cursor(float* uiX, float* uiY) {
    if (!gHaveLast || !gInside) return false;
    *uiX = gX;
    *uiY = gY;
    return true;
}

// PORT: Esc follows PC conventions: "back" (Circle) while a menu page, message window or the item screen is open,
// START (pause / skip) otherwise. Esc is therefore not bound to START in controls.ini by default.
static uint32_t escBits() {
    if (!IsKeyDown(KEY_ESCAPE)) return 0;
    return wantsCursor() ? uint32_t(platform::PAD_CIRCLE) : uint32_t(platform::PAD_START);
}

uint32_t padPoll() {
    const uint32_t esc = escBits();
    if (esc != 0) return esc | padPollMouse();
    return padPollMouse();
}

uint32_t padPollMouse() {
    if (!gEnabled) {
        clearTick();
        gHitOwner = nullptr;
        gHitIndex = -1;
        gHitMoved = false;
        return 0;
    }
    // hit test against this tick's registrations (topmost layer only, last registered wins)
    const Item* hit = nullptr;
    if (gInside) {
        for (int i = gItemCount - 1; i >= 0; --i) {
            const Item& it = gItems[i];
            if (it.layer != gTopLayer) continue;
            if (gX >= it.x0 && gX < it.x1 && gY >= it.y0 && gY < it.y1) {
                hit = &it;
                break;
            }
        }
    }
    gHitOwner = hit ? hit->owner : nullptr;
    gHitIndex = hit ? hit->index : -1;
    gHitMoved = gMoved;
    // new events -> synthesised presses
    if (gLeft && hit != nullptr) push(hit->click, hit->owner, hit->index);
    if (gRight) push(kCancel, nullptr, -1);
    if (gWheelOff && !gWheelH) gWheel = 0.0f;
    const float steps = std::trunc(gWheel);
    gWheel -= steps;
    const int n = static_cast<int>(steps);
    for (int i = 0; i < (n < 0 ? -n : n); ++i) {
        uint32_t m;
        if (gWheelH) m = (n > 0) ? platform::PAD_RIGHT : platform::PAD_LEFT;
        else m = (n > 0) ? platform::PAD_UP : platform::PAD_DOWN;
        // PORT: no wheel "inertia" -- at most 2 wheel steps wait in the queue; fast / smooth wheels drop the rest
        int queuedWheel = 0;
        for (int q = 0; q < gQueueCount; ++q) if (gQueue[q].owner == nullptr && gQueue[q].index == -2) ++queuedWheel;
        if (queuedWheel >= 2) break;
        push(m, nullptr, -2);   // index -2 marks a wheel step
    }
    gMoved = false;
    gLeft = gRight = false;
    clearTick();
    // emit at most one press per sample
    uint32_t bits = 0;
    gWheelPress = false;
    if (gGap) {
        gGap = false;
    } else if (gQueueCount > 0) {
        const Pending p = gQueue[0];
        for (int i = 1; i < gQueueCount; ++i) gQueue[i - 1] = gQueue[i];
        --gQueueCount;
        bits = p.mask;
        gGap = true;
        gWheelPress = p.index == -2;
        if (p.owner != nullptr) {   // the page sees the click's item as hovered in the tick that sees the press
            gHitOwner = p.owner;
            gHitIndex = p.index;
            gHitMoved = true;
        }
    }
    return bits;
}

void item(const void* owner, int index, float x, float y, float w, float h, uint32_t click, int layer) {
    if (layer > gTopLayer) gTopLayer = layer;
    if (gItemCount >= kMaxItems) return;
    gItems[gItemCount++] = {owner, index, x, y, x + w, y + h, click, layer};
}
void row(const void* owner, int index, float x0, float x1, float textY, float pitch, uint32_t click, int layer) {
    item(owner, index, x0, textY + 9.0f - pitch * 0.5f, x1 - x0, pitch, click, layer);
}
void block(float x, float y, float w, float h, int layer) {
    (void)x; (void)y; (void)w; (void)h;   // the region only raises the active layer
    if (layer > gTopLayer) gTopLayer = layer;
}
void wheelHorizontal() { gWheelH = true; }
void wheelOff() { gWheelOff = true; }
bool lastPressFromWheel() { return gWheelPress; }
int hovered(const void* owner) {
    if (owner == nullptr || !gHitMoved || gHitOwner != owner) return -1;
    return gHitIndex;
}
float textWidth(const char* s) {
    TextRenderer& t = text();
    const float hw = t.halfW, hh = t.halfH, sp = t.spacing;
    const int prop = t.proportional;
    t.halfW = 8.0f;
    t.halfH = 9.0f;
    t.spacing = 1.0f;
    t.proportional = 1;
    const float w = t.measure("%s", s);
    t.halfW = hw;
    t.halfH = hh;
    t.spacing = sp;
    t.proportional = prop;
    return w;
}

}  // namespace baroque::game::ui::mouse
