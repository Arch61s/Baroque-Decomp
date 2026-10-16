// OPTION main page (vtable 0x4cdd00; ctor FUN_001683a0; open FUN_00168480, close FUN_00168500, is-active
// FUN_00168540, update FUN_00168630, input FUN_001688f0, wants-next 0x168550, next page FUN_00168560).
#include "game/ui/menu.hpp"
#include "game/fade.hpp"
#include "game/options.hpp"
#include "game/pad.hpp"
#include "game/sound_stub.hpp"
#include "game/ui/help_bar.hpp"
#include "game/ui/mouse_menu.hpp"
#include "game/ui/strings.hpp"
#include "game/ui/text.hpp"
#include <cmath>
namespace baroque::game::ui {
namespace {
// PORT: row 7 "PC SETTINGS" (menu mode 11, page_pc_settings.cpp) after the seven database rows; GAME CONFIG (which also
// lists it) is not reachable from the title in the original, so the title's Option menu gets its own entry.
constexpr int kPcRow = 7;
const char* rowLabel(int i) { return i == kPcRow ? "PC SETTINGS" : str(i + 2); }
class OptionPage final : public MenuPage {
public:
    OptionPage() {   // orig FUN_001683a0 (work/decompiled/001683a0.c)
        isOpen = 0; angle_ = 0; alpha_ = 0;
        if (options().cursorPos == 1) gOptionCursor = 0;
        selected_ = -1; nextMode_ = -1;
    }
    const char* debugName() const override { return "OPTION"; }
    // orig FUN_00168480 (work/decompiled/00168480.c)
    void open() override {
        makeFrame();
        makeTitle(str(0));
        isOpen = 1; angle_ = 0; alpha_ = 0;
        if (options().cursorPos == 1) gOptionCursor = 0;
        selected_ = -1; nextMode_ = -1;
        makeFrame2(0);
    }
    // orig FUN_00168500 (work/decompiled/00168500.c)
    void close() override { freeFrame2(); helpBar().clear(0); freeFrame(); isOpen = 0; }
    // orig FUN_00168630 (work/decompiled/00168630.c)
    void update() override {
        if (isOpen == 0) return;
        animateTitle();
        if (titleSettled != 0) drawList();
    }
    // orig FUN_001688f0 (work/decompiled/001688f0.c)
    void input() override {
        if (isOpen == 0 || !fade().finished() || alpha_ < 0x40) return;
        const PadPort& p = pad().port[0];
        if (p.repeat & (0x8000 | 0x40000)) { /* FUN_00168b60: nothing */ }
        else if (p.repeat & (0x2000 | 0x80000)) { /* FUN_00168b70: nothing */ }
        else if (p.repeat & (0x1000 | 0x10000)) cursorPrev();     // FUN_00168ac0
        else if (p.repeat & (0x4000 | 0x20000)) cursorNext();     // FUN_00168a10
        else if (p.pressed & 0x40) select();                       // FUN_00168b80
        else if (p.pressed & 0x20) { close(); sound().playSe(1.0f, 8); }   // FUN_00168d20
    }
    // PORT: mouse menu layer. Hover = the Down step of cursorNext() onto the hovered row (help text + SE 6).
    void mouseInput() override {
        if (isOpen == 0 || !fade().finished() || alpha_ < 0x40) return;   // the guard of input()
        const int h = mouse::hovered(this);
        if (h < 0 || h > kPcRow || h == gOptionCursor) return;
        gOptionCursor = static_cast<signed char>(h == 0 ? kPcRow : h - 1);
        cursorNext();
    }
    // PORT: the eight rows of drawList() (text at x 63, y 79 + 31 i).
    void mouseItems() override {
        if (isOpen == 0 || titleSettled == 0) return;
        for (int i = 0; i <= kPcRow; ++i) mouse::row(this, i, 55.0f, 63.0f + mouse::textWidth(rowLabel(i)) + 8.0f, static_cast<float>(i) * 31.0f + 79.0f, 31.0f);
    }
    bool wantsNext() const override { return nextMode_ != -1; }   // 0x168550: !(+0x475 == -1)
    // orig FUN_00168560 (work/decompiled/00168560.c)
    MenuPage* nextPage() override {
        switch (nextMode_) {
        case 0: case 1: case 2: case 3: case 4: case 5: case 6: return newDatabasePage(nextMode_);
        case 10: return newGameConfigPage();
        case 11: return newPcSettingsPage(0);   // PORT
        default: return nullptr;
        }
    }
private:
    void setHelp() {   // FUN_0015c490(32, 403, 448, 40, 0x5180f0, 0, str(cursor + 0xd)); uRam005181bc = 4
        helpBar().set(32.0f, 403.0f, 448.0f, 40.0f, 0, gOptionCursor == kPcRow ? "Display, mouse and keyboard settings" : str(gOptionCursor + 0xd));   // PORT: row 7
        helpBar().setSpeed(0, 4);
    }
    // orig FUN_00168a10 (work/decompiled/00168a10.c)
    void cursorNext() {
        gOptionCursor = static_cast<signed char>(gOptionCursor + 1);
        if (gOptionCursor > kPcRow) gOptionCursor = 0;   // PORT: original > 6
        setHelp();
        sound().playSe(1.0f, 6);
    }
    // orig FUN_00168ac0 (work/decompiled/00168ac0.c)
    void cursorPrev() {
        gOptionCursor = static_cast<signed char>(gOptionCursor - 1);
        if (gOptionCursor < 0) gOptionCursor = kPcRow;   // PORT: original 6
        setHelp();
        sound().playSe(1.0f, 6);
    }
    // orig FUN_00168b80 (work/decompiled/00168b80.c)
    void select() {
        switch (gOptionCursor) {
        case 0: case 1: case 2: case 3: case 4: case 5: case 6: case 10:
            selected_ = gOptionCursor; nextMode_ = gOptionCursor; sound().playSe(1.0f, 8); break;
        case kPcRow: selected_ = gOptionCursor; nextMode_ = 11; sound().playSe(1.0f, 8); break;   // PORT
        default: break;
        }
    }
    // orig FUN_00168680 (work/decompiled/00168680.c)
    void drawList() {
        angle_ += 8;
        if (angle_ > 0x168) angle_ = -360;
        if (alpha_ < 0x40 && fade().finished()) { alpha_ += 6; if (alpha_ > 0x40) alpha_ = 0x40; }
        TextRenderer& t = text();
        if (fade().finished()) t.layerFlag = 1;
        if (!helpBar().isSet(0)) {
            setHelp();
            sound().playBgm(0x18, 4, 1);
        }
        t.halfW = 8.0f; t.halfH = 9.0f;
        for (int i = 0; i <= kPcRow; ++i) {   // PORT: + row 7 (original i < 7)
            t.x = 63.0f;       // 0x427c0000
            t.z = 0.9f;        // 0x3f666666
            t.y = static_cast<float>(i) * 31.0f + 79.0f;
            t.color = (i == gOptionCursor) ? (static_cast<uint32_t>(alpha_) << 24) + 0x22227fu : 0xbbbbbbu + (static_cast<uint32_t>(alpha_) << 24);
            t.draw("%s", rowLabel(i));
        }
        t.layerFlag = 1;
        t.color = 0x40bbbbbb;   // DAT_0044c198
        t.halfW = 8.0f; t.halfH = 9.0f;
    }
    signed char selected_ = -1;   // +0x474
    signed char nextMode_ = -1;   // +0x475
    int angle_ = 0;               // +0x478
    unsigned alpha_ = 0;          // +0x47c
};
}
MenuPage* newOptionPage() { return new OptionPage(); }   // FUN_00163b10
}
