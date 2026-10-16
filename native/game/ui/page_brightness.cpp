// BRIGHTNESS page (GAME CONFIG item 7; vtable 0x4cdc40; ctor FUN_00163090; open FUN_00163180, close FUN_00163350,
// is-active 0x1639c0 (+0x46c), update FUN_00163550, input FUN_00163730, wants-next 0x1639d0 (+0x498),
// next page 0x1639b0 (= FUN_00163a20 GAME CONFIG)). The value is cRam0051af64 (Options::brightness, 0..10); the
// dimming quad itself is FUN_00168010 (MenuObject::updateBrightness).
#include "game/ui/menu.hpp"
#include "game/fade.hpp"
#include "game/hud/hud_api.hpp"
#include "game/options.hpp"
#include "game/pad.hpp"
#include "game/sound_stub.hpp"
#include "game/ui/help_bar.hpp"
#include "game/ui/mouse_menu.hpp"
#include "game/ui/sprite.hpp"
#include "game/ui/strings.hpp"
#include "game/ui/ui_tables.hpp"
namespace baroque::game::ui {
namespace {
class BrightnessPage final : public MenuPage {
public:
    BrightnessPage() = default;   // orig FUN_00163090 (all fields 0)
    const char* debugName() const override { return "BRIGHTNESS"; }

    // orig FUN_00163180 (work/decompiled/00163180.c)
    void open() override {
        makeFrame();
        isOpen = 1;
        for (int i = 0; i < 9; ++i) {
            const float* p = kConfigBarPos[i];
            const float* uv = kConfigBarUv[i];
            bar_[i] = sprites().create(p[0] + -110.0f, p[1] + 215.0f, 1.0f, p[2], p[3], uv[0], uv[1], uv[2], uv[3], 0x2e, 1);
        }
        knob_ = sprites().create(kConfigBarPos[0][0] + -16.0f + -110.0f, 359.0f, 2.0f, 6.0f, 15.0f, 115.0f, 0.0f, 121.0f, 15.0f, 0x30, 1);
        makeFrame2(0);
        // iRam00517b68 (item screen open) -> uRam0051aa1c = uRam0051aa20 = 0: help slots 1 / 2 (item screen) hidden
        if (itemScreen().isOpen()) { helpBar().setEnabled(1, 0); helpBar().setEnabled(2, 0); }
        // backup 0x51af70 <- 0x51af30, every word except the brightness byte's word +0x34
        Options& b = optionsBackup();
        const Options& o = options();
        b.initialized = o.initialized;
        b.cursorPos = o.cursorPos;
        b.mono = o.mono;
        b.bgmVol = o.bgmVol;
        b.seVol = o.seVol;
        b.voiceVol = o.voiceVol;
        b.vibrationOff = o.vibrationOff;
        for (int i = 0; i < 8; ++i) b.keys[i] = o.keys[i];
        b.camera = o.camera;
    }

    // orig FUN_00163350 (work/decompiled/00163350.c)
    void close() override {
        freeFrame2();
        helpBar().clear(0);
        freeFrame();
        isOpen = 0;
        for (Sprite*& s : bar_) sprites().destroyRef(s);
        sprites().destroyRef(knob_);
    }

    // orig FUN_00163550 (work/decompiled/00163550.c)
    void update() override {
        if (isOpen == 0) return;
        angle_ += 8;
        if (0x168 < angle_) angle_ = -360;
        // FUN_00206940(angle): result unused
        if (alpha_ < 0x40 && fade().finished()) {
            alpha_ += 6;
            if (0x40 < alpha_) alpha_ = 0x40;
        } else {
            alpha_ = 0x40;
        }
        if (!helpBar().isSet(0) && alpha_ == 0x40) {
            helpBar().set(32.0f, 403.0f, 448.0f, 40.0f, 0, str(100));
            helpBar().setSpeed(0, 4);
        }
        for (Sprite* s : bar_)
            if (s != nullptr) s->color = (alpha_ << 24) + 0x7f7f7f;
        if (knob_ != nullptr) {
            knob_->cx = static_cast<float>(options().brightness) * 0.1f * 160.0f +
                        ((kConfigBarPos[0][0] + -16.0f) - -16.0f) + -110.0f + 8.0f + 0.0f;
            knob_->color = (alpha_ << 24) + 0x7f7f7f;
        }
    }

    // orig FUN_00163730 (work/decompiled/00163730.c)
    void input() override {
        if (isOpen == 0 || !fade().finished() || alpha_ <= 0x3f) return;
        const PadPort& p = pad().port[0];
        Options& o = options();
        if (p.pressed & 0x20) {
            wantsNext_ = 1;
            sound().playSe(1.0f, 2);
            o = optionsBackup();                           // full restore 0x51af70 -> 0x51af30 (brightness included)
        } else if (p.pressed & 0x40) {
            wantsNext_ = 1;
            gOptionsChanged = 1;
            sound().playSe(1.0f, 1);
        } else if ((p.repeat & 0x8000) != 0 || (p.repeat & 0x40000) != 0) {
            o.brightness = static_cast<int8_t>(o.brightness - 1);
            if (o.brightness < 0) o.brightness = 0;
            else sound().playSe(1.0f, 6);
        } else if ((p.repeat & 0x2000) != 0 || (p.repeat & 0x80000) != 0) {
            o.brightness = static_cast<int8_t>(o.brightness + 1);
            if (o.brightness < 11) sound().playSe(1.0f, 6);
            else o.brightness = 10;
        }
    }

    // PORT: mouse menu layer. The slider (bar pieces kConfigBarPos + (-110, 215) and the knob at y 359) is one item:
    // a click on it is Cross (accept); the wheel is Left/Right on this page.
    void mouseItems() override {
        if (isOpen == 0) return;
        const float x0 = kConfigBarPos[0][0] - 110.0f - 8.0f;
        const float x1 = kConfigBarPos[8][0] + kConfigBarPos[8][2] - 110.0f + 8.0f;
        mouse::item(this, 0, x0, 352.0f, x1 - x0, 28.0f);
        mouse::wheelHorizontal();
    }

    bool wantsNext() const override { return wantsNext_ != 0; }   // 0x1639d0
    MenuPage* nextPage() override { return newGameConfigPage(); }  // 0x1639b0: j FUN_00163a20

private:
    Sprite* bar_[9] = {};            // +0x470
    Sprite* knob_ = nullptr;         // +0x494
    int wantsNext_ = 0;              // +0x498
    int angle_ = 0;                  // +0x49c
    unsigned alpha_ = 0;             // +0x4a0
};
}  // namespace
MenuPage* newBrightnessPage() { return new BrightnessPage(); }   // FUN_00163a70 (alloc FUN_00163ba0 + ctor FUN_00163090)
}  // namespace baroque::game::ui
