// BAROQUE LIST page (OPTION mode 4; vtable 0x4cf220; ctor FUN_001faee0; open 0x1fafd0 (disassembled), close
// FUN_001fb140, is-active 0x1fbcc0 (+0x46c), update 0x1fb350 (title + FUN_001fb4b0), input FUN_001fb3a0,
// wants-next 0x1fbcd0 (+0x470), next page 0x1fb340 (= FUN_00163b10 OPTION)).
// 0x40 entries in two columns of 9; entry n is unlocked by flag 0x483 + n, named str(0x82 + n) (FUN_001fafb0) and
// described by str(0xc2 + n) (FUN_001fafc0). View globals: iRam0053e9d0 first entry, iRam0053e9d8 cursor (0..17).
#include "game/ui/database.hpp"
#include "game/ui/menu.hpp"
#include "game/fade.hpp"
#include "game/flag_manager.hpp"
#include "game/options.hpp"
#include "game/pad.hpp"
#include "game/sound_stub.hpp"
#include "game/ui/help_bar.hpp"
#include "game/ui/mouse_menu.hpp"
#include "game/ui/sprite.hpp"
#include "game/ui/strings.hpp"
#include "game/ui/text.hpp"
namespace baroque::game::ui {
namespace {
int gBaroqueTop = 0;      // iRam0053e9d0
int gBaroqueCursor = 0;   // iRam0053e9d8
constexpr uint32_t kTextRgb = 0xbbbbbb;         // DAT_0044c190
constexpr float kScaleW = 1.16f;                // DAT_0044c180
constexpr float kScaleH = 1.22f;                // DAT_0044c188
const char kLocked[] = "\x81H\x81H\x81H\x81H\x81H\x81H\x81H";   // 0x469650 (seven full-width '?')

bool unlocked(int n) { return flags().test(static_cast<uint32_t>(n + 0x483)); }

class BaroqueListPage final : public MenuPage {
public:
    BaroqueListPage() = default;   // orig FUN_001faee0 (all fields 0)
    const char* debugName() const override { return "BAROQUE LIST"; }

    // orig 0x1fafd0 (not exported by Ghidra; disassembled)
    void open() override {
        makeFrame();
        makeTitle(str(6));
        isOpen = 1;
        wantsNext_ = 0;
        angle_ = 0;
        alpha_ = 0;
        if (options().cursorPos == 1) {
            gBaroqueTop = 0;
            gBaroqueCursor = 0;
        }
        makeFrame2(2);
        for (int i = 0; i < 2; ++i) {
            const float y = static_cast<float>(0x40 + i * 0x10b);
            left_[i] = sprites().create(24.0f, y, 1.0f, 10.0f, 13.0f, 25.0f, 17.0f, 35.0f, 30.0f, 0x28, 1);
            right_[i] = sprites().create(478.0f, y, 1.0f, 10.0f, 13.0f, 35.0f, 17.0f, 45.0f, 30.0f, 0x28, 1);
        }
    }

    // orig FUN_001fb140 (work/decompiled/001fb140.c)
    void close() override {
        helpBar().clear(0);
        freeFrame2();
        freeFrame();
        isOpen = 0;
        for (int i = 0; i < 2; ++i) {
            sprites().destroyRef(left_[i]);
            sprites().destroyRef(right_[i]);
        }
    }

    // orig 0x1fb350 (disassembled)
    void update() override {
        if (isOpen == 0) return;
        animateTitle();
        if (titleSettled != 0) draw();                     // FUN_001fb4b0
    }

    // orig FUN_001fb3a0 (work/decompiled/001fb3a0.c)
    void input() override {
        if (isOpen == 0 || alpha_ <= 0x3f) return;
        const PadPort& p = pad().port[0];
        if ((p.repeat & 0x8000) != 0 || (p.repeat & 0x40000) != 0) left();          // FUN_001fbab0
        else if ((p.repeat & 0x2000) != 0 || (p.repeat & 0x80000) != 0) right();   // FUN_001fbb60
        else if ((p.repeat & 0x1000) != 0 || (p.repeat & 0x10000) != 0) up();      // FUN_001fba00
        else if ((p.repeat & 0x4000) != 0 || (p.repeat & 0x20000) != 0) down();    // FUN_001fb910
        else if (p.pressed & 0x40) {
            // FUN_001fbc70: empty
        } else if (p.pressed & 0x20) {
            if (isOpen != 0) {                             // FUN_001fbc80
                wantsNext_ = 1;
                sound().playSe(1.0f, 8);
            }
        }
    }

    // PORT: mouse menu layer. Hover on a name = the cursor move of up/down/left/right onto it (help + SE 6); the page
    // arrows are Left/Right (previous / next nine entries, as with the pad).
    void mouseInput() override {
        if (isOpen == 0 || alpha_ <= 0x3f) return;                            // the guard of input()
        const int h = mouse::hovered(this);
        if (h < 0 || h > 0x11 || h == gBaroqueCursor || 0x40 <= h + gBaroqueTop) return;
        gBaroqueCursor = h;
        help(gBaroqueTop + gBaroqueCursor);
        sound().playSe(1.0f, 6);
    }
    // PORT: the two columns of nine names of draw() (x 64 + 224 col, y 75 + 30 row).
    void mouseItems() override {
        if (isOpen == 0 || titleSettled == 0) return;
        for (int i = 0; i < 0x12; ++i) {
            if (0x40 <= i + gBaroqueTop) continue;
            const float x = static_cast<float>(i / 9) * 224.0f + 64.0f;
            mouse::row(this, i, x - 8.0f, x + 216.0f, static_cast<float>(i % 9) * 30.0f + 75.0f, 30.0f);
        }
        for (int k = 0; k < 2; ++k) {
            const float y = static_cast<float>(0x40 + k * 0x10b);
            if (0 < gBaroqueTop - 8) mouse::item(this, 0x20 + k, 20.0f, y - 4.0f, 18.0f, 21.0f, platform::PAD_LEFT);
            if (gBaroqueTop + 9 < 0x38) mouse::item(this, 0x22 + k, 474.0f, y - 4.0f, 18.0f, 21.0f, platform::PAD_RIGHT);
        }
    }

    bool wantsNext() const override { return wantsNext_ != 0; }   // 0x1fbcd0
    MenuPage* nextPage() override { return newOptionPage(); }       // 0x1fb340

private:
    // orig FUN_001fb840 (work/decompiled/001fb840.c)
    void help(int n) {
        if (!unlocked(n)) helpBar().set(32.0f, 369.0f, 448.0f, 54.0f, 0, str(0x102));
        else helpBar().set(32.0f, 369.0f, 448.0f, 54.0f, 0, str(n + 0xc2));   // FUN_001fafc0
        helpBar().setSpeed(0, 4);
    }
    // orig FUN_001fb910 (work/decompiled/001fb910.c)
    void down() {
        const int old = gBaroqueCursor;
        if (isOpen == 0) return;
        bool moved = true;
        ++gBaroqueCursor;
        if (0x11 < gBaroqueCursor || 0x3f < gBaroqueCursor + gBaroqueTop) {
            if (gBaroqueTop + 9 < 0x38) {
                gBaroqueCursor = 9;
                gBaroqueTop += 9;
            } else {
                gBaroqueCursor = 0x11;
                if (0x3f < gBaroqueTop + 0x11) gBaroqueCursor = old;
                moved = false;
            }
        }
        if (moved) {
            help(gBaroqueTop + gBaroqueCursor);
            sound().playSe(1.0f, 6);
        }
    }
    // orig FUN_001fba00 (work/decompiled/001fba00.c)
    void up() {
        if (isOpen == 0) return;
        bool moved = true;
        --gBaroqueCursor;
        if (gBaroqueCursor < 0) {
            if (gBaroqueTop - 8 < 1) {
                gBaroqueCursor = 0;
                moved = false;
            } else {
                gBaroqueTop -= 9;
                gBaroqueCursor = 8;
            }
        }
        if (moved) {
            help(gBaroqueTop + gBaroqueCursor);
            sound().playSe(1.0f, 6);
        }
    }
    // orig FUN_001fbab0 (work/decompiled/001fbab0.c)
    void left() {
        if (isOpen == 0) return;
        if (gBaroqueCursor < 9) {
            if (0 < gBaroqueTop - 8) {
                gBaroqueTop -= 9;
                help(gBaroqueTop + gBaroqueCursor);
                sound().playSe(1.0f, 6);
            }
        } else {
            gBaroqueCursor -= 9;
            help(gBaroqueTop + gBaroqueCursor);
            sound().playSe(1.0f, 6);
        }
    }
    // orig FUN_001fbb60 (work/decompiled/001fbb60.c)
    void right() {
        if (isOpen == 0) return;
        if (gBaroqueCursor < 9 && gBaroqueCursor + gBaroqueTop + 9 < 0x40) {
            gBaroqueCursor += 9;
            if (0x3f < gBaroqueTop + gBaroqueCursor) gBaroqueCursor = 9;
            help(gBaroqueTop + gBaroqueCursor);
            sound().playSe(1.0f, 6);
        } else {
            const int top = gBaroqueTop + 9;
            if (top < 0x38) {
                if (0x3f < top + gBaroqueCursor) gBaroqueCursor = 9;
                gBaroqueTop = top;
                help(top + gBaroqueCursor);
                sound().playSe(1.0f, 6);
            }
        }
    }

    // orig FUN_001fb4b0 (work/decompiled/001fb4b0.c)
    void draw() {
        if (alpha_ < 0x40 && fade().finished()) {
            alpha_ += 6;
            if (0x40 < alpha_) alpha_ = 0x40;
        }
        const uint32_t a = alpha_ << 24;
        TextRenderer& t = text();
        t.color = kTextRgb + a;
        t.halfW = kScaleW * 8.0f;
        t.layerFlag = 1;
        t.halfH = kScaleH * 9.0f;
        int i = 0;
        for (int col = 0; col < 2; ++col) {
            const float x = static_cast<float>(col) * 224.0f + 64.0f;
            for (int row = 0; row < 9; ++row, ++i) {
                if (0x40 <= i + gBaroqueTop) continue;
                const bool on = flags().test(static_cast<uint32_t>(gBaroqueTop + 0x483 + i));
                if (gBaroqueCursor == i) t.color = a + 0x22227f;
                else if (!on) t.color = a + 0x3f3f3f;
                else t.color = kTextRgb + a;
                t.z = 2.0f;
                t.y = static_cast<float>(row) * 30.0f + 75.0f;
                t.x = x;
                if (!on) t.draw("%s", kLocked);            // 0x469648
                else t.draw("%s", str(i + gBaroqueTop + 0x82));   // FUN_001fafb0
            }
        }
        if (!helpBar().isSet(0) && 0x3f < alpha_) {
            help(gBaroqueTop + gBaroqueCursor);
            sound().playBgm(0x19, 4, 1);
            flags().set(0x3b7);
        }
        for (int k = 0; k < 2; ++k) {
            if (left_[k] != nullptr) left_[k]->color = (gBaroqueTop - 8 < 1) ? 0u : a + 0x7f7f7f;
            if (right_[k] != nullptr) right_[k]->color = (gBaroqueTop + 9 < 0x38) ? a + 0x7f7f7f : 0u;
        }
        t.layerFlag = 1;
        t.color = 0x40bbbbbb;                              // DAT_0044c198
    }

    int wantsNext_ = 0;              // +0x470
    int angle_ = 0;                  // +0x474
    unsigned alpha_ = 0;             // +0x478
    Sprite* left_[2] = {};           // +0x47c
    Sprite* right_[2] = {};          // +0x484
};
}  // namespace
MenuPage* newBaroqueListPage() { return new BaroqueListPage(); }   // FUN_00169100 (ctor FUN_001faee0)
}  // namespace baroque::game::ui
