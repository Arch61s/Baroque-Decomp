// KEY ASSIGN page (GAME CONFIG item 6; vtable 0x4cdc70; ctor FUN_00160ee0; open FUN_00160fe0, close FUN_001614c0,
// is-active 0x1639e0 (+0x46c), update FUN_00161850, input FUN_00161ae0, wants-next 0x1639f0 (+0x47c),
// next page 0x161bb0 (= FUN_00163a20 GAME CONFIG)). SCENES.md 11.2 row 6.
// The eight actions are read through the getter table PTR_FUN_0044c0d0 (button slots 2,1,0,3,7,5,6,4 of 0x4e65c0).
#include "game/ui/menu.hpp"
#include "game/hud/hud_api.hpp"
#include "game/fade.hpp"
#include "game/options.hpp"
#include "game/pad.hpp"
#include "game/scene_manager.hpp"
#include "game/sound_stub.hpp"
#include "game/ui/gradient_texture.hpp"
#include "game/ui/help_bar.hpp"
#include "game/ui/menu_tables.hpp"
#include "game/ui/mouse_menu.hpp"
#include "game/ui/sprite.hpp"
#include "game/ui/strings.hpp"
#include "game/ui/text.hpp"
#include "game/ui/texture_registry.hpp"
namespace baroque::game::ui {
namespace {
constexpr uint32_t kTextRgb = 0xbbbbbb;   // DAT_0044c190

// PTR_FUN_0044c0d0: 0x15d7a0 -> 0x4e65d0, 0x15d7b0 -> 0x4e65c8, 0x15d7c0 -> 0x4e65c0, 0x15d7d0 -> 0x4e65d8,
// 0x15d7e0 -> 0x4e65f8, 0x1534f0 -> 0x4e65e8, 0x153510 -> 0x4e65f0, 0x153500 -> 0x4e65e0 (same table as options.cpp)
constexpr int kGetterSlot[8] = {2, 1, 0, 3, 7, 5, 6, 4};
uint32_t getter(int i) { return buttons().slot[kGetterSlot[i]]; }
bool isFace(uint32_t m) { return m == 0x20 || m == 0x10 || m == 0x80 || m == 0x40; }

class KeyAssignPage final : public MenuPage {
public:
    KeyAssignPage() = default;   // orig FUN_00160ee0 (all fields 0)
    const char* debugName() const override { return "KEY ASSIGN"; }

    // orig FUN_00160fe0 (work/decompiled/00160fe0.c)
    void open() override {
        makeFrame();
        makeTitle(str(0xb));
        if (scenes().isCurrent("Game....")) {
            makeGameBackdrop();                            // FUN_00164910
            if (!hudMapShown()) {
                mapHidden_ = 0;
            } else {
                hudMapHide();                              // FUN_00126fb0(0x50a350)
                mapHidden_ = 1;                            // +0x4c4
            }
            // iRam00517b68 (item screen open) -> uRam0051aa1c = uRam0051aa20 = 0: help slots 1 / 2 (item screen) hidden
            if (itemScreen().isOpen()) { helpBar().setEnabled(1, 0); helpBar().setEnabled(2, 0); }
        }
        isOpen = 1;
        faceChoice_ = 0;
        pairChoice_ = 0;
        shoulderChoice_ = 0;
        cursor_ = 0;
        sub_ = 0;
        alpha_ = 0;
        rebuildIcons();                                    // FUN_00161210
        makeFrame2(0);
        arrow_[0] = sprites().create(257.0f, 381.0f, 3.0f, 15.0f, 24.0f, 94.0f, 94.0f, 109.0f, 118.0f, 0x2e, 0);
        arrow_[1] = sprites().create(328.0f, 381.0f, 3.0f, 15.0f, 24.0f, 94.0f, 94.0f, 109.0f, 118.0f, 0x2e, 0);
        for (Sprite* a : arrow_)
            if (a != nullptr) a->color = 0x407f7f7f;
    }

    // orig FUN_001614c0 (work/decompiled/001614c0.c)
    void close() override {
        freeFrame2();
        helpBar().clear(0);
        freeFrame();
        isOpen = 0;
        faceChoice_ = 0;
        for (Sprite*& s : icons_) sprites().destroyRef(s);
        for (int i = 0; i < 8; ++i) options().keys[i] = getter(i);   // +0x14.. of 0x51af30
        if (scenes().isCurrent("Game....") && mapHidden_ != 0) hudMapShow();   // FUN_00126cc0(0x50a350)
        for (Sprite*& a : arrow_) sprites().destroyRef(a);
    }

    // orig FUN_00161850 (work/decompiled/00161850.c)
    void update() override {
        if (isOpen == 0) return;
        animateTitle();
        if (titleSettled != 0) draw();                     // FUN_001618a0
    }

    // orig FUN_00161ae0 (work/decompiled/00161ae0.c)
    void input() override {
        if (isOpen == 0 || !fade().finished() || alpha_ <= 0x3f) return;
        const PadPort& p = pad().port[0];
        if (p.pressed & 0x20) back();                      // FUN_00162470
        else if (p.pressed & 0x40) decide();               // FUN_00161f60
        else if ((p.repeat & 0x4000) != 0 || (p.repeat & 0x20000) != 0) cursorDown();   // FUN_00161bc0
        else if ((p.repeat & 0x1000) != 0 || (p.repeat & 0x10000) != 0) cursorUp();     // FUN_00161d90
    }

    // PORT: mouse menu layer. Hover = the Down step of cursorDown() onto the hovered action (indices 0..7) or, while a
    // choice window is open, onto the hovered choice (indices 100 + sub); cursorDown() places the choice cursor + SE 6.
    void mouseInput() override {
        if (isOpen == 0 || !fade().finished() || alpha_ <= 0x3f) return;   // the guard of input()
        const int h = mouse::hovered(this);
        if (h < 0) return;
        const bool choice = faceChoice_ != 0 || pairChoice_ != 0 || shoulderChoice_ != 0;
        if (!choice && h <= 7 && h != cursor_) {
            cursor_ = static_cast<signed char>(h == 0 ? 7 : h - 1);
            cursorDown();
        } else if (choice && h >= 100) {
            const int n = (pairChoice_ != 0) ? 2 : 4;
            const int t = h - 100;
            if (t >= n || t == sub_) return;
            sub_ = static_cast<signed char>(t == 0 ? n - 1 : t - 1);
            cursorDown();
        }
    }
    // PORT: the action rows of draw() (text at kKeyItemTextPos, icons at x 280..320) and the choice icons of
    // openFaceChoice / openPairChoice / openShoulderChoice (one rectangle per choice around its cursor box).
    void mouseItems() override {
        if (isOpen == 0 || titleSettled == 0) return;
        if (faceChoice_ != 0) {
            for (int i = 0; i < 4; ++i)
                mouse::item(this, 100 + i, 348.0f, static_cast<float>(i) * 32.0f + static_cast<float>(cursor_) * 34.0f + 24.0f, 32.0f, 32.0f);
        } else if (pairChoice_ != 0) {
            for (int i = 0; i < 2; ++i) mouse::item(this, 100 + i, 358.0f, static_cast<float>(i) * 30.0f + 262.0f, 50.0f, 30.0f);
        } else if (shoulderChoice_ != 0) {
            const float base = static_cast<float>(cursor_ - 4) * 34.0f + 170.0f;
            for (int i = 0; i < 4; ++i) mouse::item(this, 100 + i, 348.0f, static_cast<float>(i) * 30.0f + base - 8.0f, 32.0f, 30.0f);
        } else {
            for (int i = 0; i < 8; ++i) {
                const float x = kKeyItemTextPos[i][language() * 2];
                mouse::row(this, i, x - 8.0f, 330.0f, kKeyItemTextPos[i][language() * 2 + 1], 34.0f);
            }
        }
    }

    bool wantsNext() const override { return wantsNext_ != 0; }   // 0x1639f0
    MenuPage* nextPage() override { return newGameConfigPage(); }  // 0x161bb0: j FUN_00163a20

private:
    // orig FUN_00161210 (work/decompiled/00161210.c)
    void rebuildIcons() {
        for (Sprite*& s : icons_) sprites().destroyRef(s);
        for (int i = 0; i < 7; ++i) {
            float w, h;
            const uint32_t m = getter(i);
            if (m == 0x20 || m == 0x10 || m == 0x80 || m == 0x40) {
                w = 16.0f;
                h = 17.0f;
            } else if (m == 8 || m == 4) {
                w = 18.0f;
                h = 15.0f;
            } else {
                w = 18.0f;
                h = 17.0f;
            }
            icons_[i] = buttonIcon(kKeyIconPos[i][0], kKeyIconPos[i][1], 6.0f, w, h, m, 1);
            if (i == 6) icons_[7] = buttonIcon(kKeyIconPos[i + 1][0], kKeyIconPos[i + 1][1], 6.0f, w, h, getter(7), 1);
        }
    }

    // orig FUN_001618a0 (work/decompiled/001618a0.c)
    void draw() {
        if (alpha_ < 0x40 && fade().finished()) {
            alpha_ += 6;
            if (0x40 < alpha_) alpha_ = 0x40;
        } else {
            alpha_ = 0x40;
        }
        if (!helpBar().isSet(0) && alpha_ == 0x40) {
            helpBar().set(32.0f, 403.0f, 448.0f, 40.0f, 0, str(0x5b));
            helpBar().setSpeed(0, 4);
        }
        TextRenderer& t = text();
        t.halfW = 8.0f;
        t.halfH = 9.0f;
        // the list 0x51afd0 = str(0x5c..0x63) (static ctor FUN_004c7d10), terminated by the zero word at 0x51aff0
        for (int i = 0; i < 8; ++i) {
            const float y = kKeyItemTextPos[i][language() * 2 + 1];
            t.x = kKeyItemTextPos[i][language() * 2];
            t.z = 0.9f;
            t.color = (cursor_ == i) ? (alpha_ << 24) + 0x22227f : kTextRgb + (alpha_ << 24);
            t.y = y;
            t.draw("%s\n", str(i + 0x5c));                 // 0x44c118
        }
        for (Sprite* s : icons_)
            if (s != nullptr) s->color = (alpha_ << 24) + 0x7f7f7f;
    }

    // orig FUN_00162470 (work/decompiled/00162470.c): Circle
    void back() {
        if (faceChoice_ == 0 && pairChoice_ == 0 && shoulderChoice_ == 0) {
            wantsNext_ = 1;
            sound().playSe(1.0f, 8);
        } else if (faceChoice_ == 0) {
            if (pairChoice_ == 0) {
                if (shoulderChoice_ != 0) {
                    closeShoulderChoice();                 // FUN_00162eb0
                    closeSubWindowA();
                    sound().playSe(1.0f, 5);
                }
            } else {
                closePairChoice();                         // FUN_00162b00
                closeSubWindowB();
                sound().playSe(1.0f, 5);
            }
        } else {
            closeFaceChoice();                             // FUN_00162740
            closeSubWindowA();
            sound().playSe(1.0f, 5);
        }
    }

    void placeChoiceCursorFace() {
        if (choiceCursor_) choiceCursor_->cy = ((static_cast<float>(sub_) * 32.0f + static_cast<float>(cursor_) * 34.0f + 44.0f) - 4.0f);
    }
    void placeChoiceCursorPair() {
        if (choiceCursor_) choiceCursor_->cy = (static_cast<float>(sub_) * 30.0f + 282.0f) - 5.0f;
    }
    void placeChoiceCursorShoulder() {
        if (choiceCursor_)
            choiceCursor_->cy = ((static_cast<float>(cursor_ - 4) * 34.0f + static_cast<float>(sub_) * 30.0f + 179.0f) - 2.0f);
    }

    // orig FUN_00161bc0 (work/decompiled/00161bc0.c): Down
    void cursorDown() {
        if (faceChoice_ == 0 && pairChoice_ == 0 && shoulderChoice_ == 0) {
            cursor_ = static_cast<signed char>(cursor_ + 1);
            if (cursor_ == 8) cursor_ = 0;
        } else {
            sub_ = static_cast<signed char>(sub_ + 1);
            if (faceChoice_ != 0) {
                if (sub_ == 4) sub_ = 0;
                placeChoiceCursorFace();
            }
            if (pairChoice_ != 0) {
                if (sub_ == 2) sub_ = 0;
                placeChoiceCursorPair();
            }
            if (shoulderChoice_ != 0) {
                if (sub_ == 4) sub_ = 0;
                placeChoiceCursorShoulder();
            }
        }
        sound().playSe(1.0f, 6);
    }
    // orig FUN_00161d90 (work/decompiled/00161d90.c): Up
    void cursorUp() {
        if (faceChoice_ == 0 && pairChoice_ == 0 && shoulderChoice_ == 0) {
            cursor_ = static_cast<signed char>(cursor_ - 1);
            if (cursor_ < 0) cursor_ = 7;
        } else {
            sub_ = static_cast<signed char>(sub_ - 1);
            if (faceChoice_ != 0) {
                if (sub_ < 0) sub_ = 3;
                placeChoiceCursorFace();
            }
            if (pairChoice_ != 0) {
                if (sub_ < 0) sub_ = 1;
                placeChoiceCursorPair();
            }
            if (shoulderChoice_ != 0) {
                if (sub_ < 0) sub_ = 3;
                placeChoiceCursorShoulder();
            }
        }
        sound().playSe(1.0f, 6);
    }

    // orig FUN_00161f60 (work/decompiled/00161f60.c): Cross
    void decide() {
        if (faceChoice_ == 0 && pairChoice_ == 0 && shoulderChoice_ == 0) {
            const uint32_t m = getter(cursor_);
            if (!isFace(m)) {
                const int c = cursor_;
                if (static_cast<unsigned>(c - 4) < 2u) {   // "Throw Item" / "Scroll Through Item Slot"
                    openSubWindowA(static_cast<float>(c) * 34.0f);
                    openShoulderChoice();                  // FUN_00162ce0
                    sound().playSe(1.0f, 4);
                    return;
                }
                if (c != 6) {
                    if (c != 7) return;
                    buttonDefaults();                      // FUN_00100f10 ("Default Settings")
                    rebuildIcons();
                    sound().playSe(1.0f, 2);
                    gOptionsChanged = 1;
                    return;
                }
                openSubWindowB();
                openPairChoice();                          // FUN_00162920
                sound().playSe(1.0f, 4);
                return;
            }
            openSubWindowA(kKeyWindowY[cursor_]);
            openFaceChoice();                              // FUN_00162580
            sound().playSe(1.0f, 4);
            return;
        }
        if (isFace(getter(cursor_))) {
            buttonSwap(getter(cursor_), kKeyChoiceFace[sub_], -1, -1);   // FUN_00100f80
            closeSubWindowA();
            closeFaceChoice();
            rebuildIcons();
            sound().playSe(1.0f, 1);
            gOptionsChanged = 1;
            return;
        }
        const int c = cursor_;
        if (static_cast<unsigned>(c - 4) < 2u) {
            buttonSwap(getter(c), kKeyChoiceShoulder[sub_], -1, -1);
            closeSubWindowA();
            closeShoulderChoice();
            rebuildIcons();
            sound().playSe(1.0f, 1);
            gOptionsChanged = 1;
        } else if (c == 6) {
            buttonSwap(getter(6), kKeyChoicePairs[sub_ * 2], static_cast<long>(getter(7)), static_cast<long>(kKeyChoicePairs[sub_ * 2 + 1]));
            closeSubWindowB();
            closePairChoice();
            rebuildIcons();
            sound().playSe(1.0f, 1);
            gOptionsChanged = 1;
        }
    }

    // orig FUN_00162580 (work/decompiled/00162580.c)
    void openFaceChoice() {
        faceChoice_ = 1;
        sub_ = 0;
        for (int i = 0; i < 4; ++i) {
            choice_[i] = buttonIcon(357.0f, static_cast<float>(i) * 32.0f + static_cast<float>(cursor_) * 34.0f + 32.0f, 5.1f, 16.0f,
                                    17.0f, kKeyIconsFace[i], 1);
            if (choice_[i]) choice_[i]->color = 0x407f7f7f;
        }
        choiceCursor_ = sprites().create(352.0f, (static_cast<float>(cursor_) * 34.0f + 32.0f) - 4.0f, 6.0f, 24.0f, 24.0f, 0.0f,
                                         1.0f, 32.0f, 32.0f, gradientTextureId(), 0);
        if (choiceCursor_) choiceCursor_->color = 0x407f7f7f;
    }
    // orig FUN_00162920 (work/decompiled/00162920.c)
    void openPairChoice() {
        pairChoice_ = 1;
        sub_ = 0;
        for (int i = 0; i < 2; ++i) {
            const float y = static_cast<float>(i) * 30.0f + 270.0f;
            choice_[i * 2] = buttonIcon(363.0f, y, 5.1f, 18.0f, 15.0f, kKeyIconsPairs[i * 2], 1);
            if (choice_[i * 2]) choice_[i * 2]->color = 0x407f7f7f;
            choice_[i * 2 + 1] = buttonIcon(382.0f, y, 5.1f, 18.0f, 15.0f, kKeyIconsPairs[i * 2 + 1], 1);
            if (choice_[i * 2 + 1]) choice_[i * 2 + 1]->color = 0x407f7f7f;
        }
        choiceCursor_ = sprites().create(362.0f, 265.0f, 6.0f, 42.0f, 24.0f, 0.0f, 1.0f, 32.0f, 32.0f, gradientTextureId(), 2);
        if (choiceCursor_) choiceCursor_->color = 0x407f7f7f;
    }
    // orig FUN_00162ce0 (work/decompiled/00162ce0.c)
    void openShoulderChoice() {
        shoulderChoice_ = 1;
        sub_ = 0;
        const float base = static_cast<float>(cursor_ - 4) * 34.0f;
        const float y0 = base + 170.0f;
        for (int i = 0; i < 4; ++i) {
            choice_[i] = buttonIcon(357.0f, static_cast<float>(i) * 30.0f + y0 + 0.0f, 5.1f, 18.0f, 15.0f, kKeyIconsShoulder[i], 1);
            if (choice_[i]) choice_[i]->color = 0x407f7f7f;
        }
        choiceCursor_ = sprites().create(352.0f, (base + 170.0f) - 5.0f, 6.0f, 24.0f, 24.0f, 0.0f, 1.0f, 32.0f, 32.0f,
                                         gradientTextureId(), 2);
        if (choiceCursor_) choiceCursor_->color = 0x407f7f7f;
    }
    void freeChoice() {
        for (Sprite*& s : choice_) sprites().destroyRef(s);
        sprites().destroyRef(choiceCursor_);
    }
    // orig FUN_00162740 / FUN_00162b00 / FUN_00162eb0
    void closeFaceChoice() { faceChoice_ = 0; freeChoice(); }
    void closePairChoice() { pairChoice_ = 0; freeChoice(); }
    void closeShoulderChoice() { shoulderChoice_ = 0; freeChoice(); }

    int faceChoice_ = 0;             // +0x470
    int pairChoice_ = 0;             // +0x474
    int shoulderChoice_ = 0;         // +0x478
    int wantsNext_ = 0;              // +0x47c
    signed char cursor_ = 0;         // +0x480
    signed char sub_ = 0;            // +0x481
    Sprite* icons_[8] = {};          // +0x484
    Sprite* choice_[4] = {};         // +0x4a4
    Sprite* choiceCursor_ = nullptr; // +0x4b4
    Sprite* arrow_[2] = {};          // +0x4b8 / +0x4bc
    int mapHidden_ = 0;              // +0x4c4 (Game scene: the 2-D map was hidden by open)
    unsigned alpha_ = 0;             // +0x4c0
};
}  // namespace
// orig FUN_00166eb0 (work/decompiled/00166eb0.c): button glyph sprite (face buttons tex 0x28, shoulders tex 0x27)
Sprite* buttonIcon(float x, float y, float z, float w, float h, uint32_t mask, int flag) {
    int tex;
    if (mask == 0x20 || mask == 0x40 || mask == 0x80 || mask == 0x10) tex = 0x28;
    else if (mask == 8 || mask == 2 || mask == 4 || mask == 1) tex = 0x27;
    else return nullptr;
    if (!textures().isLoaded(tex)) textures().load(tex);
    Sprite* s = sprites().alloc(tex, flag);
    if (s == nullptr) return nullptr;
    s->cx = w / 2.0f + x;
    s->cy = h / 2.0f + y;
    s->z = z;
    s->hw = w / 2.0f;
    s->hh = h / 2.0f;
    s->color = 0x888888;
    int idx = (mask == 0x40) ? 1 : 0;
    if (mask == 0x80) idx = 2;
    if (mask == 0x10) idx = 3;
    if (mask == 8) idx = 5;
    if (mask == 4) idx = 4;
    if (mask == 2) idx = 7;
    if (mask == 1) idx = 6;
    const float* uv = kButtonIconUv[idx];
    s->u0 = uv[0] / 128.0f;
    s->v0 = uv[1] / 128.0f;
    s->u1 = uv[2] / 128.0f;
    s->v1 = uv[3] / 128.0f;
    return s;
}
MenuPage* newKeyAssignPage() { return new KeyAssignPage(); }   // FUN_00163ac0 (alloc FUN_00163b80 + ctor FUN_00160ee0)
}  // namespace baroque::game::ui
