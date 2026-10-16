// NAME ENTRY page (menu mode 8; vtable 0x4cdd50; ctor FUN_00169be0; open FUN_00169cd0, close FUN_00169f20,
// is-active 0x16b890 (+0x46c), update FUN_0016a210, input FUN_0016ae10, wants-next 0x16b8a0 (+0x470),
// next page 0x16b8b0 (returns 0)). SCENES.md 7.
// Character grid: PTR_DAT_0044ca70 (kNameEntryChars), 4 pages x 105 cells (15 x 7), last cells are the mode keys
// "hira" / "kata" / "alpha" / "end" (0x44c238 / 0x44c240 / 0x44c248 / 0x44c250).
#include "game/ui/menu.hpp"
#include "game/fade.hpp"
#include "game/flag_manager.hpp"
#include "game/pad.hpp"
#include "game/sound_stub.hpp"
#include "game/ui/help_bar.hpp"
#include "game/ui/menu_tables.hpp"
#include "game/ui/message_box.hpp"
#include "game/ui/mouse_menu.hpp"
#include "game/ui/sprite.hpp"
#include "game/ui/strings.hpp"
#include "game/ui/text.hpp"
#include "game/ui/gradient_texture.hpp"
#include "game/ui/texture_registry.hpp"
#include "game/ui/ui_tables.hpp"
#include <cstring>
namespace baroque::game::ui {
namespace {
constexpr uint32_t kTextRgb = 0xbbbbbb;         // DAT_0044c190
constexpr uint32_t kTextDefault = 0x40bbbbbb;   // DAT_0044c198
bool same(const char* a, const char* b) { return std::strcmp(a, b) == 0; }   // FUN_0023f3fc == 0

// FUN_00165560 = messageWindowReady() (message_box.hpp): window 0x51b108 open and its opening slide finished

class NameEntryPage final : public MenuPage {
public:
    // orig FUN_00169be0 (work/decompiled/00169be0.c)
    NameEntryPage() = default;
    const char* debugName() const override { return "NAME ENTRY"; }

    // orig FUN_00169cd0 (work/decompiled/00169cd0.c)
    void open() override {
        makeFrame();
        makeTitle(str(10));
        nameEntryReset();                                  // FUN_0016ace0
        isOpen = 1;
        committed_ = 0;
        state_ = 0;
        cursor_ = 0;
        if (language() == 1) {
            mode_ = 0;
            savedMode_ = 0;
        } else if (language() == 0) {
            mode_ = 2;
            savedMode_ = 2;
        }
        page_ = static_cast<signed char>(mode_);
        nameEntry().pos = 0;
        // cursor box: the gradient texture's marching-ants border (uv 0,1 - 33,32 of 32x32), layer flag 2
        cursorBox_ = sprites().create(0.0f, 0.0f, 0.9f, 23.0f, 28.0f, 0.0f, 1.0f, 33.0f, 32.0f, gradientTextureId(), 2);
        for (int i = 0; i < 5; ++i)
            underline_[i] = sprites().create(static_cast<float>(i) * 30.0f + 186.0f, 104.0f, 1.0f, 26.0f, 2.0f, 0.0f, 0.0f, 0.0f,
                                             0.0f, kTexNone, 1);
        for (int i = 0; i < 4; ++i) {
            if (language() == 1 || i == 3) {
                const float* p = kNameEntryTabPos[i];
                const float* uv = kNameEntryTabUv[i];
                tabs_[i] = sprites().create(p[0], p[1], 1.0f, p[2], p[3], uv[0], uv[1], uv[2], uv[3], 0x2e, 1);
            }
        }
        makeFrame2(0);
        alpha_ = 0;
        angle_ = 0;
    }

    // orig FUN_00169f20 (work/decompiled/00169f20.c)
    void close() override {
        messageBox().close();                              // FUN_001ac020(0x53d730)
        freeFrame2();
        helpBar().clear(0);
        freeFrame();
        isOpen = 0;
        sprites().destroyRef(cursorBox_);
        for (Sprite*& s : underline_) sprites().destroyRef(s);
        for (Sprite*& s : tabs_) sprites().destroyRef(s);
    }

    // orig FUN_0016a210 (work/decompiled/0016a210.c)
    void update() override {
        if (isOpen != 0) {
            logic();                                       // FUN_0016a450
            if (isOpen != 0) draw();                       // FUN_0016a5c0
        }
    }

    // orig FUN_0016ae10 (work/decompiled/0016ae10.c)
    void input() override {
        const PadPort& p = pad().port[0];
        const uint32_t rep = p.repeat;                     // uRam004e6290
        if (isOpen == 0 || alpha_ != 0x40 || messageBox().isOpen()) return;
        if (p.pressed & 0x40) {
            confirm();                                     // FUN_0016b2c0
        } else if (p.pressed & 0x20) {
            back();                                        // FUN_0016b610
        } else if (p.pressed & 0x800) {
            jumpToEnd();                                   // FUN_0016b6f0
            sound().playSe(1.0f, 8);
        } else {
            if ((rep & 0x8000) != 0 || (rep & 0x40000) != 0) cursorLeft();          // FUN_0016b1a0
            else if ((rep & 0x2000) != 0 || (rep & 0x80000) != 0) cursorRight();   // FUN_0016b230
            if ((rep & 0x1000) != 0 || (rep & 0x10000) != 0) cursorUp();            // FUN_0016b060
            else if ((rep & 0x4000) != 0 || (rep & 0x20000) != 0) cursorDown();    // FUN_0016b100
        }
    }

    // PORT: mouse menu layer. Hover = the Right step of cursorRight() onto the hovered cell (cursor box size + SE 6);
    // a click is Cross on that cell (character, mode key or END). The dialogs are handled by the message box.
    void mouseInput() override {
        if (isOpen == 0 || alpha_ != 0x40 || messageBox().isOpen()) return;   // the guard of input()
        if (state_ != 0 || static_cast<unsigned>(mode_) >= 4) return;       // cursorRight() moves only here
        const int h = mouse::hovered(this);
        if (h < 0 || h > 0x68 || h == cursor_) return;
        cursor_ = static_cast<signed char>(h == 0 ? 0x68 : h - 1);
        cursorRight();
    }
    // PORT: the cells at the positions draw() lays them out (15 x 7, 26 px pitch with a gap after every 5, 32 px
    // rows from (32, 148)); a mode key is only clickable where its tab sprite is shown (EN: END only).
    void mouseItems() override {
        if (isOpen == 0 || messageBox().isOpen()) return;
        float x = 32.0f;
        float y = 148.0f;
        for (int idx = 0; kNameEntryChars[page_][idx] != nullptr; ++idx) {
            const char* c = kNameEntryChars[page_][idx];
            int key = -1;
            if (same(c, kNameEntryHira)) key = 0;
            else if (same(c, kNameEntryKata)) key = 1;
            else if (same(c, kNameEntryAlpha)) key = 2;
            else if (same(c, kNameEntryEnd)) key = 3;
            if (key < 0) {
                mouse::item(this, idx, x - 5.0f, y - 7.0f, 26.0f, 32.0f);   // centred on the cursor box (x + 8, y + 9)
            } else if (tabs_[key] != nullptr) {
                const float* p = kNameEntryTabPos[key];
                mouse::item(this, idx, p[0], p[1], p[2], p[3]);
            }
            x += 26.0f;
            if ((idx + 1) % 5 == 0) x += 26.0f;
            if ((idx + 1) % 0xf == 0) {
                x = 32.0f;
                y += 32.0f;
            }
        }
    }

    bool wantsNext() const override { return wantsNext_ != 0; }   // 0x16b8a0
    MenuPage* nextPage() override { return nullptr; }               // 0x16b8b0

private:
    const char* cell(int index) const { return kNameEntryChars[page_][index]; }

    // orig FUN_0016a450 (work/decompiled/0016a450.c)
    void logic() {
        if (state_ == 0) {
            angle_ += 0xf;
            if (0x168 < angle_) angle_ = -360;
        }
        if (alpha_ < 0x40 && fade().finished()) {
            alpha_ += 6;
            if (0x40 < alpha_) alpha_ = 0x40;
        }
        if (!helpBar().isSet(0) && alpha_ == 0x40) {
            helpBar().set(32.0f, 403.0f, 420.0f, 40.0f, 0, str(0x14));
            helpBar().setSpeed(0, 4);                      // uRam005181bc = 4
            sound().playBgm(0x18, 4, 1);
            flags().set(0x3b6);
        }
        if (!nameEntryChanged() && nameEntry().pos == 0 && state_ == 0) nameEntry().canCancel = 1;
        else nameEntry().canCancel = 0;
        dialog();                                          // FUN_0016a260
    }

    // orig FUN_0016a260 (work/decompiled/0016a260.c)
    void dialog() {
        if (!messageWindowReady()) return;
        MessageBox& box = messageBox();
        if (state_ == 1) {
            if (box.choice() == 2) {
                state_ = 0;
                mode_ = savedMode_;
                box.close();
            } else if (box.choice() != 0) {
                if (box.choice() == 0xd) flags().set(0x4f1);
                else if (box.choice() == 0xe) flags().set(0x4f2);
                else if (box.choice() == 0xf) flags().set(0x4f3);
                else if (box.choice() == 0x10) flags().set(0x4f4);
                advanceDialog();                           // FUN_0016af80
            }
        } else if (state_ == 2) {
            if (box.choice() == 1) {
                committed_ = 1;
                state_ = 3;
                close();                                   // (**(vtable + 0x10))(this)
                fade().set(0x40000000u, 0, 0);
                fade().set(0x80000000u, 0x10, 0);
                sound().stopBgm(8);
            } else if (box.choice() == 2) {
                back();                                    // FUN_0016b610
            }
        }
    }

    // orig FUN_0016af80 (work/decompiled/0016af80.c): name check, then difficulty (msg 0x3f) -> confirm (msg 0x3e)
    void advanceDialog() {
        if (!nameEntryChanged()) {
            sound().playSe(1.0f, 3);
        } else if (state_ == 0) {
            for (uint32_t f = 0x4f1; f < 0x4f5; ++f) flags().clear(f);
            state_ = 1;
            messageBox().open(0x3f, 999999);
        } else if (state_ == 1) {
            messageBox().close();
            state_ = 2;
            messageBox().open(0x3e, 999999);
        }
    }

    // orig FUN_0016ad40 (work/decompiled/0016ad40.c): EN cursor box size (the "end" key is a wider tab sprite)
    void cursorSize() {
        if (language() != 0 || cursorBox_ == nullptr) return;
        if (same(cell(cursor_), kNameEntryEnd)) {
            const float h = kNameEntryTabPos[3][3] + 4.0f;     // DAT_00495a9c
            cursorBox_->hw = (kNameEntryTabPos[3][2] + 2.0f) * 0.5f;   // DAT_00495a98
            cursorBox_->hh = h * 0.5f;
        } else {
            cursorBox_->hw = 11.5f;                        // 0x41380000
            cursorBox_->hh = 14.0f;                        // 0x41600000
        }
    }

    // orig FUN_0016b060 (work/decompiled/0016b060.c)
    void cursorUp() {
        if (isOpen == 0) return;
        if (state_ == 0 && static_cast<unsigned>(mode_) < 4) {
            if (cursor_ < 0xf) cursor_ = static_cast<signed char>('i' - (0xf - cursor_));
            else cursor_ = static_cast<signed char>(cursor_ - 0xf);
            sound().playSe(1.0f, 6);
        }
        cursorSize();
    }
    // orig FUN_0016b100 (work/decompiled/0016b100.c)
    void cursorDown() {
        if (isOpen == 0) return;
        if (state_ == 0 && static_cast<unsigned>(mode_) < 4) {
            if (cursor_ < 'Z') cursor_ = static_cast<signed char>(cursor_ + 0xf);
            else cursor_ = static_cast<signed char>(0xf - ('i' - cursor_));
            sound().playSe(1.0f, 6);
        }
        cursorSize();
    }
    // orig FUN_0016b1a0 (work/decompiled/0016b1a0.c)
    void cursorLeft() {
        if (isOpen == 0) return;
        if (state_ == 0 && static_cast<unsigned>(mode_) < 4) {
            cursor_ = static_cast<signed char>(cursor_ - 1);
            if (cursor_ < 0) cursor_ = 0x68;
            sound().playSe(1.0f, 6);
        }
        cursorSize();
    }
    // orig FUN_0016b230 (work/decompiled/0016b230.c)
    void cursorRight() {
        if (isOpen == 0) return;
        if (state_ == 0 && static_cast<unsigned>(mode_) < 4) {
            cursor_ = static_cast<signed char>(cursor_ + 1);
            if ('h' < cursor_) cursor_ = 0;
            sound().playSe(1.0f, 6);
        }
        cursorSize();
    }

    // orig FUN_0016b2c0 (work/decompiled/0016b2c0.c): Cross
    void confirm() {
        if (isOpen == 0 || state_ != 0) return;
        if (mode_ == 4) {
            advanceDialog();
            return;
        }
        if (mode_ != 3 && mode_ != 2 && mode_ != 1 && mode_ != 0) return;
        if (same(cell(cursor_), kNameEntryHira)) {
            if (language() != 1) {
                advance();                                 // FUN_0016b5b0 (EN: the key is not shown; only the position moves)
                return;
            }
            mode_ = 0;
            page_ = 0;
            sound().playSe(1.0f, 8);
            return;
        }
        if (same(cell(cursor_), kNameEntryKata)) {
            if (language() != 1) {
                advance();
                return;
            }
            mode_ = 1;
            page_ = 1;
            sound().playSe(1.0f, 8);
            return;
        }
        if (!same(cell(cursor_), kNameEntryAlpha)) {
            if (!same(cell(cursor_), kNameEntryEnd)) {
                storeChar();                               // FUN_0016b7b0
                advance();                                 // FUN_0016b5b0
                return;
            }
            if (!nameEntryChanged()) {
                sound().playSe(1.0f, 3);
                return;
            }
            savedMode_ = mode_;
            mode_ = 4;
            advanceDialog();
            return;
        }
        if (language() != 1) {
            advance();
            return;
        }
        if (language() == 1) mode_ = 3;
        else if (language() == 0) mode_ = 2;
        page_ = static_cast<signed char>(mode_);
        sound().playSe(1.0f, 8);
    }

    // orig FUN_0016b5b0 (work/decompiled/0016b5b0.c, disassembled for the clamp): position + 1, at the end jump to "end"
    void advance() {
        signed char& pos = nameEntry().pos;
        pos = static_cast<signed char>(pos + 1);
        if (4 < pos) {
            pos = static_cast<signed char>(pos - 1);
            jumpToEnd();                                   // FUN_0016b6f0
        }
        sound().playSe(1.0f, 1);
    }

    // orig FUN_0016b610 (work/decompiled/0016b610.c): Circle
    void back() {
        if (isOpen == 0) return;
        if (state_ == 0) {
            clearChar();                                   // FUN_0016b830
            signed char& pos = nameEntry().pos;
            pos = static_cast<signed char>(pos - 1);
            if (pos < 0) pos = 0;
            else sound().playSe(1.0f, 2);
        } else if (messageBox().ready()) {                 // FUN_001ab3f0
            messageBox().close();
            if (state_ == 2) {
                state_ = 0;
                advanceDialog();                           // back to the difficulty question
            } else {
                state_ = 0;
                mode_ = savedMode_;
            }
            sound().playSe(1.0f, 2);
        }
    }

    // orig FUN_0016b6f0 (work/decompiled/0016b6f0.c): cursor to the first "end" key of the page
    void jumpToEnd() {
        cursor_ = 0;
        if (!same(cell(0), kNameEntryEnd)) {
            signed char c = cursor_;
            for (;;) {
                cursor_ = static_cast<signed char>(c + 1);
                if (same(cell(static_cast<signed char>(c + 1)), kNameEntryEnd)) break;
                c = cursor_;
            }
        }
        cursorSize();
    }

    // orig FUN_0016b7b0 (work/decompiled/0016b7b0.c)
    void storeChar() {
        const int pos = nameEntry().pos;
        nameEntry().name[pos * 2] = cell(cursor_)[0];
        nameEntry().name[pos * 2 + 1] = cell(cursor_)[1];
    }
    // orig FUN_0016b830 (work/decompiled/0016b830.c): ideographic space at the current position
    void clearChar() {
        const int pos = nameEntry().pos;
        nameEntry().name[pos * 2] = kNameEntrySpace[0];
        nameEntry().name[pos * 2 + 1] = kNameEntrySpace[1];
    }

    void drawName(float y, float z) {
        TextRenderer& t = text();
        for (int i = 0; i < 5; ++i) {
            t.y = y;
            t.z = z;
            t.x = static_cast<float>(i) * 30.0f + 186.0f;
            t.draw("%c%c\n", nameEntry().name[i * 2], nameEntry().name[i * 2 + 1]);   // 0x44c230
        }
    }

    // orig FUN_0016a5c0 (work/decompiled/0016a5c0.c)
    void draw() {
        animateTitle();                                    // FUN_00164dd0 (+ empty FUN_00164f30)
        int blink = 0;
        if (alpha_ == 0x40) {
            float s = sinDeg(static_cast<float>(angle_));
            s = (s + 1.0f) * 127.0f;
            blink = ftoiWrap(s);
        }
        const uint32_t a = static_cast<uint32_t>(alpha_) << 24;
        for (Sprite* tab : tabs_)
            if (tab != nullptr) tab->color = a + 0x7f7f7f;
        TextRenderer& t = text();
        t.proportional = 0;                                // uRam004e96b8
        t.halfW = 11.2f;                                   // 0x41333333
        t.halfH = 12.6f;                                   // 0x4149999a
        t.color = kTextRgb + a;
        drawName(74.0f, 1.0f);
        for (int i = 0; i < 5; ++i) {
            if (underline_[i] == nullptr) continue;
            if (i == nameEntry().pos) {
                if (static_cast<unsigned>(mode_) < 4) underline_[i]->color = static_cast<uint32_t>(blink) * 0x1000000u + 0x22227f;
                else underline_[i]->color = a + 0x22227f;
            } else {
                underline_[i]->color = 0x407f6363;
            }
        }
        t.halfW = 8.0f;
        t.halfH = 9.0f;
        float x = 32.0f;
        float y = 148.0f;
        for (int idx = 0; kNameEntryChars[page_][idx] != nullptr; ++idx) {
            const char* c = kNameEntryChars[page_][idx];
            if (!same(c, kNameEntryHira) && !same(c, kNameEntryKata) && !same(c, kNameEntryAlpha) && !same(c, kNameEntryEnd)) {
                t.z = 1.0f;
                if (cursor_ == idx && static_cast<unsigned>(mode_) < 4) t.color = a + 0x22227f;
                else t.color = kTextRgb + a;
                t.x = x;
                t.y = y;
                t.draw("%s", c);                           // 0x44c258
            }
            if (static_cast<unsigned>(mode_) < 4) {
                if (cursor_ == idx && cursorBox_ != nullptr) {
                    const float cx = (x + 13.0f) - 5.0f;
                    cursorBox_->color = a + 0x7f7f7f;
                    cursorBox_->cx = cx;
                    cursorBox_->cy = (y + 16.0f) - 7.0f;
                    if (language() == 0 && same(cell(cursor_), kNameEntryEnd)) {
                        cursorBox_->cx = cx;
                        cursorBox_->cy = (y + 16.0f) - 3.0f;
                    }
                }
            } else if (cursorBox_ != nullptr) {
                cursorBox_->color = 0;
            }
            x += 26.0f;
            if ((idx + 1) % 5 == 0) x += 26.0f;
            if ((idx + 1) % 0xf == 0) {
                x = 32.0f;
                y += 32.0f;
            }
        }
        if (state_ == 2 && messageBox().isOpen()) {        // the name repeated inside the confirm window
            t.layerFlag = 0;
            t.halfW = 11.2f;
            t.halfH = 12.6f;
            t.color = kTextRgb + a;
            drawName(154.0f, 5.0f);
        }
        t.layerFlag = 1;
        t.proportional = 1;
        t.color = kTextDefault;
    }

    int wantsNext_ = 0;              // +0x470
    signed char state_ = 0;          // +0x474: 0 typing, 1 difficulty dialog (0x3f), 2 confirm dialog (0x3e), 3 committed
    int mode_ = 0;                   // +0x478: character page 0..3, 4 = dialog
    int savedMode_ = 0;              // +0x47c
    Sprite* cursorBox_ = nullptr;    // +0x480
    signed char cursor_ = 0;         // +0x484 (cell 0..104)
    signed char page_ = 0;           // +0x485
    Sprite* underline_[5] = {};      // +0x488
    Sprite* tabs_[4] = {};           // +0x49c
    int angle_ = 0;                  // +0x4ac
    unsigned alpha_ = 0;             // +0x4b0
    int committed_ = 0;              // +0x4b4
};
}  // namespace
MenuPage* newNameEntryPage() { return new NameEntryPage(); }   // FUN_00169010 (alloc FUN_00169370 + ctor FUN_00169be0)
}  // namespace baroque::game::ui
