// PORT-ONLY: PC SETTINGS page (menu mode 11) and its KEY BINDINGS list. Not in the original game
// (work/reports/PC_SETTINGS.md). Reached from GAME CONFIG item 10 (in game: item screen -> SELECT) and from the title's
// OPTION page (row 7). Built from the GAME CONFIG page's own pieces so it reads as part of the original menus:
// page frame / title letters / frame2 + help bar (FUN_001642d0, FUN_00164a30, FUN_001664b0), the text object at 16x18
// (labels x 32, values x 280, pitch 31, grey 0xbbbbbb / red 0x22227f cursor / dim 0x3f3f3f values, the 0x0c blink),
// the volume bar + knob (kConfigBarPos, tex 0x2e / 0x30) for the mouse speed, the grey arrow (tex 0x30) for the
// sub-page row, the red arrows of TEX_UI05 (tex 0x28, SOUND TEST / ITEM LIST) for the selector and the scroll marks,
// the "Default Settings" Yes/No window 0x40, and the GAME CONFIG sounds (SE 6 move / change, SE 1 decide, SE 2 cancel,
// SE 8 page change). Editing works like GAME CONFIG (FUN_00160680 / FUN_00160930): Cross selects a value, Left/Right
// change it (applied live), Cross keeps it (saved), Circle restores it.
// Settings go to <exe>/baroque_port.ini (game/present/present_config.hpp) and <exe>/controls.ini
// (platform/controls.hpp, game/aim_assist.hpp); nothing is written into the game's own options or save data.
#include "game/ui/menu.hpp"
#include "audio/sfs.hpp"
#include "audio/sound_system.hpp"
#include "game/aim_assist.hpp"
#include "game/fade.hpp"
#include "game/hud/hud_api.hpp"
#include "game/options.hpp"
#include "game/pad.hpp"
#include "game/present/present_config.hpp"
#include "game/scene_manager.hpp"
#include "game/sound_stub.hpp"
#include "game/ui/gradient_texture.hpp"
#include "game/ui/help_bar.hpp"
#include "game/ui/message_box.hpp"
#include "game/ui/mouse_menu.hpp"
#include "game/ui/sprite.hpp"
#include "game/ui/strings.hpp"
#include "game/ui/text.hpp"
#include "game/ui/texture_registry.hpp"
#include "game/ui/ui_tables.hpp"
#include "platform/controls.hpp"
#include "raylib.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
namespace baroque::game::ui {
namespace {
constexpr uint32_t kDim = 0x3f3f3f;           // GAME CONFIG inactive values
constexpr uint32_t kText = 0xbbbbbb;          // DAT_0044c190
constexpr uint32_t kTextDefault = 0x40bbbbbb; // DAT_0044c198
constexpr uint32_t kRed = 0x22227f;           // cursor / current value
constexpr float kLabelX = 32.0f, kValueX = 280.0f, kTop = 79.0f, kPitch = 31.0f;
constexpr int kVisible = 10;                  // rows y 79 .. 358 (GAME CONFIG's ten rows)
constexpr int kTexArrows = 0x28;              // TEX_UI05: red down / up / left / right arrows at v 17..30
constexpr float kUvDown[4] = {0.0f, 17.0f, 13.0f, 30.0f};
constexpr float kUvUp[4] = {13.0f, 17.0f, 25.0f, 30.0f};
constexpr float kUvLeft[4] = {25.0f, 17.0f, 35.0f, 30.0f};
constexpr float kUvRight[4] = {35.0f, 17.0f, 45.0f, 30.0f};
constexpr float kScrollX = 486.0f;            // scroll marks right of the values
const char* const kSlash = "\x81^";           // 0x44c110 full-width slash (GAME CONFIG value pairs)

uint32_t col(uint32_t rgb, unsigned alpha) { return rgb + (alpha << 24); }

int gFrom = 0;                    // 0: opened from OPTION, 1: from GAME CONFIG (Circle returns there)
signed char gSettingsCursor = 1;  // persists across the pages like gConfigCursor
signed char gBindCursor = 0;

// ---- shared base: frame, title, scroll list, game-scene handling ---------------------------------------------------
class PcPageBase : public MenuPage {
protected:
    void openBase(const char* title) {
        makeFrame();
        makeTitle(title);
        if (scenes().isCurrent("Game....")) {   // as KEY ASSIGN FUN_00160fe0
            makeGameBackdrop();
            if (!hudMapShown()) mapHidden_ = 0;
            else { hudMapHide(); mapHidden_ = 1; }
            if (itemScreen().isOpen()) { helpBar().setEnabled(1, 0); helpBar().setEnabled(2, 0); }
        }
        isOpen = 1;
        alpha_ = 0;
        angle_ = 0;
        wantsNext_ = 0;
        if (!textures().isLoaded(kTexArrows)) textures().load(kTexArrows);   // as buttonIcon FUN_00166eb0 (kept loaded)
        scrollUp_ = sprites().create(kScrollX, kTop + 2.0f, 1.0f, 12.0f, 13.0f, kUvUp[0], kUvUp[1], kUvUp[2], kUvUp[3], kTexArrows, 1);
        scrollDown_ = sprites().create(kScrollX, kTop + kPitch * (kVisible - 1) + 3.0f, 1.0f, 13.0f, 13.0f, kUvDown[0], kUvDown[1],
                                       kUvDown[2], kUvDown[3], kTexArrows, 1);
        makeFrame2(0);
    }
    void closeBase() {
        freeFrame2();
        helpBar().clear(0);
        freeFrame();
        isOpen = 0;
        sprites().destroyRef(scrollUp_);
        sprites().destroyRef(scrollDown_);
        if (scenes().isCurrent("Game....") && mapHidden_ != 0) hudMapShow();
    }
    // the alpha ramp + blink of FUN_0015e9d0
    void animate() {
        angle_ += 0xc;
        if (angle_ > 0x168) angle_ = -360;
        float b = (std::sin(static_cast<float>(angle_) * 0.017453292f) + 1.0f) * 32.0f + 32.0f;
        blink_ = static_cast<unsigned>(static_cast<int>(b));
        if (alpha_ < 0x40 && fade().finished()) { alpha_ += 6; if (alpha_ > 0x40) alpha_ = 0x40; }
        else if (!(alpha_ < 0x40)) alpha_ = 0x40;
    }
    bool ready() const { return isOpen != 0 && fade().finished() && alpha_ >= 0x40; }
    float lineY(int line) const { return kTop + kPitch * static_cast<float>(line - top_); }
    bool visible(int line) const { return line >= top_ && line < top_ + kVisible; }
    void scrollTo(int line, int count, bool headerAbove) {
        int want = line - (headerAbove ? 1 : 0);
        if (want < top_) top_ = want;
        if (line >= top_ + kVisible) top_ = line - kVisible + 1;
        if (top_ > count - kVisible) top_ = count - kVisible;
        if (top_ < 0) top_ = 0;
    }
    void drawScrollMarks(int count) {
        const uint32_t c = col(0x7f7f7f, alpha_);
        if (scrollUp_) scrollUp_->color = top_ > 0 ? c : 0u;
        if (scrollDown_) scrollDown_->color = top_ + kVisible < count ? c : 0u;
    }
    void scrollMarkItems(int count) {
        if (top_ > 0) mouse::item(this, 900, kScrollX - 6.0f, kTop - 4.0f, 24.0f, 24.0f, platform::PAD_UP);
        if (top_ + kVisible < count) mouse::item(this, 901, kScrollX - 6.0f, kTop + kPitch * (kVisible - 1) - 3.0f, 24.0f, 24.0f, platform::PAD_DOWN);
    }
    // PORT: no wheel scrolling here; instead resting the mouse on the last (first) visible row scrolls the list down
    // (up) one line after a short dwell and keeps going while it stays there. Returns the new edge line the cursor
    // should follow on the tick a step happens, -1 otherwise.
    int edgeHoverScroll(int count) {
        float x = 0.0f, y = 0.0f;
        int dir = 0;
        if (mouse::enabled() && mouse::cursor(&x, &y) && x >= kLabelX - 8.0f && x < 476.0f) {
            const float lastY = lineY(top_ + kVisible - 1) + 9.0f, firstY = lineY(top_) + 9.0f;   // row centres
            if (top_ + kVisible < count && std::fabs(y - lastY) < kPitch * 0.5f) dir = 1;
            else if (top_ > 0 && std::fabs(y - firstY) < kPitch * 0.5f) dir = -1;
        }
        if (dir == 0 || dir != edgeDir_) {
            edgeDir_ = dir;
            edgeTicks_ = 0;
            return -1;
        }
        if (++edgeTicks_ < kEdgeDelay) return -1;
        edgeTicks_ = kEdgeDelay - kEdgeRepeat;
        top_ = std::clamp(top_ + dir, 0, std::max(0, count - kVisible));
        return dir > 0 ? top_ + kVisible - 1 : top_;
    }
    void setHelpText(const char* s) {
        helpBar().set(32.0f, 403.0f, 448.0f, 40.0f, 0, "%s", s);
        helpBar().setSpeed(0, 4);
    }
    static void textBegin() {
        TextRenderer& t = text();
        t.layerFlag = 1;
        t.halfW = 8.0f;
        t.halfH = 9.0f;
        t.z = 0.9f;
    }
    static void textEnd() {
        TextRenderer& t = text();
        t.layerFlag = 1;
        t.color = kTextDefault;
        t.halfW = 8.0f;
        t.halfH = 9.0f;
    }
    int wantsNext_ = 0;
    int mapHidden_ = 0;
    int angle_ = 0;
    unsigned alpha_ = 0;
    unsigned blink_ = 0x40;
    int top_ = 0;
    int edgeDir_ = 0;     // PORT: edge-hover scrolling (edgeHoverScroll)
    int edgeTicks_ = 0;
    static constexpr int kEdgeDelay = 20;    // ticks resting on the edge row before the first step
    static constexpr int kEdgeRepeat = 8;    // ticks between further steps
    Sprite* scrollUp_ = nullptr;
    Sprite* scrollDown_ = nullptr;
};

// ---- PC SETTINGS ---------------------------------------------------------------------------------------------------
enum Line {
    kHeadDisplay, kWindowMode, kFrameRate, kInterp, kAspect, kHud, kVoice,
    kHeadControls, kMouseCam, kMouseSpeed, kInvertX, kInvertY, kAimAssist, kAttackDir, kBindings, kDefaults,
    kLineCount
};
enum Kind { kHead, kList, kSelect, kSlider, kPage, kAction };
struct LineDef {
    Kind kind;
    const char* label;
    const char* help;
    int count;
    const char* names[6];
};
// value names: "ON" / "OFF" / "Normal" / "Reverse" are the original strings 0x47 / 0x48 / 0x4b / 0x4c
const LineDef kLines[kLineCount] = {
    {kHead, "DISPLAY", "", 0, {}},
    {kList, "WINDOW MODE", "Play in a window or borderless fullscreen.\nAlt+Enter also switches", 2, {"Window", "Borderless"}},
    {kSelect, "FRAME RATE", "Frames shown per second. The original runs at 30;\nVSync matches your monitor", 6,
     {"VSync", "30", "60", "120", "144", "Unlimited"}},
    {kList, "INTERPOLATION", "Smoother motion above 30 fps by drawing in-between\nframes. OFF is the original", 2, {"ON", "OFF"}},
    {kList, "ASPECT RATIO", "Screen shape. 4:3 is the original; 16:9 and 21:9\nshow more. Auto fills the window", 4,
     {"Auto", "16:9", "21:9", "4:3"}},
    {kList, "HUD LAYOUT", "Edge: HUD at the screen edges.\n4:3: HUD where the original had it", 2, {"Edge", "4:3"}},
    {kList, "VOICES", "Language of the spoken dialogue.\nThe text stays English", 2, {"Japanese", "English"}},
    // PORT: no background options; the game always pauses (and mutes) while another window is active
    {kHead, "CAMERA & CONTROLS", "", 0, {}},
    {kList, "MOUSE CAMERA", "Turn the camera with the mouse. Auto: on with\nkeyboard and mouse, off with a controller", 3,
     {"Auto", "ON", "OFF"}},
    {kSlider, "MOUSE SPEED", "How fast the mouse turns the camera", 11, {}},
    {kList, "LEFT-RIGHT", "Reverse turns the camera the other way when the\nmouse moves left or right", 2, {"Normal", "Reverse"}},
    {kList, "UP-DOWN", "Reverse tilts the camera the other way when the\nmouse moves up or down", 2, {"Normal", "Reverse"}},
    {kList, "AIM ASSIST", "Mouse attacks turn toward nearby enemies.\nOFF: they go exactly where you aim", 3,
     {"OFF", "Subtle", "Strong"}},
    {kList, "ATTACK DIRECTION", "Camera: mouse attacks go where the camera looks.\nCharacter: where the character faces", 2,
     {"Camera", "Character"}},
    {kPage, "KEY BINDINGS", "Change the keyboard keys and mouse buttons", 0, {}},
    {kAction, nullptr, nullptr, 0, {}},   // "Default Settings" (str 0x38 / help 0x42)
};
constexpr float kAspects[4] = {present::kAutoAspect, 16.0f / 9.0f, 21.0f / 9.0f, 4.0f / 3.0f};
constexpr float kSpeedScale[11] = {0.3f, 0.4f, 0.5f, 0.6f, 0.8f, 1.0f, 1.25f, 1.5f, 1.75f, 2.0f, 2.5f};
constexpr float kSensX = 0.15f, kSensY = 0.12f;   // controls.ini defaults (sensitivity_x / sensitivity_y)

const char* lineLabel(int l) { return l == kDefaults ? str(0x38) : kLines[l].label; }
const char* lineHelp(int l) { return l == kDefaults ? str(0x42) : kLines[l].help; }
bool selectable(int l) { return kLines[l].kind != kHead; }
bool isDisplayLine(int l) { return l > kHeadDisplay && l < kHeadControls; }

int nearestIndex(const float* v, int n, float x) {
    int best = 0;
    for (int i = 1; i < n; ++i) if (std::fabs(v[i] - x) < std::fabs(v[best] - x)) best = i;
    return best;
}
int getValue(int l) {
    const present::PortSettings& s = present::portSettings();
    const platform::Controls& c = platform::controls();
    switch (l) {
    case kWindowMode: return s.borderless ? 1 : 0;
    case kFrameRate: return static_cast<int>(s.fps);
    case kInterp: return s.interpolation ? 0 : 1;
    case kAspect: return nearestIndex(kAspects, 4, std::min(s.maxAspect, present::kAutoAspect));
    case kHud: return s.classicHud ? 1 : 0;
    case kVoice: return audio::japaneseVoicesActive() ? 0 : 1;   // the voices playing (a missing language falls back)
    case kMouseCam: return c.freeCamera;
    case kMouseSpeed: return nearestIndex(kSpeedScale, 11, c.mouseSensX / kSensX);
    case kInvertX: return c.mouseInvertX ? 1 : 0;
    case kInvertY: return c.mouseInvertY ? 1 : 0;
    case kAimAssist: return static_cast<int>(aimAssistLevel());
    case kAttackDir: return cameraRelativeAttacks() ? 0 : 1;
    default: return 0;
    }
}
// applied live, not saved
void setValue(int l, int v) {
    present::PortSettings& s = present::portSettings();
    platform::Controls c = platform::controls();
    switch (l) {
    case kWindowMode: s.borderless = v != 0; present::applyPortDisplay(); break;
    case kFrameRate: s.fps = static_cast<present::FpsCap>(v); present::applyPortDisplay(); break;
    case kInterp: s.interpolation = v == 0; break;
    case kAspect: s.maxAspect = kAspects[v]; break;
    case kHud: s.classicHud = v != 0; break;
    case kVoice:
        s.englishVoice = v != 0;
        audio::setEnglishVoiceArchive(s.englishVoice);   // PORT (voice language): applied live
        audio::reloadSoundArchive();
        break;
    case kMouseCam: c.freeCamera = v; platform::setControls(c); break;
    case kMouseSpeed: c.mouseSensX = kSensX * kSpeedScale[v]; c.mouseSensY = kSensY * kSpeedScale[v]; platform::setControls(c); break;
    case kInvertX: c.mouseInvertX = v != 0; platform::setControls(c); break;
    case kInvertY: c.mouseInvertY = v != 0; platform::setControls(c); break;
    case kAimAssist: setAimAssistLevel(static_cast<AimAssistLevel>(v), false); break;
    case kAttackDir: setCameraRelativeAttacks(v == 0, false); break;
    default: break;
    }
}
void saveLine(int l) {
    if (isDisplayLine(l)) present::savePortSettings();
    else platform::saveControls(platform::controls());
}

class PcSettingsPage final : public PcPageBase {
public:
    explicit PcSettingsPage(int from) { gFrom = from; }
    const char* debugName() const override { return "PC SETTINGS"; }
    void open() override {
        openBase("PC SETTINGS");
        selected_ = -1;
        if (!selectable(gSettingsCursor)) gSettingsCursor = 1;
        top_ = 0;
        scrollTo(gSettingsCursor, kLineCount, !selectable(gSettingsCursor - 1) || gSettingsCursor == 1);
        for (int i = 0; i < 9; ++i) {   // the BGM VOLUME bar of GAME CONFIG (FUN_0015dcd0), placed per frame
            const float* p = kConfigBarPos[i];
            const float* uv = kConfigBarUv[i];
            bar_[i] = sprites().create(p[0] - 16.0f, 0.0f, 1.0f, p[2], p[3], uv[0], uv[1], uv[2], uv[3], 0x2e, 1);
        }
        knob_ = sprites().create(kConfigBarPos[0][0] - 16.0f, 0.0f, 2.0f, 6.0f, 15.0f, 115.0f, 0.0f, 121.0f, 15.0f, 0x30, 1);
        pageArrow_ = sprites().create(296.0f, 0.0f, 1.0f, 10.0f, 13.0f, 105.0f, 0.0f, 115.0f, 13.0f, 0x30, 1);
        selLeft_ = sprites().create(kValueX, 0.0f, 1.0f, 10.0f, 13.0f, kUvLeft[0], kUvLeft[1], kUvLeft[2], kUvLeft[3], kTexArrows, 1);
        selRight_ = sprites().create(kValueX, 0.0f, 1.0f, 10.0f, 13.0f, kUvRight[0], kUvRight[1], kUvRight[2], kUvRight[3], kTexArrows, 1);
        for (int h = 0; h < 2; ++h)
            if (gradientTextureId() != -1)
                underline_[h] = sprites().create(0.0f, 0.0f, 0.5f, 220.0f, 2.0f, 0.0f, 0.0f, 32.0f, 1.0f, gradientTextureId(), 1);
    }
    void close() override {
        closeBase();
        for (Sprite*& s : bar_) sprites().destroyRef(s);
        sprites().destroyRef(knob_);
        sprites().destroyRef(pageArrow_);
        sprites().destroyRef(selLeft_);
        sprites().destroyRef(selRight_);
        for (Sprite*& s : underline_) sprites().destroyRef(s);
    }
    void update() override {
        if (isOpen == 0) return;
        animateTitle();
        if (titleSettled != 0 && isOpen != 0) { draw(); defaultsDialog(); }
        else hideSprites();
    }
    void input() override {
        if (!ready()) { pending_ = -1; return; }
        const PadPort& p = pad().port[0];
        if (pending_ >= 1000 && (p.pressed & 0x40) != 0 && !messageBox().isOpen()) clickValue(pending_);
        else if (p.pressed & 0x20) { if (selected_ != -1) cancel(); else leave(); }
        else if (p.pressed & 0x40) confirm();
        else if (p.repeat & (0x4000 | 0x20000)) cursorStep(+1);
        else if (p.repeat & (0x1000 | 0x10000)) cursorStep(-1);
        else if (p.repeat & (0x8000 | 0x40000)) adjust(-1);
        else if (p.repeat & (0x2000 | 0x80000)) adjust(+1);
        pending_ = -1;
    }
    // Mouse: hover on a row = cursor step onto it (help + SE 6) while nothing is edited; a click on a value / selector
    // arrow / bar position sets it and saves (one click); a click on a label is Cross (GAME CONFIG semantics).
    void mouseInput() override {
        if (!ready() || messageBox().isOpen()) return;
        if (selected_ == -1) {
            int l = edgeHoverScroll(kLineCount);
            if (l >= 0) {
                const int in = l == top_ ? 1 : -1;   // nearest selectable row, moving inward from the edge
                while (visible(l) && !selectable(l)) l += in;
                if (visible(l) && l != gSettingsCursor) {
                    gSettingsCursor = static_cast<signed char>(l);
                    setHelp();
                    sound().playSe(1.0f, 6);
                }
            }
        }
        const int h = mouse::hovered(this);
        if (h < 0 || (h >= 900 && h < 1000)) return;
        const int line = h < 100 ? h : (h >= 2000 ? static_cast<int>(kMouseSpeed) : (h - 1000) / 8);
        if (selected_ == -1 && line != gSettingsCursor && selectable(line)) {
            gSettingsCursor = static_cast<signed char>(line);
            setHelp();
            sound().playSe(1.0f, 6);
        }
        if (h >= 1000) pending_ = h;
    }
    void mouseItems() override {
        if (isOpen == 0 || titleSettled == 0 || messageBox().isOpen()) return;
        for (int l = top_; l < top_ + kVisible && l < kLineCount; ++l) {
            if (!selectable(l)) continue;
            const float y = lineY(l);
            mouse::row(this, l, kLabelX - 8.0f, 476.0f, y, kPitch);
            if (selected_ != -1 && selected_ != l) continue;
            const LineDef& d = kLines[l];
            if (d.kind == kList) {
                for (int c = 0; c < d.count; ++c) mouse::row(this, 1000 + l * 8 + c, choiceX_[l][c] - 2.0f, choiceX_[l][c] + choiceW_[l][c] + 2.0f, y, kPitch);
            } else if (d.kind == kSelect) {
                mouse::row(this, 1000 + l * 8 + 0, kValueX - 6.0f, kValueX + 14.0f, y, kPitch);
                mouse::row(this, 1000 + l * 8 + 1, selRightX_ - 4.0f, selRightX_ + 16.0f, y, kPitch);
            } else if (d.kind == kSlider) {
                const float x0 = kConfigBarPos[0][0] - 16.0f;
                for (int v = 0; v < 11; ++v) mouse::row(this, 2000 + v, x0 + 16.0f * static_cast<float>(v), x0 + 16.0f * static_cast<float>(v + 1), y, kPitch);
            }
        }
        scrollMarkItems(kLineCount);
        if (selected_ != -1 && kLines[selected_].kind != kAction) mouse::wheelHorizontal();
        else mouse::wheelOff();
    }
    bool wantsNext() const override { return wantsNext_ != 0; }
    MenuPage* nextPage() override;

private:
    void setHelp() { setHelpText(lineHelp(gSettingsCursor)); }
    void cursorStep(int dir) {
        if (selected_ != -1 || messageBox().isOpen()) return;
        int l = gSettingsCursor;
        const bool wheel = mouse::lastPressFromWheel();   // PORT: the wheel stops at the ends instead of wrapping
        do {
            l += dir;
            if (wheel && (l >= kLineCount || l < 0)) return;
            if (l >= kLineCount) l = 0;
            if (l < 0) l = kLineCount - 1;
        } while (!selectable(l));
        gSettingsCursor = static_cast<signed char>(l);
        scrollTo(l, kLineCount, !selectable(l - 1) || l == 1);
        setHelp();
        sound().playSe(1.0f, 6);
    }
    void backup() { bakPort_ = present::portSettings(); bakCtl_ = platform::controls(); bakAim_ = aimAssistLevel(); bakAtk_ = cameraRelativeAttacks(); }
    void restore() {
        present::portSettings() = bakPort_;
        present::applyPortDisplay();
        platform::Controls c = platform::controls();
        c.freeCamera = bakCtl_.freeCamera; c.mouseSensX = bakCtl_.mouseSensX; c.mouseSensY = bakCtl_.mouseSensY;
        c.mouseInvertX = bakCtl_.mouseInvertX; c.mouseInvertY = bakCtl_.mouseInvertY;
        platform::setControls(c);
        setAimAssistLevel(bakAim_, false);
        setCameraRelativeAttacks(bakAtk_, false);
    }
    // Cross (FUN_00160680 pattern)
    void confirm() {
        if (messageBox().isOpen()) return;
        const int l = gSettingsCursor;
        if (selected_ == -1) {
            const Kind k = kLines[l].kind;
            if (k == kPage) { selected_ = static_cast<signed char>(l); wantsNext_ = 1; sound().playSe(1.0f, 8); return; }
            if (k == kAction) { selected_ = static_cast<signed char>(l); messageBox().open(0x40, 999999); return; }
            selected_ = static_cast<signed char>(l);
            backup();
            sound().playSe(1.0f, 1);
        } else if (selected_ != kDefaults) {
            saveLine(selected_);
            selected_ = -1;
            sound().playSe(1.0f, 1);
        }
    }
    // Circle while editing (FUN_00160930 pattern)
    void cancel() {
        if (selected_ == -1 || selected_ == kDefaults) return;
        restore();
        selected_ = -1;
        sound().playSe(1.0f, 2);
    }
    void leave() {
        if (messageBox().isOpen()) return;
        wantsNext_ = 1;
        sound().playSe(1.0f, 8);
    }
    // Left / Right on the edited value: two-value rows flip (as GAME CONFIG's toggles), longer rows clamp (as volumes)
    void adjust(int dir) {
        if (selected_ < 0 || selected_ == kDefaults) return;
        const LineDef& d = kLines[selected_];
        const int n = d.kind == kSlider ? 11 : d.count;
        const int v = getValue(selected_);
        int nv = v + dir;
        if (n == 2) nv = v ^ 1;
        if (nv < 0 || nv >= n) return;
        if (voiceMissing(selected_, nv)) return;
        setValue(selected_, nv);
        sound().playSe(1.0f, 6);
    }
    // PORT (voice language): the other language comes from a second disc image set up at start-up; without it the
    // value stays and the help line says what to do (SE 2, the menus' "cannot" sound)
    bool voiceMissing(int line, int v) {
        if (line != kVoice || audio::voicesAvailable(v != 0)) return false;
        setHelpText(v != 0 ? "English voices need your Baroque (Europe) disc image.\nPut the .iso next to Baroque.exe and restart"
                           : "Japanese voices need your Baroque (Japan) disc image.\nPut the .iso next to Baroque.exe and restart");
        sound().playSe(1.0f, 2);
        return true;
    }
    // a click on a value: set + save in one step
    void clickValue(int item) {
        int line = 0, v = 0;
        if (item >= 2000) { line = kMouseSpeed; v = item - 2000; }
        else {
            line = (item - 1000) / 8;
            v = (item - 1000) % 8;
            if (kLines[line].kind == kSelect) {
                v = getValue(line) + (v == 0 ? -1 : 1);
                if (v < 0 || v >= kLines[line].count) return;
            }
        }
        if (selected_ != -1 && selected_ != line) return;
        gSettingsCursor = static_cast<signed char>(line);
        const bool changed = getValue(line) != v;
        if (changed && voiceMissing(line, v)) { selected_ = -1; return; }
        if (changed) setValue(line, v);
        saveLine(line);
        const bool wasEditing = selected_ != -1;
        if (kLines[line].kind == kList || wasEditing) selected_ = -1;
        sound().playSe(1.0f, (kLines[line].kind == kList || wasEditing) ? 1 : 6);
    }
    // the "Default Settings" window 0x40 (FUN_0015e920 pattern): Yes resets this page's settings (key bindings keep)
    void defaultsDialog() {
        if (selected_ != kDefaults) return;
        const int c = messageBox().choice();
        if (c == 1) {
            const float windowAspect = present::portSettings().windowAspect;
            present::portSettings() = present::PortSettings{};
            present::portSettings().windowAspect = windowAspect;
            present::applyPortDisplay();
            present::savePortSettings();
            const platform::Controls d = platform::defaultControls();
            platform::Controls cur = platform::controls();
            cur.freeCamera = d.freeCamera; cur.mouseSensX = d.mouseSensX; cur.mouseSensY = d.mouseSensY;
            cur.mouseInvertX = d.mouseInvertX; cur.mouseInvertY = d.mouseInvertY;
            platform::setControls(cur);
            setAimAssistLevel(AimAssistLevel::Subtle, false);
            setCameraRelativeAttacks(true, false);
            platform::saveControls(platform::controls());
            messageBox().close();
            selected_ = -1;
        } else if (c == 2) {
            selected_ = -1;
            messageBox().close();
        }
    }
    void hideSprites() {
        for (Sprite* s : bar_) if (s) s->color = 0;
        for (Sprite* s : underline_) if (s) s->color = 0;
        for (Sprite* s : {knob_, pageArrow_, selLeft_, selRight_, scrollUp_, scrollDown_}) if (s) s->color = 0;
    }
    // value list "A / B / C" at x 280, highlighted like GAME CONFIG's drawToggle
    void drawList(int l, float y) {
        TextRenderer& t = text();
        const LineDef& d = kLines[l];
        const bool active = selected_ == l;
        const uint32_t base = active ? kText : kDim;
        const int cur = getValue(l);
        t.x = kValueX; t.y = y; t.z = 0.9f;
        for (int c = 0; c < d.count; ++c) {
            if (c != 0) { t.color = col(base, alpha_); t.draw("%s", kSlash); }
            choiceX_[l][c] = t.x;
            t.color = (c == cur) ? (active ? col(kRed, blink_) : col(kRed, alpha_)) : col(base, alpha_);
            t.draw("%s", d.names[c]);
            choiceW_[l][c] = t.x - choiceX_[l][c];
        }
    }
    void drawSelect(int l, float y) {
        TextRenderer& t = text();
        const bool active = selected_ == l;
        const int cur = getValue(l);
        t.x = kValueX + 18.0f; t.y = y; t.z = 0.9f;
        t.color = active ? col(kRed, blink_) : col(kRed, alpha_);
        t.draw("%s", kLines[l].names[cur]);
        selRightX_ = t.x + 8.0f;
        const uint32_t on = active ? col(0x7f7f7f, alpha_) : col(kDim, alpha_);
        if (selLeft_) { selLeft_->cx = kValueX + 5.0f; selLeft_->cy = y + 2.0f + 6.5f; selLeft_->color = cur > 0 ? on : col(kDim, alpha_ / 2); }
        if (selRight_) {
            selRight_->cx = selRightX_ + 5.0f; selRight_->cy = y + 2.0f + 6.5f;
            selRight_->color = cur + 1 < kLines[l].count ? on : col(kDim, alpha_ / 2);
        }
    }
    void drawSlider(int l, float y) {
        for (int i = 0; i < 9; ++i) {
            if (!bar_[i]) continue;
            bar_[i]->cy = y + 6.0f + kConfigBarPos[i][3] * 0.5f;
            bar_[i]->color = col(0x7f7f7f, alpha_);
        }
        if (knob_) {   // FUN_0015e9d0: vol * 0.1 * 160 + bar x - 16 + 8
            knob_->cx = static_cast<float>(getValue(l)) * 0.1f * 160.0f + kConfigBarPos[0][0] + -16.0f + 8.0f;
            knob_->cy = y + 3.0f + 7.5f;
            const uint32_t base = selected_ == l ? kText : kDim;
            knob_->color = selected_ == l ? col(base, blink_) : col(base, alpha_);
        }
    }
    void draw() {
        animate();
        hideSprites();
        textBegin();
        TextRenderer& t = text();
        int header = 0;
        for (int l = 0; l < kLineCount; ++l) {
            const LineDef& d = kLines[l];
            if (d.kind == kHead) {
                Sprite* u = underline_[header++];
                if (!visible(l)) continue;
                const float y = lineY(l);
                t.x = kLabelX - 8.0f; t.y = y; t.z = 0.9f;
                t.color = col(0x7f7f7f, alpha_);
                t.draw("%s", d.label);
                if (u) { u->cx = kLabelX - 8.0f + u->hw; u->cy = y + 22.0f; u->color = col(0x7f7f7f, alpha_); }
                continue;
            }
            if (!visible(l)) continue;
            const float y = lineY(l);
            t.x = kLabelX; t.y = y; t.z = 0.9f;
            t.color = (gSettingsCursor == l) ? col(kRed, alpha_) : col(kText, alpha_);
            t.draw("%s", lineLabel(l));
            switch (d.kind) {
            case kList: drawList(l, y); break;
            case kSelect: drawSelect(l, y); break;
            case kSlider: drawSlider(l, y); break;
            case kPage:
                if (pageArrow_) { pageArrow_->cy = y + 6.5f; pageArrow_->color = col(kDim, alpha_); }
                break;
            default: break;
            }
        }
        drawScrollMarks(kLineCount);
        if (!helpBar().isSet(0) && alpha_ >= 0x40) setHelp();
        textEnd();
    }
    signed char selected_ = -1;
    int pending_ = -1;
    Sprite* bar_[9] = {};
    Sprite* knob_ = nullptr;
    Sprite* pageArrow_ = nullptr;
    Sprite* selLeft_ = nullptr;
    Sprite* selRight_ = nullptr;
    Sprite* underline_[2] = {};
    float choiceX_[kLineCount][5] = {};
    float choiceW_[kLineCount][5] = {};
    float selRightX_ = kValueX + 80.0f;
    present::PortSettings bakPort_{};
    platform::Controls bakCtl_{};
    AimAssistLevel bakAim_ = AimAssistLevel::Subtle;
    bool bakAtk_ = true;
};

// ---- KEY BINDINGS --------------------------------------------------------------------------------------------------
// PC keyboard keys / mouse buttons per PS2 pad input (controls.ini [keyboard] / [mouse]). Cross on a row waits for a
// key or mouse button (Esc cancels, Delete clears the row); the new key replaces the row's first key and is removed
// from every other row. Which game action a PS2 button performs is still KEY ASSIGN's (GAME CONFIG item 6).
using platform::PadInput;
constexpr PadInput kBindOrder[] = {
    platform::IN_SQUARE, platform::IN_CROSS, platform::IN_CIRCLE, platform::IN_TRIANGLE, platform::IN_L1, platform::IN_R1,
    platform::IN_L2, platform::IN_R2, platform::IN_R3, platform::IN_SELECT, platform::IN_START,
    platform::IN_LS_UP, platform::IN_LS_DOWN, platform::IN_LS_LEFT, platform::IN_LS_RIGHT, platform::IN_WALK,
    platform::IN_RS_UP, platform::IN_RS_DOWN, platform::IN_RS_LEFT, platform::IN_RS_RIGHT,
    platform::IN_UP, platform::IN_DOWN, platform::IN_LEFT, platform::IN_RIGHT, platform::IN_L3,
};
constexpr int kBindRows = static_cast<int>(sizeof(kBindOrder) / sizeof(kBindOrder[0]));
constexpr int kBindLines = kBindRows + 1;   // + "Default Settings"
constexpr float kIconX = 248.0f;

// KEY ASSIGN action of a face / shoulder button (getter table PTR_FUN_0044c0d0 order, strings 0x5c..0x61; the item
// slot pair 0x62 "Scroll Through Item Slot" is split into its two directions: slot 6 previous, slot 4 next)
const char* actionName(uint32_t mask) {
    static constexpr int kSlot[8] = {2, 1, 0, 3, 7, 5, 6, 4};
    for (int i = 0; i < 8; ++i) {
        if (buttons().slot[kSlot[i]] != mask) continue;
        if (i == 6) return "Previous Item Slot";
        if (i == 7) return "Next Item Slot";
        return str(0x5c + i);
    }
    return nullptr;
}
const char* bindLabel(PadInput in) {
    switch (in) {
    case platform::IN_R3: return "Camera Reset";
    case platform::IN_L3: return "L3 Button";
    case platform::IN_SELECT: return "Floor Map";
    case platform::IN_START: return "Pause / Skip";
    case platform::IN_LS_UP: return "Move Forward";
    case platform::IN_LS_DOWN: return "Move Back";
    case platform::IN_LS_LEFT: return "Move Left";
    case platform::IN_LS_RIGHT: return "Move Right";
    case platform::IN_WALK: return "Walk";
    case platform::IN_RS_UP: return "Camera Up";
    case platform::IN_RS_DOWN: return "Camera Down";
    case platform::IN_RS_LEFT: return "Camera Left";
    case platform::IN_RS_RIGHT: return "Camera Right";
    case platform::IN_UP: return "Cursor Up";
    case platform::IN_DOWN: return "Cursor Down";
    case platform::IN_LEFT: return "Cursor Left";
    case platform::IN_RIGHT: return "Cursor Right";
    default: break;
    }
    const char* a = actionName(1u << static_cast<int>(in));
    return a != nullptr ? a : platform::padInputName(in);
}
std::string prettyKey(int key) {
    std::string n = platform::keyName(key);
    if (n.rfind("LEFT_", 0) == 0) n = "L-" + n.substr(5);
    else if (n.rfind("RIGHT_", 0) == 0) n = "R-" + n.substr(6);
    if (n == "L-CONTROL") n = "L-CTRL";
    if (n == "R-CONTROL") n = "R-CTRL";
    for (char& ch : n) if (ch == '_') ch = ' ';
    return n;
}
const char* mouseLabel(int m) {
    static const char* const kNames[] = {"", "Mouse L", "Mouse R", "Mouse M", "Mouse 4", "Mouse 5", "Mouse 6", "Mouse 7", "Wheel Up", "Wheel Down"};
    return m > 0 && m < 10 ? kNames[m] : "";
}
std::string bindValue(const platform::InputBinding& b) {
    std::string s;
    int shown = 0;
    for (int k : b.keys) {
        if (k == 0) continue;
        if (shown == 2) { s += ",.."; break; }
        if (!s.empty()) s += ", ";
        s += prettyKey(k);
        ++shown;
    }
    if (b.mouse[0] != 0) { if (!s.empty()) s += ", "; s += mouseLabel(b.mouse[0]); }
    return s.empty() ? "---" : s;
}

class PcBindingsPage final : public PcPageBase {
public:
    const char* debugName() const override { return "KEY BINDINGS"; }
    void open() override {
        openBase("KEY BINDINGS");
        if (gBindCursor < 0 || gBindCursor >= kBindLines) gBindCursor = 0;
        top_ = 0;
        scrollTo(gBindCursor, kBindLines, false);
        selected_ = -1;
        capture_ = 0;
    }
    void close() override {
        closeBase();
        for (Sprite*& s : icons_) sprites().destroyRef(s);
    }
    void update() override {
        if (isOpen == 0) return;
        animateTitle();
        if (titleSettled != 0 && isOpen != 0) { draw(); defaultsDialog(); }
        if (capture_ != 0) captureTick();
    }
    void input() override {
        if (!ready() || capture_ != 0) return;
        if (waitRelease_) { if (anyDown()) return; waitRelease_ = false; }
        const PadPort& p = pad().port[0];
        if (messageBox().isOpen()) return;
        if (p.pressed & 0x20) { wantsNext_ = 1; sound().playSe(1.0f, 8); }
        else if (p.pressed & 0x40) decide();
        else if (p.repeat & (0x4000 | 0x20000)) step(+1);
        else if (p.repeat & (0x1000 | 0x10000)) step(-1);
    }
    void mouseInput() override {
        if (!ready() || capture_ != 0 || messageBox().isOpen()) return;
        const int l = edgeHoverScroll(kBindLines);
        if (l >= 0 && l != gBindCursor) {
            gBindCursor = static_cast<signed char>(l);
            setHelp();
            sound().playSe(1.0f, 6);
        }
        const int h = mouse::hovered(this);
        if (h < 0 || h >= kBindLines || h == gBindCursor) return;
        gBindCursor = static_cast<signed char>(h);
        setHelp();
        sound().playSe(1.0f, 6);
    }
    void mouseItems() override {
        if (isOpen == 0 || titleSettled == 0 || capture_ != 0 || messageBox().isOpen()) return;
        for (int l = top_; l < top_ + kVisible && l < kBindLines; ++l) mouse::row(this, l, kLabelX - 8.0f, 476.0f, lineY(l), kPitch);
        scrollMarkItems(kBindLines);
        mouse::wheelOff();
    }
    bool wantsNext() const override { return wantsNext_ != 0; }
    MenuPage* nextPage() override { return newPcSettingsPage(gFrom); }

private:
    void setHelp() {
        if (capture_ != 0) setHelpText("Press a key or button  (Esc: cancel / Del: clear)");
        else if (gBindCursor == kBindRows) setHelpText(str(0x42));
        else setHelpText("Choose the key or mouse button to use");
    }
    void step(int dir) {
        int l = gBindCursor + dir;
        if (mouse::lastPressFromWheel() && (l >= kBindLines || l < 0)) return;   // PORT: wheel clamps at the ends
        if (l >= kBindLines) l = 0;
        if (l < 0) l = kBindLines - 1;
        gBindCursor = static_cast<signed char>(l);
        scrollTo(l, kBindLines, false);
        setHelp();
        sound().playSe(1.0f, 6);
    }
    void decide() {
        if (gBindCursor == kBindRows) { selected_ = gBindCursor; messageBox().open(0x40, 999999); return; }
        capture_ = 1;
        snapshotDown();
        setHelp();
        sound().playSe(1.0f, 1);
    }
    void defaultsDialog() {
        if (selected_ != kBindRows) return;
        const int c = messageBox().choice();
        if (c == 1) {
            platform::Controls cur = platform::controls();
            const platform::Controls d = platform::defaultControls();
            for (int i = 0; i < platform::IN_COUNT; ++i) {
                for (int k = 0; k < platform::kMaxKeys; ++k) cur.bind[i].keys[k] = d.bind[i].keys[k];
                for (int k = 0; k < platform::kMaxMouse; ++k) cur.bind[i].mouse[k] = d.bind[i].mouse[k];
            }
            cur.releaseCursorKey = d.releaseCursorKey;
            platform::setControls(cur);
            platform::saveControls(cur);
            messageBox().close();
            selected_ = -1;
            setHelp();
        } else if (c == 2) {
            selected_ = -1;
            messageBox().close();
        }
    }
    // ---- capture (raylib state read directly; the pad sample is ignored while waiting) ----
    static constexpr int kKeyLo = 32, kKeyHi = 349;
    static bool knownKey(int k) { return platform::keyFromName(platform::keyName(k)) == k; }
    bool anyDown() const {
        for (int k = kKeyLo; k < kKeyHi; ++k) if (IsKeyDown(k)) return true;
        for (int b = MOUSE_BUTTON_LEFT; b <= MOUSE_BUTTON_BACK; ++b) if (IsMouseButtonDown(b)) return true;
        return false;
    }
    void snapshotDown() {
        for (int k = kKeyLo; k < kKeyHi; ++k) keyHeld_[k] = IsKeyDown(k);
        for (int b = MOUSE_BUTTON_LEFT; b <= MOUSE_BUTTON_BACK; ++b) mouseHeld_[b] = IsMouseButtonDown(b);
    }
    void captureTick() {
        int key = 0, mb = 0;
        for (int k = kKeyLo; k < kKeyHi && key == 0; ++k) {
            const bool down = IsKeyDown(k);
            if (down && !keyHeld_[k]) key = k;
            keyHeld_[k] = down;
        }
        for (int b = MOUSE_BUTTON_LEFT; b <= MOUSE_BUTTON_BACK && key == 0 && mb == 0; ++b) {
            const bool down = IsMouseButtonDown(b);
            if (down && !mouseHeld_[b]) mb = b + 1;   // platform::MouseBind = raylib button + 1
            mouseHeld_[b] = down;
        }
        if (key == 0 && mb == 0) return;
        const int in = kBindOrder[gBindCursor];
        platform::Controls c = platform::controls();
        if (key == KEY_ESCAPE) {
            sound().playSe(1.0f, 2);
        } else if (key == KEY_DELETE) {
            for (int& k : c.bind[in].keys) k = 0;
            for (int& m : c.bind[in].mouse) m = 0;
            sound().playSe(1.0f, 2);
        } else if (key != 0 && knownKey(key)) {
            for (auto& b : c.bind) {   // the key leaves every other row
                int n = 0;
                for (int k : b.keys) if (k != key && k != 0) b.keys[n++] = k;
                for (; n < platform::kMaxKeys; ++n) b.keys[n] = 0;
            }
            int* keys = c.bind[in].keys;
            for (int k = platform::kMaxKeys - 1; k > 0; --k) keys[k] = keys[k - 1];
            keys[0] = key;
            int n = 1;   // drop the old first key (replaced), keep the others
            for (int k = 2; k < platform::kMaxKeys; ++k) if (keys[k] != 0) keys[n++] = keys[k];
            for (; n < platform::kMaxKeys; ++n) keys[n] = 0;
            sound().playSe(1.0f, 1);
        } else if (mb != 0) {
            for (auto& b : c.bind)
                for (int& m : b.mouse) if (m == mb) m = 0;
            c.bind[in].mouse[0] = mb;
            sound().playSe(1.0f, 1);
        } else {
            return;   // unknown key code: keep waiting
        }
        platform::setControls(c);
        platform::saveControls(c);
        capture_ = 0;
        waitRelease_ = true;
        setHelp();
    }
    void draw() {
        animate();
        drawScrollMarks(kBindLines);
        for (Sprite*& s : icons_) sprites().destroyRef(s);   // rebuilt every frame (scrolling, KEY ASSIGN swaps)
        textBegin();
        TextRenderer& t = text();
        const platform::Controls& c = platform::controls();
        for (int l = top_; l < top_ + kVisible && l < kBindLines; ++l) {
            const float y = lineY(l);
            const bool cur = gBindCursor == l;
            t.x = kLabelX; t.y = y; t.z = 0.9f;
            t.color = cur ? col(kRed, alpha_) : col(kText, alpha_);
            if (l == kBindRows) { t.draw("%s", str(0x38)); continue; }
            const PadInput in = kBindOrder[l];
            t.draw("%s", bindLabel(in));
            const uint32_t mask = 1u << static_cast<int>(in);
            if (in < platform::IN_SELECT) {   // the PS2 button glyph (FUN_00166eb0 sizes of KEY ASSIGN)
                const bool face = mask == 0x10 || mask == 0x20 || mask == 0x40 || mask == 0x80;
                const float w = face ? 16.0f : 18.0f, h = face ? 17.0f : (mask == 8 || mask == 4 ? 15.0f : 17.0f);
                Sprite* s = buttonIcon(kIconX, y, 6.0f, w, h, mask, 1);
                if (s) {
                    s->color = col(0x7f7f7f, alpha_);
                    s->keepGlyph = 1;   // PORT: this row names the PS2 button itself (never a keyboard keycap)
                }
                icons_[l - top_] = s;
            }
            t.x = kValueX; t.y = y; t.z = 0.9f;
            if (cur && capture_ != 0) {
                t.color = col(kRed, blink_);
                t.draw("%s", "?");
            } else {
                t.color = cur ? col(kText, alpha_) : col(kDim, alpha_);
                t.draw("%s", bindValue(c.bind[in]).c_str());
            }
        }
        if (!helpBar().isSet(0) && alpha_ >= 0x40) setHelp();
        textEnd();
    }
    signed char selected_ = -1;
    int capture_ = 0;
    bool waitRelease_ = false;
    bool keyHeld_[kKeyHi] = {};
    bool mouseHeld_[8] = {};
    Sprite* icons_[kVisible] = {};
};

MenuPage* PcSettingsPage::nextPage() {
    if (selected_ == kBindings) return new PcBindingsPage();
    return gFrom == 1 ? newGameConfigPage() : newOptionPage();
}
}  // namespace
MenuPage* newPcSettingsPage(int from) { return new PcSettingsPage(from); }
}  // namespace baroque::game::ui
