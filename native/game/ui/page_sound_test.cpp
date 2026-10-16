// SOUND TEST page (OPTION mode 6; vtable 0x4cf280; ctor FUN_001fe5a0; open FUN_001fe730, close FUN_001feb40,
// is-active 0x200d90 (+0x46c), update 0x1ffc40 (title + FUN_001ffef0 + auto-advance), input FUN_001ffd90,
// wants-next 0x200da0 (+0x470), next page 0x1ffc30 (= FUN_00163b10 OPTION)).
// 27 BGM tracks (0..0x1a) on two pages (0..17, 18..26); track n is unlocked by flag 0x39e + n and plays as
// FUN_00115d70(snd, n, 0, 0). View globals: cRam004e5e20 selected track, cRam004e5e24 page.
#include "game/ui/database.hpp"
#include "game/ui/menu.hpp"
#include "game/fade.hpp"
#include "game/flag_manager.hpp"
#include "game/options.hpp"
#include "game/pad.hpp"
#include "game/sound_stub.hpp"
#include "game/ui/help_bar.hpp"
#include "game/ui/menu_tables.hpp"
#include "game/ui/mouse_menu.hpp"
#include "game/ui/sprite.hpp"
#include "game/ui/strings.hpp"
#include "game/ui/text.hpp"
#include "game/ui/texture_registry.hpp"
#include <cstdio>
namespace baroque::game::ui {
namespace {
signed char gSoundSel = 0;    // cRam004e5e20
signed char gSoundPage = 0;   // cRam004e5e24

bool unlocked(int track) { return flags().test(static_cast<uint32_t>(track + 0x39e)); }
bool bgmIdle() { return sound().bgmEntry() == -1; }   // FUN_00115e80(snd) == -1

class SoundTestPage final : public MenuPage {
public:
    SoundTestPage() = default;   // orig FUN_001fe5a0 (+0x474 = -1, the rest 0)
    const char* debugName() const override { return "SOUND TEST"; }

    // orig FUN_001fe730 (work/decompiled/001fe730.c; stack arguments from the disassembly)
    void open() override {
        makeFrame();
        makeTitle(str(8));
        isOpen = 1;
        wantsNext_ = 0;
        playing_ = -1;
        angle_ = 0;
        alpha_ = 0;
        textures().load(0x43);
        if (options().cursorPos == 1) {
            gSoundSel = 0;
            gSoundPage = 0;
        }
        openSubWindowC();                                  // FUN_00166100
        crossIcon_ = buttonIcon(169.0f, 363.0f, 1.0f, 16.0f, 17.0f, 0x40, 1);   // FUN_00166eb0
        circleIcon_ = buttonIcon(230.0f, 361.0f, 1.0f, 16.0f, 17.0f, 0x20, 1);
        labelPlay_ = sprites().create(188.0f, 363.0f, 5.1f, 30.0f, 14.0f, 0.0f, 68.0f, 30.0f, 82.0f, 0x30, 0);
        labelStop_ = sprites().create(250.0f, 363.0f, 5.1f, 29.0f, 14.0f, 30.0f, 68.0f, 59.0f, 82.0f, 0x30, 0);
        labelTime_ = sprites().create(295.0f, 363.0f, 5.1f, 48.0f, 14.0f, 59.0f, 68.0f, 107.0f, 82.0f, 0x30, 0);
        colon_ = sprites().create(284.0f, 364.0f, 5.1f, 5.0f, 12.0f, 70.0f, 0.0f, 75.0f, 12.0f, 0x23, 0);
        for (int i = 0; i < 2; ++i)
            selDigits_[i] = sprites().create(static_cast<float>(i * 8) + 124.0f, 85.0f, 5.1f, 8.0f, 12.0f, 0, 0, 0, 0, 0x23, 0);
        for (int i = 0; i < 5; ++i)
            timeDigits_[i] = sprites().create(static_cast<float>(i * 8) + 353.0f, 85.0f, 5.1f, 8.0f, 12.0f, 0, 0, 0, 0, 0x23, 0);
        buildList(gSoundPage);                             // FUN_001ff2e0
        setTitle(gSoundSel);                               // FUN_00200610
        makeFrame2(0);
        sound().stopBgm(8);
    }

    // orig FUN_001feb40 (work/decompiled/001feb40.c)
    void close() override {
        freeFrame2();
        helpBar().clear(0);
        freeFrame();
        isOpen = 0;
        helpBar().clear(0);
        closeSubWindowC();                                 // FUN_00166240
        sprites().destroyRef(crossIcon_);
        sprites().destroyRef(circleIcon_);
        sprites().destroyRef(labelPlay_);
        sprites().destroyRef(labelStop_);
        sprites().destroyRef(labelTime_);
        sprites().destroyRef(colon_);
        for (Sprite*& s : selDigits_) sprites().destroyRef(s);
        sprites().destroyRef(title_);
        for (Sprite*& s : timeDigits_) sprites().destroyRef(s);
        buildList(-1);                                     // FUN_001ff2e0(page, -1): free only
        textures().release(0x43);
        sound().playBgm(0x18, 8, 1);
    }

    // orig 0x1ffc40 (disassembled)
    void update() override {
        if (isOpen == 0) return;
        animateTitle();
        if (titleSettled == 0) return;
        draw();                                            // FUN_001ffef0
        if (playing_ == -1 || !bgmIdle()) return;
        if (sound().bgmState() != 6 && sound().bgmState() != 0) return;   // FUN_00115f10 (called twice, as the original)
        do {
            gSoundSel = static_cast<signed char>(gSoundSel + 1);
            if (0x1a < gSoundSel) gSoundSel = 0;
        } while (!unlocked(gSoundSel));
        playing_ = gSoundSel;
        sound().playBgm(gSoundSel, 0, 0);
        setTitle(playing_);
        buildList(gSoundSel < 0x12 ? 0 : 1);
    }

    // orig FUN_001ffd90 (work/decompiled/001ffd90.c)
    void input() override {
        if (isOpen == 0 || alpha_ <= 0x3f) return;
        const PadPort& p = pad().port[0];
        const bool idle = (playing_ == -1);
        if (idle && ((p.repeat & 0x8000) != 0 || (p.repeat & 0x40000) != 0)) left();          // FUN_00200a60
        else if (idle && ((p.repeat & 0x2000) != 0 || (p.repeat & 0x80000) != 0)) right();   // FUN_00200b30
        else if (idle && ((p.repeat & 0x1000) != 0 || (p.repeat & 0x10000) != 0)) up();      // FUN_002009a0
        else if (idle && ((p.repeat & 0x4000) != 0 || (p.repeat & 0x20000) != 0)) down();    // FUN_002008d0
        else if (idle && (p.pressed & 0x40) != 0) play();                                     // FUN_00200c00
        else if (p.pressed & 0x20) back();                                                    // FUN_00200cb0
    }

    // PORT: mouse menu layer. Hover on a track = the cursor move of up/down onto it (title + SE 6), only while nothing
    // plays (the guards of input() / down()); a click is Cross (play). The PLAY / STOP legends are Cross / Circle and
    // the page arrows (kSoundTabPos) are Left (page 1) / Right (page 0).
    void mouseInput() override {
        if (isOpen == 0 || alpha_ <= 0x3f || playing_ != -1 || !bgmIdle()) return;
        const int h = mouse::hovered(this);
        if (h < 0 || h > 0x1a || h == gSoundSel) return;
        gSoundSel = static_cast<signed char>(h);
        setTitle(gSoundSel);
        sound().playSe(1.0f, 6);
    }
    // PORT: the track rows of buildList() (number at x 35, title at x 61 + 240 column, y 128 + 26 row).
    void mouseItems() override {
        if (isOpen == 0 || titleSettled == 0) return;
        int track = gSoundPage * 0x12;
        for (int k = 0; k < 0x12 && track < 0x1b; ++k, ++track) {
            const float x = (k > 8) ? 35.0f + 240.0f : 35.0f;
            mouse::item(this, track, x - 6.0f, static_cast<float>(k % 9) * 26.0f + 124.0f, 232.0f, 26.0f);
        }
        mouse::item(this, 0x40, 165.0f, 359.0f, 57.0f, 24.0f, mouse::kConfirm);   // Cross icon + PLAY
        mouse::item(this, 0x41, 226.0f, 357.0f, 120.0f, 26.0f, mouse::kCancel);   // Circle icon + STOP / CANCEL
        for (int i = 0; i < 4; ++i)
            if (tabs_[i] != nullptr) {
                const float* p = kSoundTabPos[i];
                mouse::item(this, 0x50 + i, p[0] - 4.0f, p[1] - 4.0f, p[2] + 8.0f, p[3] + 8.0f, i < 2 ? platform::PAD_LEFT : platform::PAD_RIGHT);
            }
    }

    bool wantsNext() const override { return wantsNext_ != 0; }   // 0x200da0
    MenuPage* nextPage() override { return newOptionPage(); }       // 0x1ffc30

private:
    // orig FUN_001ff2e0 (work/decompiled/001ff2e0.c): rebuild the track list of `page` (-1 = only free)
    void buildList(int page) {
        for (Sprite*& s : titles_) sprites().destroyRef(s);
        for (auto& d : digits_) for (Sprite*& s : d) sprites().destroyRef(s);
        for (Sprite*& s : tabs_) sprites().destroyRef(s);
        if (page == -1) return;
        gSoundPage = static_cast<signed char>(page);
        const int lang = language();
        int track = gSoundPage * 0x12;
        if (track < 0x1b) {                                // track numbers
            int xo = 0;
            int row = 0;
            for (int k = 0; k < 0x12; ++k) {
                const float y = static_cast<float>(row * 26) + 129.0f;
                digits_[k][0] = sprites().create(static_cast<float>(xo) + 35.0f, y, 5.1f, 8.0f, 12.0f, 0, 0, 0, 0, 0x23, 0);
                digits_[k][1] = sprites().create(static_cast<float>(xo) + 43.0f, y, 5.1f, 8.0f, 12.0f, 0, 0, 0, 0, 0x23, 0);
                ++row;
                if (track == 8) {
                    xo += 0xf0;
                    row = 0;
                }
                ++track;
                if (0x1a < track) break;
            }
        }
        track = gSoundPage * 0x12;
        for (int k = 0; k < 0x12 && track < 0x1b; ++k, ++track) {
            if (digits_[k][0] == nullptr) continue;
            char buf[4] = {' ', ' ', 0, 0};                // DAT_00469688 "  "
            std::snprintf(buf, sizeof buf, "%2d", track + 1);   // 0x469690
            setDigitGlyph(digits_[k][0], buf[0]);
            setDigitGlyph(digits_[k][1], buf[1]);
        }
        track = gSoundPage * 0x12;
        if (track < 0x1b) {                                // track titles (tex 0x43)
            int xo = 0;
            int row = 0;
            for (int k = 0; k < 0x12 && track < 0x1b; ++k, ++track) {
                const float x = static_cast<float>(xo) + 61.0f;
                const float y = static_cast<float>(row * 0x1a) + 128.0f;
                if (!unlocked(track)) {
                    const float* uv = kSoundLockedUv[lang];
                    titles_[k] = sprites().create(x, y, 5.1f, 40.0f, 18.0f, uv[0], uv[1], uv[2], uv[3], 0x43, 0);
                } else {
                    const float* uv = kSoundTitleUv[track * 2 + lang];
                    titles_[k] = sprites().create(x, y, 5.1f, uv[2] - uv[0], 18.0f, uv[0], uv[1], uv[2], uv[3], 0x43, 0);
                }
                ++row;
                if (k == 8) {
                    xo += 0xf0;
                    row = 0;
                }
            }
        }
        for (int i = 0; i < 4; ++i) {                      // page arrows
            if ((gSoundPage == 0 && (i == 2 || i == 3)) || (gSoundPage == 1 && i < 2)) {
                const float* p = kSoundTabPos[i];
                const float* uv = kSoundTabUv[i];
                tabs_[i] = sprites().create(p[0], p[1], 1.0f, p[2], p[3], uv[0], uv[1], uv[2], uv[3], 0x28, 1);
            }
        }
    }
    // orig FUN_00200610 (work/decompiled/00200610.c): the big title of the selected / playing track
    void setTitle(int track) {
        sprites().destroyRef(title_);
        const int lang = language();
        if (!unlocked(track)) {
            const float* uv = kSoundLockedUv[lang];
            title_ = sprites().create(151.0f, 84.0f, 5.1f, 40.0f, 18.0f, uv[0], uv[1], uv[2], uv[3], 0x43, 0);
        } else {
            const float* uv = kSoundTitleUv[track * 2 + lang];
            title_ = sprites().create(151.0f, 84.0f, 5.1f, uv[2] - uv[0], 18.0f, uv[0], uv[1], uv[2], uv[3], 0x43, 0);
        }
    }

    // orig FUN_002008d0 (work/decompiled/002008d0.c)
    void down() {
        if (isOpen == 0 || !bgmIdle()) return;
        gSoundSel = static_cast<signed char>(gSoundSel + 1);
        if (gSoundPage == 0) {
            if (0x11 < gSoundSel) gSoundSel = 0;
        } else if (gSoundPage == 1 && 0x1a < gSoundSel) {
            gSoundSel = 0x12;
        }
        setTitle(gSoundSel);
        sound().playSe(1.0f, 6);
    }
    // orig FUN_002009a0 (work/decompiled/002009a0.c)
    void up() {
        if (isOpen == 0 || !bgmIdle()) return;
        gSoundSel = static_cast<signed char>(gSoundSel - 1);
        if (gSoundPage == 0) {
            if (gSoundSel < 0) gSoundSel = 0x11;
        } else if (gSoundPage == 1 && gSoundSel < 0x12) {
            gSoundSel = 0x1a;
        }
        setTitle(gSoundSel);
        sound().playSe(1.0f, 6);
    }
    // orig FUN_00200a60 (work/decompiled/00200a60.c)
    void left() {
        if (isOpen == 0 || !bgmIdle()) return;
        if (gSoundPage == 1) {
            gSoundSel = 0;
            buildList(0);
            setTitle(gSoundSel);
            sound().playSe(1.0f, 6);
        } else if (8 < gSoundSel) {
            gSoundSel = static_cast<signed char>(gSoundSel - 9);
            setTitle(gSoundSel);
            sound().playSe(1.0f, 6);
        }
    }
    // orig FUN_00200b30 (work/decompiled/00200b30.c)
    void right() {
        if (isOpen == 0 || !bgmIdle() || gSoundPage == 1) return;
        if (gSoundSel < 9) {
            gSoundSel = static_cast<signed char>(gSoundSel + 9);
            setTitle(gSoundSel);
            sound().playSe(1.0f, 6);
        } else {
            gSoundSel = 0x12;
            buildList(1);
            setTitle(gSoundSel);
            sound().playSe(1.0f, 6);
        }
    }
    // orig FUN_00200c00 (work/decompiled/00200c00.c)
    void play() {
        if (isOpen == 0 || !bgmIdle() || playing_ != -1) return;
        if (!unlocked(gSoundSel)) {
            sound().playSe(1.0f, 3);
            return;
        }
        playing_ = gSoundSel;
        sound().playBgm(gSoundSel, 0, 0);
        setTitle(playing_);
    }
    // orig FUN_00200cb0 (work/decompiled/00200cb0.c)
    void back() {
        if (isOpen == 0) return;
        if (playing_ == -1 && bgmIdle()) {
            wantsNext_ = 1;
            sound().playSe(1.0f, 8);
            return;
        }
        if (playing_ == -1) {
            sound().playSe(1.0f, 3);
        } else {
            sound().stopBgm(8);
            playing_ = -1;
            sound().playSe(1.0f, 2);
        }
    }

    // orig FUN_001ffef0 (work/decompiled/001ffef0.c)
    void draw() {
        angle_ += 8;
        if (0x168 < angle_) angle_ = -360;
        if (alpha_ < 0x40 && fade().finished()) {
            alpha_ += 6;
            if (0x40 < alpha_) alpha_ = 0x40;
        }
        if (!helpBar().isSet(0) && alpha_ == 0x40) {
            helpBar().set(32.0f, 403.0f, 420.0f, 40.0f, 0, str(0x168));
            helpBar().setSpeed(0, 4);
        }
        const uint32_t a = alpha_ << 24;
        const uint32_t c = a + 0x7f7f7f;
        colorSubWindowC(a);                                // FUN_00166420
        for (Sprite* s : {crossIcon_, circleIcon_, labelPlay_, labelStop_, labelTime_, colon_})
            if (s != nullptr) s->color = c;
        char buf[8] = {};
        std::snprintf(buf, sizeof buf, "%2d", gSoundSel + 1);   // 0x469690
        for (int i = 0; i < 2; ++i) setDigitGlyph(selDigits_[i], buf[i]);
        const uint32_t stateColor = (playing_ == -1) ? c : 0x407f7f7fu;
        for (Sprite* s : selDigits_)
            if (s != nullptr) s->color = stateColor;
        char tb[8] = {};
        if (playing_ == -1) {
            std::snprintf(tb, sizeof tb, "--:--");         // 0x4696a8
        } else {
            const int t = sound().bgmFrames();             // FUN_00115ec0: elapsed BGM frames
            const int minutes = (t % 0x1a5e0) / 0x708;
            std::snprintf(tb, sizeof tb, "%02d:%02d", minutes, (t + (minutes + (t / 0x1a5e0) * 0x3c) * -0x708) / 0x1e);   // 0x469698
        }
        for (int i = 0; i < 5; ++i) setDigitGlyph(timeDigits_[i], tb[i]);
        for (Sprite* s : timeDigits_)
            if (s != nullptr) s->color = stateColor;
        if (title_ != nullptr) title_->color = stateColor;
        int track = gSoundPage * 0x12;
        for (int k = 0; k < 0x12 && track < 0x1b; ++k, ++track) {
            uint32_t col;
            if (track == gSoundSel) col = a + 0x7f;
            else if (playing_ == -1 && bgmIdle()) col = c;
            else col = a + 0x2f2f2f;
            if (digits_[k][0] != nullptr) {
                digits_[k][0]->color = col;
                if (digits_[k][1] != nullptr) digits_[k][1]->color = col;
            }
            if (titles_[k] != nullptr) titles_[k]->color = col;
        }
        for (Sprite* s : tabs_)
            if (s != nullptr) s->color = c;
        text().layerFlag = 1;
        text().color = 0x40bbbbbb;                         // DAT_0044c198
        if (!helpBar().isSet(0) && 0x3f < alpha_) {
            helpBar().set(32.0f, 403.0f, 420.0f, 40.0f, 0, str(0x168));
            helpBar().setSpeed(0, 4);
        }
    }

    int wantsNext_ = 0;              // +0x470
    signed char playing_ = -1;       // +0x474
    Sprite* selDigits_[2] = {};      // +0x478
    Sprite* title_ = nullptr;        // +0x480
    Sprite* timeDigits_[5] = {};     // +0x484
    Sprite* digits_[0x12][2] = {};   // +0x498
    Sprite* titles_[0x12] = {};      // +0x528
    Sprite* crossIcon_ = nullptr;    // +0x570
    Sprite* labelPlay_ = nullptr;    // +0x574
    Sprite* circleIcon_ = nullptr;   // +0x578
    Sprite* labelStop_ = nullptr;    // +0x57c
    Sprite* labelTime_ = nullptr;    // +0x580
    Sprite* colon_ = nullptr;        // +0x584
    Sprite* tabs_[4] = {};           // +0x588
    int angle_ = 0;                  // +0x598
    unsigned alpha_ = 0;             // +0x59c
};
}  // namespace
MenuPage* newSoundTestPage() { return new SoundTestPage(); }   // FUN_00169060 (ctor FUN_001fe5a0)
}  // namespace baroque::game::ui
