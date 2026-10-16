// FILE_SELECT page (menu mode 9; vtable 0x4cf2b0; ctor FUN_002017d0; open FUN_00201a00, close FUN_00201cd0,
// is-active 0x2061e0 (+0x46c), update FUN_002031b0, input FUN_00203f60, wants-next 0x2061f0 (+0x470),
// next page 0x2031a0 (= FUN_00163b10 OPTION main)). SCENES.md 8.
// Flow uRam0053ea20: 1 = title flow (GameLoad scene: continue / new game / view database / delete),
// 2 = in-game save flow. State uRam0053ea28 drives the memory-card requests (FUN_002031b0 switch).
#include "game/ui/menu.hpp"
#include "game/event/event_manager.hpp"
#include "game/hud/hud_api.hpp"
#include "game/fade.hpp"
#include "game/options.hpp"
#include "game/pad.hpp"
#include "game/save_manager.hpp"
#include "game/scene_manager.hpp"
#include "game/scenes/front_scenes.hpp"
#include "game/sound_stub.hpp"
#include "game/ui/database.hpp"
#include "game/ui/gradient_texture.hpp"
#include "game/ui/help_bar.hpp"
#include "game/ui/message_box.hpp"
#include "game/ui/mouse_menu.hpp"
#include "game/ui/sprite.hpp"
#include "game/ui/strings.hpp"
#include "game/ui/text.hpp"
#include "game/ui/texture_registry.hpp"
#include "game/ui/ui_tables.hpp"
#include <cstdio>
namespace baroque::game::ui {
namespace {
FileSelectGlobals gFs;
constexpr uint32_t kTextRgb = 0xbbbbbb;         // DAT_0044c190
constexpr uint32_t kTextDefault = 0x40bbbbbb;   // DAT_0044c198
constexpr float kScaleW = 1.16f;                // DAT_0044c180
constexpr float kScaleH = 1.22f;                // DAT_0044c188

class FileSelectPage final : public MenuPage {
public:
    // orig FUN_002017d0 (work/decompiled/002017d0.c)
    FileSelectPage() { gFs.state = 0; }
    const char* debugName() const override { return "FILE_SELECT"; }

    // orig FUN_00201a00 (work/decompiled/00201a00.c)
    void open() override {
        mcStart();                                         // FUN_00203360
        makeFrame();
        makeTitle(str(9));
        isOpen = 1;
        front().newGame = 0;                               // DAT_00469700
        front().loadSlot = -1;                             // DAT_00469708
        gFs.cardChanged = 0;
        gFs.flow = scenes().isCurrent("GameLoad") ? 1 : 2;
        if (gFs.cursor == -1) gFs.cursor = 0;
        gFs.state = 1;
        wantsNext_ = 0;
        angle_ = 0;
        alpha_ = 0;
        makeFrame2(0);
        build();                                           // FUN_00201e20
        if (scenes().isCurrent("Game....") || scenes().isCurrent("Ending..")) {
            makeGameBackdrop();                            // FUN_00164910
            if (!hudMapShown()) {
                gameMapHidden_ = 0;
            } else {
                hudMapHide();                              // FUN_00126fb0(0x50a350)
                gameMapHidden_ = 1;                        // +0x984
            }
            sound().setBgmFadeTarget(1.0f, 4);             // FUN_00115f00(1.0, snd, 4)
            sound().setMenuDuck(1);                        // FUN_001161d0(snd, 1)
            eventManager().requestTransition(0.0f, 5);     // FUN_00177cc0(0, 0x51b620, 5)
        }
        fade().set(0, 0, 0);
    }

    // orig FUN_00201cd0 (work/decompiled/00201cd0.c)
    void close() override {
        messageBox().close();
        freeFrame2();
        freeFrame();
        helpBar().clear(0);
        isOpen = 0;
        destroyAll();                                      // FUN_00202500
        if (scenes().isCurrent("Game....")) {
            sound().setBgmFadeTarget(0.7f, 4);             // FUN_00115f00(0x3f333333, snd, 4)
            if (gameMapHidden_ != 0) hudMapShow();         // FUN_00126cc0(0x50a350)
            sound().setMenuDuck(0);                        // FUN_001161d0(snd, 0)
            eventManager().requestTransition(0.0f, 8);     // FUN_00177cc0(0, 0x51b620, 8)
        }
        mcStop();                                          // FUN_002033a0
    }

    // orig FUN_002031b0 (work/decompiled/002031b0.c)
    void update() override {
        if (isOpen == 0) return;
        animateTitle();
        if (titleSettled != 0) draw();                     // FUN_002040a0
        switch (gFs.state) {
        case 1: stateCheck(); break;                       // FUN_00203ae0
        case 3: stateLoadWait(4); break;                   // FUN_002034a0
        case 4: stateLoadDone(); break;                    // FUN_00203510
        case 6: stateLoadWait(7); break;                   // FUN_002035c0
        case 7: stateViewLoaded(); break;                  // FUN_00203630
        case 9: stateSaveWait(); break;                    // FUN_00203700
        case 10: stateSaveDone(); break;                   // FUN_00203770
        case 0xc: stateDeleteWait(); break;                // FUN_00203830
        case 0xd: stateDeleteDone(); break;                // FUN_002038a0
        case 0xe: stateFormatWait(); break;                // FUN_00203980
        case 0xf: stateFormatDone(); break;                // FUN_00203a10
        case 0x10: stateError(); break;                    // FUN_00203ba0
        default: break;                                    // 0, 2, 5, 8, 0xb and others
        }
        gFs.cardChanged = cardCheck() ? 1 : 0;             // FUN_00203440
        if ((gFs.state == 2 || gFs.state == 0x11) && gFs.cardChanged != 0) {
            messageBox().close();
            close();
        }
    }

    // orig FUN_00203f60 (work/decompiled/00203f60.c)
    void input() override {
        if (isOpen == 0 || alpha_ <= 0x3f) return;
        if (gFs.state != 2 && gFs.state != 5 && gFs.state != 0x10) return;
        const PadPort& p = pad().port[0];
        if ((p.pressed & 0x8000) != 0 || (p.pressed & 0x40000) != 0) {
            // FUN_00205820: empty
        } else if ((p.pressed & 0x2000) != 0 || (p.pressed & 0x80000) != 0) {
            // FUN_00205830: empty
        } else if ((p.repeat & 0x1000) != 0 || (p.repeat & 0x10000) != 0) {
            cursorUp();                                    // FUN_00205790
        } else if ((p.repeat & 0x4000) != 0 || (p.repeat & 0x20000) != 0) {
            cursorDown();                                  // FUN_00205700
        } else if (p.pressed & 0x40) {
            decide();                                      // FUN_00205840
        } else if (p.pressed & 0x20) {
            decide();                                      // FUN_00205ff0 (same dispatch as Cross)
        }
    }

    // PORT: mouse menu layer. Hover on a file = the Down step of cursorDown() onto it (SE 6). While the file menu
    // (message 0x42) is open the message box moves its own cursor; the help line follows it as cursorUp/Down do.
    void mouseInput() override {
        if (isOpen == 0 || alpha_ <= 0x3f) return;                            // the guards of input()
        if (gFs.state != 2 && gFs.state != 5 && gFs.state != 0x10) return;
        if (messageBox().isOpen()) {
            if (messageBox().currentId() == 0x42 && messageBox().mouseMovedCursor()) fileMenuHelp();
            return;
        }
        const int h = mouse::hovered(this);
        if (h < 0 || h > 2 || h == gFs.cursor) return;
        gFs.cursor = static_cast<signed char>(h == 0 ? 2 : h - 1);
        cursorDown();
    }
    // PORT: the three file panels where draw() puts the highlight (x 76, y 66 + 115 s, 373 x 90).
    void mouseItems() override {
        if (isOpen == 0 || titleSettled == 0 || messageBox().isOpen()) return;
        for (int s = 0; s < 3; ++s) mouse::item(this, s, 76.0f, static_cast<float>(s) * 115.0f + 66.0f, 373.0f, 90.0f);
    }

    bool wantsNext() const override { return wantsNext_ != 0; }   // 0x2061f0
    MenuPage* nextPage() override { return newOptionPage(); }       // 0x2031a0: j FUN_00163b10

private:
    // ---- memory card helpers --------------------------------------------------------------------------------
    // orig FUN_00203360 (work/decompiled/00203360.c)
    void mcStart() {
        saveManager().titleCheck();                        // FUN_00117150
        mcState_ = 1;
        delay_ = 0x1e;
    }
    // orig FUN_002033a0 (work/decompiled/002033a0.c)
    void mcStop() {
        saveManager().portReset();                         // FUN_0020e7f0
        delay_ = 0x1e;
    }
    // orig FUN_002033e0 (work/decompiled/002033e0.c)
    bool mcReady() {
        if (mcState_ == 3) return true;
        if (mcState_ == 1) {
            saveManager().portCheckStart(0);               // FUN_0020e6e0
            mcState_ = 3;
            return true;
        }
        return false;
    }
    // orig FUN_00203440 (work/decompiled/00203440.c): card removed / changed -> state 0x11
    bool cardCheck() {
        if (mcState_ != 3) return false;
        if (saveManager().portCheckResult() != 0) return false;
        mcStop();
        gFs.state = 0x11;
        return true;
    }
    // orig FUN_00203db0 (work/decompiled/00203db0.c): opens the error message for the last result, 1 when shown
    bool errorMessage() {
        const SaveManager& mc = saveManager();
        const int kb = static_cast<int>(mc.requiredKB);   // uRam005097a0
        switch (mc.result) {                               // uRam00509834
        case 1:
            if (gFs.flow != 2) return false;
            messageBox().open(0x29, kb);
            return true;
        case 2:
            return false;
        case 3:
            status_[0] = 3;
            status_[1] = 3;
            status_[2] = 3;
            if (gFs.flow == 2 && gFs.state == 10) {
                messageBox().open(0x24, gFs.cursor + 1);
                return true;
            }
            if (gFs.flow == 1) {
                messageBox().open(0x18, 999999);
                return true;
            }
            return false;
        case 4:
        case 6:
            if (gFs.state == 10) messageBox().open(0x28, kb);
            else messageBox().open(0x15, 999999);
            return true;
        case 5:
            if (gFs.flow == 2) {
                messageBox().open(0xb, 999999);
                return true;
            }
            if (gFs.flow == 1) {
                messageBox().open(0x1a, 999999);
                return true;
            }
            return false;
        default:
            return false;
        }
    }

    // ---- state handlers ---------------------------------------------------------------------------------------
    // orig FUN_00203ae0 (work/decompiled/00203ae0.c): state 1, initial card read -> slot status
    void stateCheck() {
        if (saveManager().busy != 0 || !mcReady()) return;
        if (!errorMessage()) {
            gFs.state = 2;
            for (int i = 0; i < 3; ++i) status_[i] = (saveManager().summary[i].present == 1) ? 2 : 1;
        } else {
            gFs.state = 0x10;
            mcStop();
        }
    }
    // orig FUN_002034a0 (state 3 -> 4) / FUN_002035c0 (state 6 -> 7): wait 0x1e frames, then load the slot
    void stateLoadWait(int next) {
        if (saveManager().busy == 0 && delay_ == 0) {
            gFs.state = next;
            saveManager().load(gFs.cursor);                // FUN_001171c0
            return;
        }
        --delay_;
    }
    // orig FUN_00203510 (work/decompiled/00203510.c): state 4, loaded for "Continue a game."
    void stateLoadDone() {
        if (saveManager().busy != 0 || !mcReady()) return;
        messageBox().close();
        if (!errorMessage()) {
            messageBox().open(0x14, 999999);
            gFs.state = 5;
            front().newGame = 0;
            front().loadSlot = gFs.cursor;
        } else {
            gFs.state = 0x10;
            mcStop();
        }
    }
    // orig FUN_00203630 (work/decompiled/00203630.c): state 7, loaded for "View a database." -> Option scene
    void stateViewLoaded() {
        if (saveManager().busy != 0 || !mcReady()) return;
        messageBox().close();
        if (!errorMessage()) {
            gFs.state = 8;
            optionApply();                                 // FUN_0015da60
            close();
            fade().set(0, 0, 0);
            fade().set(0x7f000000u, 0x10, 0);
        } else {
            gFs.state = 0x10;
            mcStop();
        }
    }
    // orig FUN_00203700 (work/decompiled/00203700.c): state 9 -> 10, save
    void stateSaveWait() {
        if (saveManager().busy == 0 && delay_ == 0) {
            gFs.state = 10;
            saveManager().save(gFs.cursor);                // FUN_001171a0
            return;
        }
        --delay_;
    }
    // orig FUN_00203770 (work/decompiled/00203770.c)
    void stateSaveDone() {
        if (saveManager().busy != 0 || !mcReady()) return;
        messageBox().close();
        if (!errorMessage()) {
            messageBox().open(0x27, 999999);
            gFs.state = 2;
            status_[gFs.cursor] = 2;
            saveManager().portCheckStart(0);
            gFs.saved = 1;
        } else {
            gFs.state = 0x10;
            mcStop();
        }
    }
    // orig FUN_00203830 (work/decompiled/00203830.c): state 0xc -> 0xd, delete
    void stateDeleteWait() {
        if (saveManager().busy == 0 && delay_ == 0) {
            gFs.state = 0xd;
            saveManager().deleteSlot(gFs.cursor);          // FUN_001171e0
            return;
        }
        --delay_;
    }
    // orig FUN_002038a0 (work/decompiled/002038a0.c)
    void stateDeleteDone() {
        if (saveManager().busy != 0 || !mcReady()) return;
        messageBox().close();
        if (!errorMessage()) {
            messageBox().open(0x46, gFs.cursor + 1);
            gFs.state = 2;
            status_[gFs.cursor] = 1;
            if (language() == 1) hideSlotDigits(gFs.cursor);   // FUN_00203060
            saveManager().portCheckStart(0);
        } else {
            gFs.state = 0x10;
            mcStop();
        }
    }
    // orig FUN_00203980 (work/decompiled/00203980.c): state 0xe -> 0xf, format
    void stateFormatWait() {
        if (saveManager().busy == 0 && delay_ == 0) {
            gFs.state = 0xf;
            messageBox().close();
            messageBox().open(0xc, 999999);
            saveManager().format();                        // FUN_00117200
            return;
        }
        --delay_;
    }
    // orig FUN_00203a10 (work/decompiled/00203a10.c)
    void stateFormatDone() {
        if (saveManager().busy != 0) return;
        const bool waited = 0x78 < gFs.formatTimer;
        ++gFs.formatTimer;
        if (!waited || !mcReady()) return;
        messageBox().close();
        if (!errorMessage()) {
            messageBox().open(0xd, 999999);
            gFs.state = 2;
            saveManager().portCheckStart(0);
        } else {
            gFs.state = 0x10;
            mcStop();
        }
    }
    // orig FUN_00203ba0 (work/decompiled/00203ba0.c): state 0x10, error messages
    void stateError() {
        MessageBox& box = messageBox();
        if (!box.isOpen()) return;
        const uint32_t pressed = pad().port[0].pressed;
        if (box.currentId() == 0xb) {
            if (box.choice() == 1) {
                box.close();
                box.open(0xf, 999999);
                mcStop();
                gFs.state = 0xe;
            } else if (box.choice() == 2) {
                box.close();
                close();
            }
        } else if (box.currentId() == 0x24) {
            if (box.choice() == 1) {
                box.close();
                box.open(0x26, gFs.cursor + 1);
                gFs.state = 9;
                mcStop();
            } else if (box.choice() == 2) {
                box.close();
                gFs.state = 1;
                mcState_ = 1;
            }
        } else if (box.currentId() == 0x29) {
            if ((pressed & 0x40) != 0 || (pressed & 0x20) != 0) {
                box.close();
                gFs.state = 1;
                mcState_ = 1;
            }
        } else if ((box.currentId() == 0x18 || box.currentId() == 0x1a || box.currentId() == 0x28 || box.currentId() == 0x15) &&
                   ((pressed & 0x40) != 0 || (pressed & 0x20) != 0)) {
            box.close();
            close();
        }
    }

    // ---- input handlers ---------------------------------------------------------------------------------------
    // orig FUN_00205570 (work/decompiled/00205570.c): help text of the highlighted file-menu entry
    void fileMenuHelp() {
        const int h = messageBox().highlighted();          // FUN_001ac7d0
        if (h == 0x11) helpBar().set(32.0f, 403.0f, 420.0f, 40.0f, 0, str(0x17));
        else if (h == 0x12) helpBar().set(32.0f, 403.0f, 420.0f, 40.0f, 0, str(0x18));
        else if (h == 0x13) helpBar().set(32.0f, 403.0f, 420.0f, 40.0f, 0, str(0x19));
        else if (h == 0x14) helpBar().set(32.0f, 403.0f, 420.0f, 40.0f, 0, str(0x1a));
        helpBar().setSpeed(0, 4);                          // uRam005181bc = 4
    }
    // orig FUN_00205700 (work/decompiled/00205700.c)
    void cursorDown() {
        if (isOpen == 0) return;
        if (!messageBox().isOpen()) {
            gFs.cursor = static_cast<signed char>(gFs.cursor + 1);
            if (2 < gFs.cursor) gFs.cursor = 0;
            sound().playSe(1.0f, 6);
        } else if (messageBox().currentId() == 0x42) {
            fileMenuHelp();
        }
    }
    // orig FUN_00205790 (work/decompiled/00205790.c)
    void cursorUp() {
        if (isOpen == 0) return;
        if (!messageBox().isOpen()) {
            gFs.cursor = static_cast<signed char>(gFs.cursor - 1);
            if (gFs.cursor < 0) gFs.cursor = 2;
            sound().playSe(1.0f, 6);
        } else if (messageBox().currentId() == 0x42) {
            fileMenuHelp();
        }
    }
    // orig FUN_00205840 (Cross) / FUN_00205ff0 (Circle)
    void decide() {
        if (isOpen == 0) return;
        if (gFs.flow == 1) decideTitle();                  // FUN_00205890
        else decideSave();                                 // FUN_00205e20
    }
    // orig FUN_00205890 (work/decompiled/00205890.c): title flow
    void decideTitle() {
        if (saveManager().busy != 0) return;
        MessageBox& box = messageBox();
        const int cur = gFs.cursor;
        if (!box.isOpen()) {
            if (status_[cur] == 2) {
                box.open(0x42, 999999);
                fileMenuHelp();
            } else if (status_[cur] == 1) {               // empty slot: new game -> NameEntr
                gFs.state = 0xb;
                close();
                optionDefaults();                          // FUN_0015d990
                optionApply();                             // FUN_0015da60
                rankingClear();                            // FUN_001f2700
                front().newGame = 1;
                front().loadSlot = -1;
                fade().set(0, 0, 0);
                fade().set(0x7f000000u, 0x10, 0);
                front().ea48 = saveManager().clearedFlag;  // uRam0053ea48 = uRam00509840
                sound().playSe(1.0f, 1);
            } else {
                sound().playSe(1.0f, 3);
            }
            return;
        }
        const int id = box.currentId();
        if (id == 0x42) {
            if (box.choice() == 0) {
                // the original re-tests choice() == 2 here (unreachable)
                return;
            }
            if (box.highlighted() == 0x11) {               // "Begin the game with your database intact."
                box.close();
                box.open(0x3d, 999999);
            } else if (box.choice() == 0x12) {             // "Continue a game."
                box.close();
                box.open(0x12, cur + 1);
            } else if (box.choice() == 0x13) {             // "View a database."
                box.close();
                box.open(0x13, cur + 1);
                gFs.state = 6;
                mcStop();
            } else if (box.choice() == 0x14) {             // "Delete a game file."
                box.close();
                box.open(0x43, cur + 1);
            } else if (box.choice() == 2) {
                box.close();
                helpBar().clear(0);
            }
        } else if (id == 0x3d) {
            if (box.choice() == 1) {
                gFs.state = 0xb;
                close();
                front().newGame = 1;
                front().loadSlot = cur;
                fade().set(0, 0, 0);
                fade().set(0x7f000000u, 0x10, 0);
                front().ea48 = saveManager().clearedFlag;
            }
            box.close();
        } else if (id == 0x12) {
            if (box.choice() == 1) {
                box.close();
                box.open(0x13, cur + 1);
                gFs.state = 3;
                nameEntrySet(saveManager().summary[cur].name);   // FUN_0016ac80(0x509848 + slot*0x38)
                mcStop();
            } else {
                box.close();
                helpBar().clear(0);
            }
        } else if (id == 0x14) {
            box.close();
            optionApply();
            close();
            fade().set(0, 0, 0);
            fade().set(0x7f000000u, 0x10, 0);
            sound().playSe(1.0f, 1);
        } else if (id == 0x43) {
            if (box.choice() == 1) {
                box.close();
                box.open(0x44, cur + 1);
                gFs.state = 0xc;
                mcStop();
            } else {
                box.close();
                helpBar().clear(0);
            }
        } else if (id == 0x46) {
            box.close();
            sound().playSe(1.0f, 1);
            helpBar().clear(0);
        }
    }
    // orig FUN_00205e20 (work/decompiled/00205e20.c): in-game save flow
    void decideSave() {
        if (saveManager().busy != 0) return;
        MessageBox& box = messageBox();
        const uint32_t pressed = pad().port[0].pressed;
        if (!box.isOpen()) {
            if ((pressed & 0x40) == 0) {
                if ((pressed & 0x20) != 0) close();
            } else if (status_[gFs.cursor] == 1) {
                box.open(0x22, gFs.cursor + 1);
            } else {
                box.open(0x23, gFs.cursor + 1);
            }
            return;
        }
        const int id = box.currentId();
        if (static_cast<unsigned>(id - 0x22) < 2u) {
            if (box.choice() == 1) {
                box.close();
                box.open(0x26, gFs.cursor + 1);
                gFs.state = 9;
                mcStop();
            } else if (box.choice() == 2) {
                box.close();
            }
        } else if (id == 0x27) {
            box.close();
            helpBar().clear(0);
        } else if (id == 0xd) {
            gFs.state = 1;
            box.close();
            sound().playSe(1.0f, 1);
        }
    }

    // ---- sprites ----------------------------------------------------------------------------------------------
    // orig FUN_00201e20 (work/decompiled/00201e20.c; stack arguments from the disassembly)
    void build() {
        textures().load(0x2f);
        float yofs = -170.0f;
        for (int s = 0; s < 3; ++s) {
            for (int i = 0; i < 0x42; ++i) {
                const float* p = kFileSlotPos[i];
                const float* uv = kFileSlotUv[i];
                slot_[s][i] = sprites().create(p[0], yofs + p[1], 2.0f, p[2], p[3], uv[0], uv[1], uv[2], uv[3], 0x2f, 0);
            }
            back_[s] = sprites().create(88.0f, yofs + 251.0f, 2.0f, 349.0f, 60.0f, 50.0f, 42.0f, 58.0f, 50.0f, 0x27, 1);
            const float* cp = kFileCursorPos[s];
            const float* cu = kFileCursorUv[s];
            label_[s] = sprites().create(cp[0], cp[1], 2.1f, cp[2], cp[3], cu[0], cu[1], cu[2], cu[3], 0x2f, 0);
            yofs += 115.0f;
        }
        highlight_ = sprites().create(76.0f, 84.0f, 1.0f, 373.0f, 72.0f, 0.2f, 1.0f, 31.8f, 32.0f, gradientTextureId(), 1);
        if (language() != 1) return;
        textures().load(0x22);
        for (int s = 0; s < 3; ++s) {
            const float y0 = static_cast<float>(s) * 115.0f + 80.0f;
            const float y1 = y0 + 22.0f;
            jp_.diamond[s] = sprites().create(100.0f, y1, 3.0f, 16.0f, 14.0f, 71.0f, 62.0f, 87.0f, 76.0f, 0x23, 1);
            jp_.lvLabel[s] = sprites().create(214.0f, y1, 3.0f, 67.0f, 17.0f, 0.0f, 0.0f, 67.0f, 17.0f, 0x30, 1);
            jp_.difficulty[s] = sprites().create(384.0f, y1, 3.0f, 0.0f, 12.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0x22, 1);
            const float y2 = y0 + 44.0f;
            jp_.depthLabel[s] = sprites().create(100.0f, y2, 3.0f, 40.0f, 14.0f, 87.0f, 62.0f, 127.0f, 76.0f, 0x23, 1);
            jp_.dungeon[s] = sprites().create(214.0f, y2, 3.0f, 35.0f, 17.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0x30, 1);
            for (int j = 0; j < 0x13; ++j)
                jp_.date[s][j] = sprites().create(static_cast<float>(j * 8) + 214.0f + 64.0f, y0, 3.0f, 8.0f, 12.0f, 0, 0, 0, 0, 0x23, 1);
            for (int j = 0; j < 2; ++j)
                jp_.level[s][j] = sprites().create(static_cast<float>(j * 8) + 120.0f, y1, 3.0f, 8.0f, 12.0f, 0, 0, 0, 0, 0x23, 1);
            for (int j = 0; j < 5; ++j)
                jp_.bag[s][j] = sprites().create(static_cast<float>(j * 8) + 284.0f, y1, 3.0f, 8.0f, 12.0f, 0, 0, 0, 0, 0x23, 1);
            for (int j = 0; j < 6; ++j)
                jp_.depth[s][j] = sprites().create(static_cast<float>(j * 8) + 144.0f, y2, 3.0f, 8.0f, 12.0f, 0, 0, 0, 0, 0x23, 1);
        }
    }
    // orig FUN_00202500 (work/decompiled/00202500.c)
    void destroyAll() {
        sprites().destroyRef(highlight_);
        for (int s = 0; s < 3; ++s) {
            for (Sprite*& p : slot_[s]) sprites().destroyRef(p);
            sprites().destroyRef(back_[s]);
            sprites().destroyRef(label_[s]);
        }
        textures().release(0x2f);
        if (language() != 1) return;
        textures().release(0x22);
        for (int s = 0; s < 3; ++s) {
            sprites().destroyRef(jp_.diamond[s]);
            sprites().destroyRef(jp_.lvLabel[s]);
            sprites().destroyRef(jp_.difficulty[s]);
            sprites().destroyRef(jp_.depthLabel[s]);
            sprites().destroyRef(jp_.dungeon[s]);
            for (Sprite*& p : jp_.date[s]) sprites().destroyRef(p);
            for (Sprite*& p : jp_.level[s]) sprites().destroyRef(p);
            for (Sprite*& p : jp_.bag[s]) sprites().destroyRef(p);
            for (Sprite*& p : jp_.depth[s]) sprites().destroyRef(p);
        }
    }
    // orig FUN_00203060 (work/decompiled/00203060.c): JP digits of a deleted slot become invisible
    void hideSlotDigits(int s) {
        auto hide = [](Sprite* p) { if (p != nullptr) p->color = 0; };
        hide(jp_.diamond[s]);
        hide(jp_.lvLabel[s]);
        hide(jp_.difficulty[s]);
        hide(jp_.depthLabel[s]);
        hide(jp_.dungeon[s]);
        for (Sprite* p : jp_.date[s]) hide(p);
        for (Sprite* p : jp_.level[s]) hide(p);
        for (Sprite* p : jp_.bag[s]) hide(p);
        for (Sprite* p : jp_.depth[s]) hide(p);
    }

    // ---- draw -------------------------------------------------------------------------------------------------
    // orig FUN_002040a0 (work/decompiled/002040a0.c)
    void draw() {
        angle_ += 8;
        if (0x168 < angle_) angle_ = -360;
        // two FUN_00206940 calls whose results are unused
        if (alpha_ < 0x40 && fade().finished()) {
            alpha_ += 6;
            if (0x40 < alpha_) alpha_ = 0x40;
        }
        if (!helpBar().isSet(0) && alpha_ == 0x40) {
            helpBar().set(32.0f, 403.0f, 420.0f, 40.0f, 0, str(gFs.flow == 1 ? 0x15 : 0x16));
            helpBar().setSpeed(0, 4);
        }
        const bool boxOpen = messageBox().isOpen();
        if (highlight_ != nullptr) {
            highlight_->color = boxOpen ? 0u : (static_cast<uint32_t>(alpha_) << 24) + 0x7f7f7f;
            highlight_->hh = 45.0f;
            highlight_->cy = static_cast<float>(gFs.cursor) * 115.0f + highlight_->hh + 66.0f + 0.0f;
        }
        if (!boxOpen) {
            if (0 < dialogAlpha_) {
                dialogAlpha_ -= 4;
                if (dialogAlpha_ < 0) dialogAlpha_ = 0;
            }
        } else {
            dialogAlpha_ += 4;
            if (0x40 < dialogAlpha_) dialogAlpha_ = 0x40;
        }
        const int cur = gFs.cursor;
        for (int s = 0; s < 3; ++s) {
            int a;
            Sprite* lab = label_[s];
            if (!boxOpen) {
                a = 0x40 - dialogAlpha_;
                if (lab != nullptr) {
                    lab->u0 = kFileCursorUv[s][0] / 128.0f;
                    lab->v0 = kFileCursorUv[s][1] / 128.0f;
                    lab->u1 = kFileCursorUv[s][2] / 128.0f;
                    lab->v1 = kFileCursorUv[s][3] / 128.0f;
                }
            } else {
                a = 0;
                if (s == 0) {                              // the first panel shows the selected file during a dialog
                    a = static_cast<int>(alpha_);
                    if (lab != nullptr) {
                        lab->u0 = kFileCursorUv[cur][0] / 128.0f;
                        lab->v0 = kFileCursorUv[cur][1] / 128.0f;
                        lab->u1 = kFileCursorUv[cur][2] / 128.0f;
                        lab->v1 = kFileCursorUv[cur][3] / 128.0f;
                    }
                }
            }
            const uint32_t c = static_cast<uint32_t>(a) * 0x1000000u + 0x7f7f7f;
            for (Sprite* p : slot_[s])
                if (p != nullptr) p->color = c;
            if (back_[s] != nullptr) back_[s]->color = c;
            if (lab != nullptr) lab->color = c;
        }
        drawSlots();                                       // FUN_00204470
        text().layerFlag = 1;
        text().color = kTextDefault;
    }
    // orig FUN_00204470 (work/decompiled/00204470.c)
    void drawSlots() {
        TextRenderer& t = text();
        const bool boxOpen = messageBox().isOpen();
        const int cur = gFs.cursor;
        for (int s = 0; s < 3; ++s) {
            if (gFs.state == 1) continue;
            const int st = status_[s];
            if (st == 1) {
                int a = static_cast<int>(alpha_);
                float cx = 0.0f, cy = 0.0f;
                t.halfW = kScaleW * 8.0f;
                t.halfH = kScaleH * 9.0f;
                centreOnScreen(t.measure("%s", str(0x1b)), 0.0f, &cx, &cy);
                if (!boxOpen) {
                    t.x = cx;
                    t.z = 3.0f;
                    t.y = static_cast<float>(s) * 115.0f + 104.0f;
                    a = 0x40 - dialogAlpha_;
                } else {
                    t.y = 104.0f;
                    t.x = cx;
                    t.z = 3.0f;
                    if (s != cur) a = 0;
                }
                t.color = (cur == s) ? static_cast<uint32_t>(a) * 0x1000000u + 0x22227f : kTextRgb + static_cast<uint32_t>(a) * 0x1000000u;
                t.draw("%s", str(0x1b));
            } else if (st == 2) {
                drawSlotData(s);                           // FUN_00204820
            } else if (st == 3) {
                int a = static_cast<int>(alpha_);
                float cx = 0.0f, cy = 0.0f;
                centreOnScreen(t.measure("%s", str(0x1c)), 0.0f, &cx, &cy);
                if (!boxOpen) {
                    t.y = static_cast<float>(s) * 115.0f + 104.0f;
                    a = 0x40 - dialogAlpha_;
                } else {
                    t.y = 104.0f;
                    if (s != cur) a = 0;
                }
                t.x = cx;
                t.z = 3.0f;
                t.color = (cur == s) ? static_cast<uint32_t>(a) * 0x1000000u + 0x22227f : kTextRgb + static_cast<uint32_t>(a) * 0x1000000u;
                t.halfW = 8.0f;
                t.halfH = 9.0f;
                t.draw("%s", str(0x1c));
            }
        }
    }
    // the depth value printed for a slot (shared by both layouts of FUN_00204820); returns false for "DEPTH 0"
    static bool depthValue(const SlotSummary& m, int* value) {
        const int fl = m.floor;
        if (fl == 99 && m.dungeonType == 1) {
            *value = 99999;
            return true;
        }
        if (fl == 0x5b && (m.dungeonType < 3 || 9 < m.dungeonType)) return false;
        *value = (m.dungeonType == 1) ? fl * 100 : (fl + 1) * 100;
        return true;
    }
    // orig FUN_00204820 (work/decompiled/00204820.c): one saved game
    void drawSlotData(int s) {
        TextRenderer& t = text();
        const SlotSummary& m = saveManager().summary[s];
        const bool boxOpen = messageBox().isOpen();
        const int cur = gFs.cursor;
        int a = static_cast<int>(alpha_);
        bool moved = false;                                // bVar1: the selected file drawn in the first panel
        const float y0 = static_cast<float>(s) * 115.0f;
        float y = y0 + 80.0f;
        if (!boxOpen || s == cur) {
            if (!boxOpen || s != cur) {
                if (!boxOpen) a = 0x40 - dialogAlpha_;
            } else {
                a = dialogAlpha_;
                y = 80.0f;
                moved = true;
            }
        } else {
            a = 0;
        }
        float xBase = 0.0f;
        if (language() == 1) {
            t.halfW = 8.0f;
            t.halfH = 9.0f;
            t.spacing = 1.0f;
        } else if (language() == 0) {
            t.halfW = 9.6f;                                // 0x4119999a
            t.halfH = 10.8f;                               // 0x412ccccd
            xBase = 138.0f;
            t.spacing = 1.0f;
        }
        const uint32_t aa = static_cast<uint32_t>(a) << 24;
        t.x = 92.0f;
        t.z = 3.0f;
        t.color = (cur == s) ? aa + 0x22227f : kTextRgb + aa;
        t.y = y;
        t.draw("%s", m.name);                              // 0x509848 + slot*0x38
        t.x = 176.0f;
        t.color = kTextRgb + aa;
        int lv = m.level;                                  // +0x14
        t.z = 3.0f;
        t.y = y;
        if (lv < 1) {
            t.draw("\x81|%d", 1);                          // 0x469730 (full-width minus)
        } else {
            if (99 < lv) lv = 99;
            t.draw("\x81{%d", lv);                         // 0x469728 (full-width plus)
        }
        if (language() == 1) {
            drawSlotDataJp(s, m, y0, moved, aa);
        } else if (language() == 0) {
            t.halfW = 7.6f;                                // 0x40f33333
            y += 4.0f;
            t.halfH = 9.0f;
            t.spacing = 1.0f;
            t.z = 3.0f;
            t.x = xBase + 92.0f + 12.0f;
            t.y = y;
            t.draw("%0.4d\x81^%0.2d\x81^%0.02d %0.2d\x81" "F%0.2d\x81" "F%0.2d", m.time[5] + 2000, m.time[4], m.time[3], m.time[2],
                   m.time[1], m.time[0]);                  // 0x4697b0
            t.x = 100.0f;
            const float y2 = y + 20.0f;
            t.z = 3.0f;
            t.y = y2;
            t.draw("%s %d", str(0x29), m.hasChar);         // 0x4697e0, +0x18
            const float x2 = xBase + 92.0f;
            t.z = 3.0f;
            t.x = x2;
            t.y = y2;
            t.draw(str(0x28));                             // "BAG"
            t.draw(" %d\x81^%d", m.countA, m.countB);      // 0x4697e8
            t.x = 334.0f;
            t.z = 3.0f;
            if (m.achievement < 4) {
                t.y = y2;
                t.draw("%s", str(m.achievement + 0x2a));
            }
            t.x = 100.0f;
            y += 40.0f;
            t.z = 3.0f;
            t.y = y;
            int depth = 0;
            if (depthValue(m, &depth)) t.draw("%s \x81|%d", str(0x1d), depth);   // 0x4697f0
            else t.draw("%s %d", str(0x1d), 0);
            t.z = 3.0f;
            t.x = x2;
            t.y = y;
            if (-1 < m.dungeonType && m.dungeonType < 10) t.draw("%s", str(m.dungeonType + 0x1e));
        }
        if (language() == 1 || language() == 0) t.spacing = 1.0f;
    }
    // the JP half of FUN_00204820: sprite digits from TEX_UI00 and label sprites
    void drawSlotDataJp(int s, const SlotSummary& m, float y0, bool moved, uint32_t aa) {
        const uint32_t c = aa + 0x7f7f7f;
        auto colorY = [&](Sprite* p, float dy) {
            if (p == nullptr) return;
            p->color = c;
            p->cy = moved ? p->hh + dy : y0 + dy + p->hh;
        };
        colorY(jp_.diamond[s], 103.0f);
        colorY(jp_.lvLabel[s], 103.0f);
        colorY(jp_.difficulty[s], 107.0f);
        colorY(jp_.depthLabel[s], 126.0f);
        colorY(jp_.dungeon[s], 126.0f);
        if (m.achievement < 4) {
            if (Sprite* p = jp_.difficulty[s]) {
                const float* uv = kFileDifficultyUv[m.achievement];
                p->u0 = uv[0] / 128.0f;
                p->v0 = uv[1] / 64.0f;
                p->u1 = uv[2] / 128.0f;
                p->v1 = uv[3] / 64.0f;
                p->hw = kFileDifficultyWidth[m.achievement] * 0.5f;
            }
        } else {
            // 0x469740: the original passes no argument for %d (a2 is not set before the call).
            // UNVERIFIED: the port passes the level value so the format has a defined argument.
            text().draw(" - ERROR : SaveData::m_gameLevel %d\n", m.achievement);   // PORT: the original says "MemCard::"
        }
        if (Sprite* p = jp_.dungeon[s]) {
            if (m.dungeonType < 0 || 9 < m.dungeonType) {
                p->u0 = 0; p->v0 = 0; p->u1 = 0; p->v1 = 0;
            } else {
                const float* uv = kFileDungeonIconUv[m.dungeonType];
                p->u0 = uv[0] / 128.0f;
                p->v0 = uv[1] / 128.0f;
                p->u1 = uv[2] / 128.0f;
                p->v1 = uv[3] / 128.0f;
            }
        }
        char buf[24] = {};
        std::snprintf(buf, sizeof buf, "%0.4d/%0.2d/%0.02d %0.2d:%0.2d:%0.2d", m.time[5] + 2000, m.time[4], m.time[3], m.time[2],
                      m.time[1], m.time[0]);               // 0x469770
        for (int j = 0; j < 0x13; ++j) {
            Sprite* p = jp_.date[s][j];
            setDigitGlyph(p, buf[j]);
            if (p == nullptr) continue;
            p->color = c;
            p->cy = moved ? p->hh + 83.0f + 1.0f : y0 + 83.0f + p->hh + 1.0f;
        }
        const float one = 1.0f;
        char lv[3] = {};
        std::snprintf(lv, sizeof lv, "%d", m.hasChar);    // 0x469798, +0x18
        for (int j = 0; j < 2; ++j) {
            Sprite* p = jp_.level[s][j];
            setDigitGlyph(p, lv[j]);
            if (p == nullptr) continue;
            p->color = c;
            p->cy = moved ? one + 103.0f + p->hh : one + (103.0f + y0) + p->hh;
        }
        char bag[6] = {};
        std::snprintf(bag, sizeof bag, "%d/%d", m.countA, m.countB);   // 0x4697a0
        for (int j = 0; j < 5; ++j) {
            Sprite* p = jp_.bag[s][j];
            setDigitGlyph(p, bag[j]);
            if (p == nullptr) continue;
            p->color = c;
            p->cy = moved ? one + p->hh + 106.0f : one + (y0 + 106.0f) + p->hh;
        }
        char dep[7] = {};
        int depth = 0;
        if (depthValue(m, &depth)) std::snprintf(dep, sizeof dep, "-%d", depth);   // 0x4697a8
        else std::snprintf(dep, sizeof dep, "%d", 0);
        for (int j = 0; j < 6; ++j) {
            Sprite* p = jp_.depth[s][j];
            setDigitGlyph(p, dep[j]);
            if (p == nullptr) continue;
            p->color = c;
            p->cy = moved ? one + p->hh + 126.0f : one + y0 + 126.0f + p->hh;
        }
    }

    struct JpSprites {                                     // created for language 1 only
        Sprite* date[3][0x13] = {};                        // +0x7c8 (slot stride 0x4c)
        Sprite* diamond[3] = {};                           // +0x8ac
        Sprite* level[3][2] = {};                          // +0x8b8
        Sprite* lvLabel[3] = {};                           // +0x8d0
        Sprite* bag[3][5] = {};                            // +0x8dc
        Sprite* difficulty[3] = {};                        // +0x918
        Sprite* depthLabel[3] = {};                        // +0x924
        Sprite* depth[3][6] = {};                          // +0x930
        Sprite* dungeon[3] = {};                           // +0x978
    };
    int wantsNext_ = 0;              // +0x470
    int angle_ = 0;                  // +0x474
    unsigned alpha_ = 0;             // +0x478
    int dialogAlpha_ = 0;            // +0x47c
    int mcState_ = 0;                // +0x480
    int delay_ = 0;                  // +0x484
    int status_[3] = {};             // +0x488: 1 empty ("NEW DATA"), 2 game file, 3 corrupt
    Sprite* highlight_ = nullptr;    // +0x494
    Sprite* slot_[3][0x42] = {};     // +0x498 (slot stride 0x108)
    Sprite* back_[3] = {};           // +0x7b0
    Sprite* label_[3] = {};          // +0x7bc
    JpSprites jp_;
    int gameMapHidden_ = 0;          // +0x984
};
}  // namespace
// orig FUN_00166a50 (work/decompiled/00166a50.c): digit / punctuation glyph of TEX_UI00 (JP slot layout)
void setDigitGlyph(Sprite* s, char c) {
    if (s == nullptr) return;
    s->u0 = 0; s->v0 = 0; s->u1 = 0; s->v1 = 0;
    if (c == '/' || c == ' ' || c == '-' || c == ':' || c == '%' || c == 'U' || c == 'D' || c == 'K' || c == '\0') {
        if (c == '/') { s->u0 = 0.546875f; s->v0 = 0; s->u1 = 0.5875f; s->v1 = 0.09375f; s->hw = 2.5f; s->hh = 6.0f; }
        else if (c == ' ') { s->u0 = 0; s->v0 = 0; s->u1 = 0; s->v1 = 0; s->hw = 3.5f; s->hh = 6.0f; }
        else if (c == '-') { s->u0 = 0.5859375f; s->v0 = 0; s->u1 = 0.6171875f; s->v1 = 0.09375f; }
        else if (c == ':') { s->u0 = 0.9609375f; s->v0 = 0; s->u1 = 1.0f; s->v1 = 0.09375f; s->hw = 3.5f; s->hh = 6.0f; }
        else if (c == '%') { s->u0 = 0.9140625f; s->v0 = 0.59375f; s->u1 = 0.984375f; s->v1 = 0.6875f; s->hw = 3.5f; s->hh = 6.0f; }
        else if (c == 'U') { s->u0 = 0.875f; s->v0 = 0; s->u1 = 0.9609375f; s->v1 = 0.09375f; s->hw = 5.0f; s->hh = 6.0f; }
        else if (c == 'D') { s->u0 = 0.7890625f; s->v0 = 0; s->u1 = 0.875f; s->v1 = 0.09375f; s->hw = 5.0f; s->hh = 6.0f; }
        else if (c == 'K') { s->u0 = 0.6328125f; s->v0 = 0; s->u1 = 0.6796875f; s->v1 = 0.09375f; s->hw = 3.5f; s->hh = 6.0f; }
    } else {
        const int d = c - 0x30;
        if (d < 1 || 9 < d) {
            s->u0 = 0.4921875f; s->v0 = 0; s->u1 = 0.546875f; s->v1 = 0.09375f;   // '0' (and anything else)
        } else {
            const float u = static_cast<float>(c - 0x31) * 7.0f;
            s->u0 = u / 128.0f; s->v0 = 0; s->u1 = (u + 7.0f) / 128.0f; s->v1 = 0.09375f;
        }
        s->hw = 3.5f;
        s->hh = 6.0f;
    }
}
FileSelectGlobals& fileSelect() { return gFs; }
MenuPage* newFileSelectPage() { return new FileSelectPage(); }   // FUN_00168fc0 (alloc FUN_00169390 + ctor FUN_002017d0)
}  // namespace baroque::game::ui
