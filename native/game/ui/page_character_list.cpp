// CHARACTER LIST page (OPTION mode 5; vtable 0x4cf250; ctor FUN_001fbce0; dtor FUN_001fbdf0; open FUN_001fbe60,
// close FUN_001fc550, is-active 0x1fe580 (+0x46c), update 0x1fd010 (title, FUN_001fd1d0 when settled, then the talk
// per-tick FUN_001a2110), input FUN_001fd070, wants-next 0x1fe590 (+0x470), next page 0x1fd000 (= FUN_00163b10 OPTION)).
// 50 characters (global iRam0053e9e0, kept across visits unless CURSOR POSITION is "Default Settings"): character 0 is
// always shown, every other one once one of its picture flags (0x49cad0) is set. Each shows its name str(0x104 + n),
// a picture (one 256 x 256, two stacked 256 x 256 or, for n = 0x11, three pieces; up to four variants cycled with R3)
// and a list of kChrVoiceCount[n] voices (8 visible rows, scrolled); voice k is unlocked by flag
// kChrFirstVoice[n] + k (0xa5c -> 0xa5e, 0xa66 -> 0xa68) and Cross plays it through FUN_001aa1b0 (talk by voice
// number). Characters without voices show their description str(0x136 + n) in the help window.
// Sprite stack arguments (v1, texture size) of every FUN_00166820 call were read from the disassembly
// (0x1fbee0..0x1fc514, 0x1fdb0c..0x1fde14).
#include "game/ui/database.hpp"
#include "game/ui/menu.hpp"
#include "game/fade.hpp"
#include "game/flag_manager.hpp"
#include "game/options.hpp"
#include "game/pad.hpp"
#include "game/sound_stub.hpp"
#include "game/talk/talk_manager.hpp"
#include "game/ui/gradient_texture.hpp"
#include "game/ui/help_bar.hpp"
#include "game/ui/menu_tables.hpp"
#include "game/ui/mouse_menu.hpp"
#include "game/ui/sprite.hpp"
#include "game/ui/strings.hpp"
#include "game/ui/text.hpp"
#include "game/ui/texture_registry.hpp"
#include <algorithm>
#include <cstdio>
namespace baroque::game::ui {
namespace {

int gChr = 0;                                   // iRam0053e9e0 (character index 0..0x31)
constexpr uint32_t kTextRgb = 0xbbbbbb;         // DAT_0044c190
constexpr float kScaleW = 1.16f;                // DAT_0044c180
constexpr float kScaleH = 1.22f;                // DAT_0044c188
constexpr uint32_t kRgb = 0x7f7f7f;

bool talkOn() { return talkManager().active != 0; }   // iRam0053c6e8 (talk manager +8)

// the voice-number fix-up shared by FUN_001fd1d0 and FUN_001fe460
int fixVoice(int v) {
    if (v == 0xa5c) v = 0xa5e;
    else if (v == 0xa66) v = 0xa68;
    return v;
}

class CharacterListPage final : public MenuPage {
public:
    CharacterListPage() = default;   // orig FUN_001fbce0 (work/decompiled/001fbce0.c): every field from +0x46c on = 0
    const char* debugName() const override { return "CHARACTER LIST"; }

    // orig FUN_001fbe60 (work/decompiled/001fbe60.c + disasm 0x1fbe60..0x1fc54c)
    void open() override {
        portWideLeft = 0;   // PORT widescreen: the list column (kChrFramePos) joins the left pipe column, keep both
        makeFrame();                                            // FUN_001642d0
        makeTitle(str(7));                                      // FUN_00164a30(FUN_00114da0(7))
        isOpen = 1;
        wantsNext_ = 0;
        angle_ = 0;
        alpha_ = 0;
        alpha2_ = 0;
        if (options().cursorPos == 1) gChr = 0;                 // iRam0051af34 == 1
        select(0);                                              // FUN_001fd8c0(this, 0)
        cursor_ = sprites().create(31.0f, 123.0f, 0.0f, 112.0f, 22.0f, 0.3f, 1.0f, 31.7f, 32.0f, gradientTextureId(), 1);
        for (int i = 0; i < 0x16; ++i) {
            const float* p = kChrFramePos[i];
            const float* uv = kChrFrameUv[i];
            frameChr_[i] = sprites().create(p[0] - 1.0f, p[1] - 1.0f, 0.0f, p[2], p[3], uv[0], uv[1], uv[2], uv[3], 0x31, 1);
        }
        nameBox_ = sprites().create(29.0f, 73.0f, 0.0f, 207.0f, 27.0f, 50.0f, 42.0f, 58.0f, 50.0f, 0x27, 1);
        arrow_[0] = sprites().create(29.0f, 80.0f, 0.9f, 10.0f, 13.0f, 25.0f, 17.0f, 35.0f, 30.0f, 0x28, 1);
        arrow_[1] = sprites().create(225.0f, 80.0f, 0.9f, 10.0f, 13.0f, 35.0f, 17.0f, 45.0f, 30.0f, 0x28, 1);
        float dy = 0.0f;                                        // $f20
        for (int i = 0; i < 8; ++i) {
            const float y = (dy + 125.0f) - 1.0f;
            rowPiece_[0][i] = sprites().create(126.0f, y, 0.9f, 11.0f, 10.0f, 117.0f, 61.0f, 128.0f, 71.0f, 0x31, 1);
            rowPiece_[1][i] = sprites().create(108.0f, y, 0.9f, 18.0f, 19.0f, 38.0f, 109.0f, 56.0f, 128.0f, 0x31, 1);
            rowPiece_[2][i] = sprites().create(33.0f, y, 0.9f, 11.0f, 19.0f, 27.0f, 109.0f, 38.0f, 128.0f, 0x31, 1);
            rowPiece_[3][i] = sprites().create(25.0f, (dy + 120.0f) - 1.0f, 0.9f, 8.0f, 18.0f, 20.0f, 84.0f, 28.0f, 102.0f,
                                               0x31, 1);
            rowPiece_[4][i] = sprites().create(44.0f, y, 0.9f, 64.0f, 5.0f, 24.0f, 46.0f, 40.0f, 51.0f, 0x31, 1);
            const float y2 = (dy + 130.0f) - 1.0f;
            rowPiece_[5][i] = sprites().create(44.0f, y2, 0.9f, 50.0f, 14.0f, 57.0f, 100.0f, 107.0f, 114.0f, 0x31, 1);
            for (int j = 0; j < 2; ++j)
                digit_[i][j] = sprites().create((static_cast<float>(j) * 7.0f + 95.0f) - 1.0f, y2, 0.9f, 7.0f, 14.0f, 0.0f,
                                                0.0f, 0.0f, 0.0f, 0x31, 1);
            dy += 21.0f;
        }
        scrollUp_ = sprites().create(74.0f, 109.0f, 0.9f, 12.0f, 13.0f, 13.0f, 17.0f, 25.0f, 30.0f, 0x28, 1);
        scrollDown_ = sprites().create(74.0f, 293.0f, 0.9f, 13.0f, 13.0f, 0.0f, 17.0f, 13.0f, 30.0f, 0x28, 1);
        makeFrame2(5);                                          // FUN_001664b0(this, 5)
    }

    // orig FUN_001fc550 (work/decompiled/001fc550.c)
    void close() override {
        freeFrame2();                                           // FUN_00166640
        helpBar().clear(0);                                     // FUN_0015c3f0(0x5180f0, 0)
        freeFrame();                                            // FUN_00164460
        isOpen = 0;
        sprites().destroyRef(cursor_);
        for (Sprite*& s : frameChr_) sprites().destroyRef(s);
        sprites().destroyRef(nameBox_);
        for (Sprite*& s : arrow_) sprites().destroyRef(s);
        for (int i = 0; i < 8; ++i) {
            for (auto& piece : rowPiece_) sprites().destroyRef(piece[i]);
            for (Sprite*& s : digit_[i]) sprites().destroyRef(s);
        }
        sprites().destroyRef(scrollUp_);
        sprites().destroyRef(scrollDown_);
        releasePicture();                                       // FUN_001fde30
    }

    // orig 0x1fd010 (disassembled; not exported by Ghidra)
    void update() override {
        if (isOpen == 0) return;
        animateTitle();                                         // FUN_00164dd0 (+ FUN_00164f30, empty)
        if (titleSettled != 0) draw();                          // FUN_001fd1d0
        if (talkWorld() != nullptr) talkManager().tick();       // FUN_001a2110(0x53c6e0)
    }

    // orig FUN_001fd070 (work/decompiled/001fd070.c)
    void input() override {
        if (isOpen == 0 || alpha_ <= 0x3f || alpha2_ <= 0x3f) return;
        const PadPort& p = pad().port[0];
        const uint32_t press = p.pressed;                       // uRam004e6288
        const uint32_t rep = p.repeat;                          // uRam004e6290
        if ((press & 0x8000) != 0 || (press & 0x40000) != 0) prevCharacter();       // FUN_001fe380
        else if ((press & 0x2000) != 0 || (press & 0x80000) != 0) nextCharacter();  // FUN_001fe3f0
        else if ((rep & 0x1000) != 0 || (rep & 0x10000) != 0) up();                 // FUN_001fe2f0
        else if ((rep & 0x4000) != 0 || (rep & 0x20000) != 0) down();               // FUN_001fe260
        else if ((press & 0x40) != 0) decide();                                      // FUN_001fe460
        else if ((press & 0x20) != 0) cancel();                                      // FUN_001fe520
        else if ((press & 0x400) != 0) nextPicture();                                // FUN_001fe0b0
    }

    // PORT: mouse menu layer. Hover on a voice row = the cursor move of up/down onto it (SE 6, as FUN_001fe2f0 /
    // FUN_001fe260); the name arrows are Left/Right (previous / next character), the list arrows Up/Down and a click on
    // the picture is R3 (next picture variant), all through input().
    void mouseInput() override {
        if (isOpen == 0 || alpha_ <= 0x3f || alpha2_ <= 0x3f) return;   // the guard of input()
        const int h = mouse::hovered(this);
        if (h < 0 || h > 7 || h == row_ || count_ <= h + scroll_) return;
        row_ = h;
        sound().playSe(1.0f, 6);
    }
    // PORT: rectangles from open(): the cursor bar (x 31, y 123 + 21 row, 112 x 22), the arrows 0x4f4 / 0x4f8 /
    // 0x5fc / 0x600 and the picture sprites 0x604.. (registered first so the list wins where they overlap).
    void mouseItems() override {
        if (isOpen == 0 || titleSettled == 0) return;
        float x0 = 512.0f, y0 = 448.0f, x1 = 0.0f, y1 = 0.0f;
        for (Sprite* s : picture_) {
            if (s == nullptr) continue;
            x0 = std::min(x0, s->cx - s->hw);
            y0 = std::min(y0, s->cy - s->hh);
            x1 = std::max(x1, s->cx + s->hw);
            y1 = std::max(y1, s->cy + s->hh);
        }
        x0 = std::max(x0, 0.0f);
        y0 = std::max(y0, 0.0f);
        x1 = std::min(x1, 512.0f);
        y1 = std::min(y1, 448.0f);
        if (x0 < x1 && y0 < y1) mouse::item(this, 0x20, x0, y0, x1 - x0, y1 - y0, platform::PAD_R3);
        for (int i = 0; i < 8; ++i)
            if (i + scroll_ < count_) mouse::item(this, i, 31.0f, static_cast<float>(i) * 21.0f + 123.0f, 112.0f, 22.0f);
        mouse::item(this, 0x10, 25.0f, 76.0f, 18.0f, 21.0f, platform::PAD_LEFT);
        mouse::item(this, 0x11, 221.0f, 76.0f, 18.0f, 21.0f, platform::PAD_RIGHT);
        if (0 < scroll_) mouse::item(this, 0x12, 70.0f, 105.0f, 20.0f, 21.0f, platform::PAD_UP);
        if (scroll_ + 8 < count_) mouse::item(this, 0x13, 70.0f, 289.0f, 21.0f, 21.0f, platform::PAD_DOWN);
    }

    bool wantsNext() const override { return wantsNext_ != 0; }   // 0x1fe590
    MenuPage* nextPage() override { return newOptionPage(); }       // 0x1fd000: j FUN_00163b10

private:
    // orig FUN_001fd8c0 (work/decompiled/001fd8c0.c + disasm 0x1fd8c0..0x1fda78): step to the previous / next shown
    // character (step 0 = stay unless the current one is hidden) and pick its first unlocked picture.
    void select(int step) {
        releasePicture();                                       // thunk 0x1fda90 -> FUN_001fde30
        if (step == 0 && gChr != 0 && kChrPictureFlag[gChr][0] != -1 &&
            !flags().test(static_cast<uint32_t>(kChrPictureFlag[gChr][0])))
            step = 1;
        bool found = false;
        do {
            gChr += step;
            if (gChr < 0x32) {
                if (gChr < 0) gChr = 0x31;
            } else {
                gChr = 0;
            }
            if (gChr == 0) {
                variant_ = 0;
                break;
            }
            // UNVERIFIED: with step 0 on a character whose four flags are all clear this loops forever, as the original
            // does; it cannot happen because step becomes 1 above unless flag [0] is set (every character n != 0 has a
            // flag [0], kChrPictureFlag).
            for (int k = 0; k < 4; ++k) {
                const int f = kChrPictureFlag[gChr][k];
                if (f != -1 && flags().test(static_cast<uint32_t>(f))) {
                    variant_ = k;
                    found = true;
                    break;
                }
            }
        } while (!found);
        makePicture();                                          // thunk 0x1fda80 -> FUN_001fdaa0
        helpBar().clear(0);
        count_ = kChrVoiceCount[gChr];                          // 0x49c600
        row_ = 0;
        scroll_ = 0;
        alpha2_ = 0;
    }

    // orig FUN_001fdaa0 (work/decompiled/001fdaa0.c + disasm 0x1fdaa0..0x1fde24)
    void makePicture() {
        const int tex = kChrPictureTex[gChr][variant_];
        if (gChr == 0x10 || gChr == 0x13 || 0x16 < gChr) {
            textures().load(tex);                               // FUN_00106090(0x4e6a68, tex, 1)
            picture_[0] = sprites().create(212.0f, 93.0f, 0.5f, 256.0f, 256.0f, 0.0f, 0.0f, 256.0f, 256.0f, tex, 1);
        } else if (gChr == 0x11) {
            textures().loadRange(tex, tex + 2);                 // FUN_00106120 (table 0x49c8b0 = 0x49c7a0 + 0x11 * 0x10)
            picture_[0] = sprites().create(128.0f, 0.0f, 0.5f, 128.0f, 256.0f, 0.0f, 0.0f, 128.0f, 256.0f, tex, 1);
            picture_[1] = sprites().create(256.0f, -1.0f, 0.5f, 256.0f, 256.0f, 0.0f, 0.0f, 256.0f, 256.0f, tex + 1, 1);
            picture_[2] = sprites().create(256.0f, 255.0f, 0.5f, 256.0f, 256.0f, 0.0f, 0.0f, 256.0f, 256.0f, tex + 2, 1);
        } else {
            textures().loadRange(tex, tex + 1);
            picture_[0] = sprites().create(211.0f, -1.0f, 0.5f, 256.0f, 256.0f, 0.0f, 0.0f, 256.0f, 256.0f, tex, 1);
            picture_[1] = sprites().create(211.0f, 255.0f, 0.5f, 256.0f, 256.0f, 0.0f, 0.0f, 256.0f, 256.0f, tex + 1, 1);
        }
    }

    // orig FUN_001fde30 (work/decompiled/001fde30.c + disasm 0x1fdf40..0x1fdff8): free the three picture sprites and
    // release textures tex .. tex + 2 of the current picture that are loaded and inside the picture range 0x8f..0x11a
    void releasePicture() {
        for (int k = 0; k < 3; ++k) {
            sprites().destroyRef(picture_[k]);
            const int id = k + kChrPictureTex[gChr][variant_];
            // id == -1 counts as "loaded" in the original and then fails the range test
            if (0x8e < id && id < 0x11b && textures().isLoaded(id)) textures().release(id);   // FUN_00105f00
        }
    }

    // orig FUN_001fe0b0 (work/decompiled/001fe0b0.c + disasm): next picture variant (R3), wrapping over the four slots;
    // a slot is usable when it has a texture and its flag is -1 or set
    void nextPicture() {
        int v = variant_ + 1;
        if (3 < v) v = 0;
        for (;;) {
            if (kChrPictureTex[gChr][v] != -1) {
                const int f = kChrPictureFlag[gChr][v];
                if (f == -1 || flags().test(static_cast<uint32_t>(f))) break;
            }
            ++v;
            if (3 < v) v = 0;
        }
        if (v != variant_) {
            releasePicture();                                   // FUN_001fde30 (old variant)
            variant_ = v;
            makePicture();                                      // FUN_001fdaa0
        }
    }

    // orig FUN_001fe000 (work/decompiled/001fe000.c): the character description in the help window
    void describe() {
        helpBar().clear(0);
        helpBar().set(kChrHelpX[language()], kChrHelpY[language()], 360.0f, 120.0f, 0, "%s\n", str(gChr + 0x136));   // 0x469670
        helpBar().setSpeed(0, 4);                               // uRam005181bc = 4
    }

    // orig FUN_001fe190 (work/decompiled/001fe190.c): digit glyph of the voice number ('\0' leaves the sprite as is)
    static void setDigit(Sprite* s, char c) {
        if (s == nullptr) return;
        if (c == ' ' || c == '\0') {
            if (c == ' ') {
                s->u0 = 0.5078125f;                             // 0x3f020000
                s->v0 = 0.671875f;                              // 0x3f2c0000
                s->u1 = 0.5625f;                                // 0x3f100000
                s->v1 = 0.78125f;                               // 0x3f480000
            }
            return;
        }
        // UNVERIFIED: only '0'..'9' reach here (sprintf "%02d" of 1..28); other characters would index outside 0x49c2a0
        const float* uv = kChrDigitUv[c - '0'];
        s->u0 = uv[0] / 128.0f;
        s->v0 = uv[1] / 128.0f;
        s->u1 = uv[2] / 128.0f;
        s->v1 = uv[3] / 128.0f;
    }

    // orig FUN_001fe260 (work/decompiled/001fe260.c)
    void down() {
        if (isOpen == 0) return;
        if (row_ + scroll_ + 1 < count_) {
            if (row_ + 1 < 8) ++row_;
            else ++scroll_;
            sound().playSe(1.0f, 6);
        }
    }
    // orig FUN_001fe2f0 (work/decompiled/001fe2f0.c)
    void up() {
        if (isOpen == 0) return;
        if (0 < row_ + scroll_) {
            if (row_ < 1) {
                if (0 < scroll_) {
                    --scroll_;
                    sound().playSe(1.0f, 6);
                }
            } else {
                --row_;
                sound().playSe(1.0f, 6);
            }
        }
    }
    // orig FUN_001fe380 / FUN_001fe3f0 (work/decompiled/001fe380.c, 001fe3f0.c)
    void prevCharacter() {
        if (isOpen == 0) return;
        if (talkOn()) talkManager().end();                      // FUN_001a6710(0x53c6e0)
        select(-1);
        sound().playSe(1.0f, 6);
    }
    void nextCharacter() {
        if (isOpen == 0) return;
        if (talkOn()) talkManager().end();
        select(1);
        sound().playSe(1.0f, 6);
    }
    // orig FUN_001fe460 (work/decompiled/001fe460.c): play the voice under the cursor (its number is its flag)
    void decide() {
        if (isOpen == 0) return;
        const int v = fixVoice(scroll_ + row_ + kChrFirstVoice[gChr]);   // 0x49c6d0
        if (!flags().test(static_cast<uint32_t>(v))) {
            sound().playSe(1.0f, 3);
        } else {
            helpBar().clear(0);
            // UNVERIFIED: guarded like the VOICE LIST page (the talk manager needs its world binding)
            if (talkWorld() != nullptr) talkByVoiceNumber(v);   // FUN_001aa1b0
        }
    }
    // orig FUN_001fe520 (work/decompiled/001fe520.c)
    void cancel() {
        if (isOpen == 0) return;
        if (!talkOn()) wantsNext_ = 1;
        else talkManager().end();
        sound().playSe(1.0f, 8);
    }

    // orig FUN_001fd1d0 (work/decompiled/001fd1d0.c + disasm 0x1fd1d0..0x1fd8bc)
    void draw() {
        angle_ += 8;
        if (0x168 < angle_) angle_ = -360;
        // FUN_00206940((float)angle) -- result unused
        if (alpha_ < 0x40 && fade().finished()) {               // iRam004e7898 == 0
            alpha_ += 6;
            if (0x40 < alpha_) alpha_ = 0x40;
        }
        if (alpha2_ < 0x40 && fade().finished()) {
            alpha2_ += 6;
            if (0x40 < alpha2_) alpha2_ = 0x40;
        }
        if (!helpBar().isSet(0) && alpha2_ == 0x40 && !talkOn()) {
            if (count_ == 0) {
                describe();                                     // FUN_001fe000
            } else {
                helpBar().set(kChrHelpX[language()], kChrHelpY[language()], 360.0f, 120.0f, 0, "%s\n", str(0x103));
                helpBar().setSpeed(0, 4);
            }
            sound().playBgm(0x19, 4, 1);                        // FUN_00115d70(0x4e9790, 0x19, 4, 1)
            flags().set(0x3b7);                                 // FUN_00178890(0x53c3b0, 0x3b7)
        }
        // the character name, centred over 188 px from x 39
        TextRenderer& t = text();
        t.halfW = kScaleW * 8.0f;
        t.halfH = kScaleH * 9.0f;
        t.color = kTextRgb + (alpha_ << 24);
        const float w = t.measure("%s", str(gChr + 0x104));     // FUN_00113cf0(0x4e7e90, name)
        t.y = 78.0f;
        t.z = 1.9f;                                             // 0x3ff33333
        t.x = (39.0f + 0.5f * (188.0f - w)) - 1.0f;
        t.draw("%s", str(gChr + 0x104));                        // FUN_00112e80(0x4e7e90, name)
        const uint32_t c = (alpha_ << 24) + kRgb;
        if (cursor_ != nullptr) {
            cursor_->color = count_ < 1 ? 0u : c;
            cursor_->cy = 11.0f + (123.0f + 21.0f * static_cast<float>(row_));
        }
        for (Sprite* s : frameChr_)
            if (s != nullptr) s->color = c;
        if (nameBox_ != nullptr) nameBox_->color = c;
        for (Sprite* s : arrow_)
            if (s != nullptr) s->color = c;
        for (int i = 0; i < 8; ++i) {
            uint32_t a = alpha2_;
            if (count_ <= i + scroll_ || count_ == 0) a = 0;
            const uint32_t rc = (a << 24) + kRgb;
            for (auto& piece : rowPiece_)
                if (piece[i] != nullptr) piece[i]->color = rc;
            const int v = fixVoice(scroll_ + i + kChrFirstVoice[gChr]);
            const bool on = flags().test(static_cast<uint32_t>(v));
            if (Sprite* s = rowPiece_[5][i]; s != nullptr) {
                if (!on) {
                    s->u0 = 0.71875f;                           // 0x3f380000
                    s->v0 = 0.5625f;                            // 0x3f100000
                    s->u1 = 1.0f;
                    s->v1 = 0.671875f;                          // 0x3f2c0000
                } else {
                    s->u0 = 0.4453125f;                         // 0x3ee40000
                    s->v0 = 0.78125f;                           // 0x3f480000
                    s->u1 = 0.8359375f;                         // 0x3f560000
                    s->v1 = 0.890625f;                          // 0x3f640000
                }
            }
            char buf[4] = {};                                   // sp+0x9c, 3 bytes cleared
            if (!on) std::snprintf(buf, sizeof buf, "  ");      // 0x469680
            else std::snprintf(buf, sizeof buf, "%02d", i + scroll_ + 1);   // 0x469678
            for (int j = 0; j < 2; ++j) {
                if (digit_[i][j] != nullptr) digit_[i][j]->color = rc;
                setDigit(digit_[i][j], buf[j]);                 // FUN_001fe190
            }
        }
        const uint32_t c2 = (alpha2_ << 24) + kRgb;
        if (scrollUp_ != nullptr) scrollUp_->color = scroll_ < 1 ? 0u : c2;
        if (scrollDown_ != nullptr) scrollDown_->color = scroll_ + 8 < count_ ? c2 : 0u;
        for (Sprite* s : picture_)
            if (s != nullptr) s->color = c2;
        t.layerFlag = 1;
        t.color = 0x40bbbbbb;                                   // DAT_0044c198
    }

    int wantsNext_ = 0;              // +0x470
    int angle_ = 0;                  // +0x474
    uint32_t alpha_ = 0;             // +0x478 frame / name fade-in
    uint32_t alpha2_ = 0;            // +0x47c list / picture fade-in (restarted per character)
    int variant_ = 0;                // +0x480 picture slot 0..3
    int row_ = 0;                    // +0x484 cursor row 0..7
    int scroll_ = 0;                 // +0x488 first voice shown
    int count_ = 0;                  // +0x48c voices of the character
    Sprite* cursor_ = nullptr;       // +0x494
    Sprite* frameChr_[22] = {};      // +0x498
    Sprite* nameBox_ = nullptr;      // +0x4f0
    Sprite* arrow_[2] = {};          // +0x4f4 previous / next character
    Sprite* rowPiece_[6][8] = {};    // +0x4fc, +0x51c, +0x53c, +0x55c, +0x57c, +0x59c (voice label, locked / unlocked uv)
    Sprite* digit_[8][2] = {};       // +0x5bc
    Sprite* scrollUp_ = nullptr;     // +0x5fc
    Sprite* scrollDown_ = nullptr;   // +0x600
    Sprite* picture_[3] = {};        // +0x604
};
}  // namespace
MenuPage* newCharacterListPage() { return new CharacterListPage(); }   // FUN_001690b0 (ctor FUN_001fbce0)
}  // namespace baroque::game::ui
