// PORT (not in the original): hold-to-skip for EVT events and its bottom-right prompt. See event_skip_prompt.hpp.
// Everything in this file is port-only; the skip itself is the original FUN_0016d1a0 / FUN_0016d260 path.
#include "game/event/event_skip_prompt.hpp"

#include "game/event/event_manager.hpp"
#include "game/hud/pause.hpp"
#include "game/pad.hpp"
#include "game/render2d.hpp"
#include "game/ui/sprite.hpp"
#include "game/ui/text.hpp"
#include "game/ui/texture_registry.hpp"
#include "raylib.h"

#include <algorithm>
#include <cstdint>
#include <cstring>

namespace baroque::game {

namespace {

// PORT: tuning (ticks are original 30 Hz ticks)
constexpr int kHoldTicks = 30;          // hold time to skip (1 s)
constexpr int kDrainPerTick = 3;        // progress lost per tick after an early release
constexpr int kLingerTicks = 45;        // the prompt stays this long after the release
constexpr float kFadeInStep = 1.0f / 5.0f;
constexpr float kFadeOutStep = 1.0f / 10.0f;
constexpr int kMinTicks = 0x3b;         // FUN_0016d1a0: 0x3b < +0x20d70 (60-tick minimum)

// PORT: layout in the 512x448 UI raster, right-anchored (moves to the right screen edge in widescreen). The bottom
// letterbox (op 198) covers y 377..448; the subtitles end around y 430, the prompt sits below them.
constexpr const char* kPromptText = "Hold START to skip";
constexpr float kScale = 0.625f;        // of the 16x18 glyph cell (help bar text uses 1.0)
constexpr float kRight = 496.0f;        // right edge (16 px margin, as the help bar's 32 px frame / 2)
constexpr float kTop = 429.0f;
constexpr float kZ = 5.0f;              // above the letterbox (3) and the event text (3.5), below the fade quad (30)
constexpr float kGaugeGap = 1.5f;
constexpr float kGaugeH = 2.0f;
constexpr int kDim = 0x50;              // glyph grey before the fill passes (PS2 0x80 = 1.0)
constexpr int kDimKey = 0x80;           // the button name before the fill passes
constexpr int kLit = 0xbb;              // DAT_0044c190, the help bar text colour

struct HoldState {
    bool eligible = false;              // fed by the FUN_0013af80 host this tick
    bool prevHeld = false;
    bool armed = false;                 // a press that started while the event ran is still held
    bool done = false;                  // the forced skip was requested (needs a new press for the next event)
    int progress = 0;                   // 0..kHoldTicks
    int linger = 0;
    float alpha = 0.0f;
    ui::Sprite* track = nullptr;        // gauge sprites (untextured, persistent while shown)
    ui::Sprite* fill = nullptr;
};
HoldState gHold;

bool skipInputHeld() {
    return (pad().port[0].held & 0x800u) != 0             // START (Enter / Esc / gamepad Start through controls.ini)
           || IsMouseButtonDown(MOUSE_BUTTON_LEFT);       // click-and-hold
}

void releaseGauge() {
    ui::sprites().destroyRef(gHold.track);
    ui::sprites().destroyRef(gHold.fill);
}

uint32_t grey(int v, float alpha, float alphaScale) {
    const uint32_t c = static_cast<uint32_t>(std::clamp(v, 0, 0xff));
    const uint32_t a = static_cast<uint32_t>(std::clamp(static_cast<int>(alpha * alphaScale), 0, 0xff));
    return (a << 24) | (c << 16) | (c << 8) | c;
}

// The prompt: the game's font through the text object (glyph sprites are released by the host before every tick, so
// this runs every tick), each glyph lit as the fill passes it, and a thin gauge under the text.
void drawPrompt(float alpha, float fill) {
    if (alpha <= 0.0f) {
        releaseGauge();
        return;
    }
    ui::TextRenderer& t = ui::text();
    const int sMode = t.mode, sLayer = t.layerFlag, sProp = t.proportional;
    const float sx = t.x, sy = t.y, sz = t.z, shw = t.halfW, shh = t.halfH, sSpacing = t.spacing;
    const uint32_t sColor = t.color;

    ui::AnchorScope anchor(kAnchorRight);
    t.mode = 0;
    t.layerFlag = 1;                    // as the help bar (FUN_0015c710)
    t.proportional = 1;
    t.spacing = 1.0f;
    t.halfW = 8.0f * kScale;
    t.halfH = 9.0f * kScale;
    const float width = t.measure("%s", kPromptText);
    const float x0 = kRight - width;
    const float fillX = fill * width;
    const char* key = std::strstr(kPromptText, "START");
    const std::size_t keyBegin = key != nullptr ? static_cast<std::size_t>(key - kPromptText) : 0;
    const std::size_t keyEnd = key != nullptr ? keyBegin + 5 : 0;
    t.x = x0;
    t.y = kTop;
    t.z = kZ;
    const std::size_t n = std::strlen(kPromptText);
    for (std::size_t i = 0; i < n; ++i) {
        const float a = t.measure("%.*s", static_cast<int>(i), kPromptText);
        const float b = t.measure("%.*s", static_cast<int>(i + 1), kPromptText);
        const float lit = b > a ? std::clamp((fillX - a) / (b - a), 0.0f, 1.0f) : (fillX >= b ? 1.0f : 0.0f);
        const int base = (i >= keyBegin && i < keyEnd) ? kDimKey : kDim;
        t.color = grey(base + static_cast<int>(static_cast<float>(kLit - base) * lit), alpha, 64.0f);   // alpha 0x40 = full
        const float y = t.y;
        t.draw("%c", kPromptText[i]);
        t.y = y;                        // draw keeps the cursor; stay on the line
    }

    const float gy = kTop + t.halfH * 2.0f + kGaugeGap;
    ui::SpriteLayer& sl = ui::sprites();
    // layer flag 1 like the prompt text: the list +4 is drawn after every flag-0 sprite (render2d), so the gauge stays
    // over the letterbox bar; as flag-0 sprites the newer, translucent track would be drawn first and its z would keep
    // the older letterbox from covering the picture behind it. Same z bucket, flag and list: the older track goes
    // under the fill.
    if (gHold.track == nullptr) gHold.track = sl.create(x0, gy, kZ, width, kGaugeH, 0, 0, 0, 0, ui::kTexNone, 1);
    if (gHold.fill == nullptr) gHold.fill = sl.create(x0, gy, kZ + 0.1f, width, kGaugeH, 0, 0, 0, 0, ui::kTexNone, 1);
    if (gHold.track != nullptr) {
        gHold.track->cx = x0 + width * 0.5f;
        gHold.track->cy = gy + kGaugeH * 0.5f;
        gHold.track->hw = width * 0.5f;
        gHold.track->hh = kGaugeH * 0.5f;
        gHold.track->color = grey(0x20, alpha * 0.75f, 128.0f);   // untextured: alpha 0x80 = opaque
    }
    if (gHold.fill != nullptr) {
        const float w = std::max(fillX, 0.0f);
        gHold.fill->cx = x0 + w * 0.5f;
        gHold.fill->cy = gy + kGaugeH * 0.5f;
        gHold.fill->hw = w * 0.5f;
        gHold.fill->hh = kGaugeH * 0.5f;
        gHold.fill->color = grey(0x58, w > 0.0f ? alpha : 0.0f, 128.0f);
    }

    t.mode = sMode;
    t.layerFlag = sLayer;
    t.proportional = sProp;
    t.x = sx;
    t.y = sy;
    t.z = sz;
    t.halfW = shw;
    t.halfH = shh;
    t.spacing = sSpacing;
    t.color = sColor;
}

// installed into the FUN_0013af80 host at static initialisation (null in builds without this file)
const bool kHookInstalled = [] {
    pauseEventHoldHook = &eventSkipHoldEligible;
    return true;
}();

}  // namespace

void eventSkipHoldEligible() { gHold.eligible = true; }

float eventSkipHoldProgress() { return static_cast<float>(gHold.progress) / static_cast<float>(kHoldTicks); }
float eventSkipPromptAlpha() { return gHold.alpha; }

void eventSkipPromptTick(EventManager* mgr, bool paused) {
    (void)kHookInstalled;
    const bool held = skipInputHeld();
    const bool rising = held && !gHold.prevHeld;
    gHold.prevHeld = held;
    const bool eligible = gHold.eligible;
    gHold.eligible = false;

    if (mgr == nullptr || mgr->active == 0) {
        // no event: forget the hold (a button still held from before the next event is not a new press)
        gHold.armed = false;
        gHold.done = false;
        gHold.progress = 0;
        gHold.linger = 0;
        gHold.alpha = 0.0f;
        releaseGauge();
        return;
    }
    if (!paused) {
        if (mgr->skipCountdown != -1) {
            // a skip is fading out (this hold, or the original START press on a seen event)
            gHold.armed = false;
            gHold.linger = 0;
            if (!gHold.done) gHold.progress = 0;
            gHold.alpha = std::max(0.0f, gHold.alpha - kFadeInStep);
        } else {
            gHold.done = false;
            if (!held) gHold.armed = false;
            else if (rising) gHold.armed = true;
            const bool skippable = mgr->skippable != 0;   // op 193: the script's own no-skip section is respected
            if (gHold.armed && skippable) {
                gHold.linger = kLingerTicks;
                if (eligible && kMinTicks < static_cast<int>(mgr->ticks)) {
                    gHold.progress = std::min(gHold.progress + 1, kHoldTicks);
                    if (gHold.progress >= kHoldTicks) {
                        mgr->skip(true);                  // FUN_0016d1a0(0x51b620, 1)
                        gHold.done = mgr->skipCountdown != -1;
                        gHold.armed = false;
                    }
                }
            } else {
                gHold.progress = std::max(0, gHold.progress - kDrainPerTick);
                if (gHold.linger > 0) --gHold.linger;
                if (!skippable) gHold.linger = 0;
            }
            const bool show = gHold.linger > 0 || gHold.progress > 0;
            if (show) gHold.alpha = std::min(1.0f, gHold.alpha + kFadeInStep);
            else gHold.alpha = std::max(0.0f, gHold.alpha - kFadeOutStep);
        }
    }
    drawPrompt(gHold.alpha, static_cast<float>(gHold.progress) / static_cast<float>(kHoldTicks));
}

}  // namespace baroque::game
