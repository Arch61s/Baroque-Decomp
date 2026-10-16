// GAME CONFIG page (vtable 0x4cdca0; ctor FUN_0015d7f0; open FUN_0015dcd0, close FUN_0015e150, is-active 0x163a00,
// update FUN_0015e8c0, input FUN_0015fa70, wants-next 0x163a10 (+0x470), next page FUN_0015e840).
#include "game/ui/menu.hpp"
#include "game/event/event_manager.hpp"
#include "game/hud/hud_api.hpp"
#include "game/fade.hpp"
#include "game/options.hpp"
#include "game/pad.hpp"
#include "game/scene_manager.hpp"
#include "game/sound_stub.hpp"
#include "game/ui/help_bar.hpp"
#include "game/ui/message_box.hpp"
#include "game/ui/mouse_menu.hpp"
#include "game/ui/sprite.hpp"
#include "game/ui/strings.hpp"
#include "game/ui/text.hpp"
#include "game/ui/ui_tables.hpp"
#include <cmath>
namespace baroque::game::ui {
namespace {
constexpr uint32_t kDim = 0x3f3f3f;        // literal used for inactive values
constexpr uint32_t kText = 0xbbbbbb;       // DAT_0044c190
constexpr uint32_t kTextDefault = 0x40bbbbbb; // DAT_0044c198
constexpr float kHelpScaleW = 1.16f;       // DAT_0044c180
constexpr float kHelpScaleH = 1.22f;       // DAT_0044c188
// PORT (PC SETTINGS row, user decision 2026-10-10): GAME CONFIG lists 11 items. Item id 10 = PC SETTINGS (opens the
// port's page_pc_settings.cpp) is shown between CAMERA SETTING (8) and Default Settings (9), styled like KEY ASSIGN /
// BRIGHTNESS / CAMERA SETTING (label + grey arrow). The original 10 rows (kConfigRowPos: x 32, y 79 + 31 i) are laid out
// at a 28-unit pitch from y 77 so the 11 rows still end above the help bar; bars / knobs / arrows keep their original
// offsets from their row (bar +6, knob +3, arrow +0). Item ids, help strings and handlers are unchanged.
constexpr int kPcItem = 10;
constexpr const char* kPcLabel = "PC SETTINGS";
constexpr const char* kPcHelp = "Display, mouse and keyboard settings";
int rowSlot(int id) { return id == kPcItem ? 9 : (id == 9 ? 10 : id); }
float rowX(int id) { return kConfigRowPos[id == kPcItem ? 6 : id][0]; }   // x 32 (the table has 10 rows)
float rowY(int id) { return 77.0f + 28.0f * static_cast<float>(rowSlot(id)); }
signed char nextItem(int id) { return static_cast<signed char>(id == 8 ? kPcItem : (id == kPcItem ? 9 : (id >= 9 ? 0 : id + 1))); }
signed char prevItem(int id) { return static_cast<signed char>(id == 0 ? 9 : (id == 9 ? kPcItem : (id == kPcItem ? 8 : id - 1))); }
class GameConfigPage final : public MenuPage {
public:
    GameConfigPage() {   // orig FUN_0015d7f0 (work/decompiled/0015d7f0.c)
        isOpen = 0; wantsNext_ = 0; angle_ = 0; alpha_ = 0;
        if (options().cursorPos == 1) gConfigCursor = 0;
        cameraCursor_ = 0;
    }
    const char* debugName() const override { return "GAME CONFIG"; }
    // orig FUN_0015dcd0 (work/decompiled/0015dcd0.c)
    void open() override {
        makeFrame();
        makeTitle(str(1));
        if (scenes().isCurrent("Game....")) {
            makeGameBackdrop();                            // FUN_00164910
            if (!hudMapShown()) {
                mapHidden_ = 0;
            } else {
                hudMapHide();                              // FUN_00126fb0(0x50a350)
                mapHidden_ = 1;                            // +0x510
            }
            sound().setBgmFadeTarget(1.0f, 4);             // FUN_00115f00(1.0, snd, 4)
        }
        isOpen = 1; wantsNext_ = 0; angle_ = 0; alpha_ = 0;
        if (options().cursorPos == 1) gConfigCursor = 0;
        selected_ = -1;
        for (int i = 0; i < 9; ++i) {
            const float* p = kConfigBarPos[i];
            const float* uv = kConfigBarUv[i];
            // PORT: y p[1] (147) / +31 / +62 -> rowY(2..4) + 6 (11-row layout)
            bars_[0][i] = sprites().create(p[0] - 16.0f, rowY(2) + 6.0f, 1.0f, p[2], p[3], uv[0], uv[1], uv[2], uv[3], 0x2e, 1);
            bars_[1][i] = sprites().create(p[0] - 16.0f, rowY(3) + 6.0f, 1.0f, p[2], p[3], uv[0], uv[1], uv[2], uv[3], 0x2e, 1);
            bars_[2][i] = sprites().create(p[0] - 16.0f, rowY(4) + 6.0f, 1.0f, p[2], p[3], uv[0], uv[1], uv[2], uv[3], 0x2e, 1);
        }
        const float kx = kConfigBarPos[0][0] - 16.0f;
        // PORT: knob y 144 / 175 / 206 -> rowY(2..4) + 3, arrow y kConfigRowPos[6 + i][1] -> rowY(6 + i) (11-row layout)
        knob_[0] = sprites().create(kx, rowY(2) + 3.0f, 2.0f, 6.0f, 15.0f, 115.0f, 0.0f, 121.0f, 15.0f, 0x30, 1);   // v1 UNVERIFIED (stack arg)
        knob_[1] = sprites().create(kx, rowY(3) + 3.0f, 2.0f, 6.0f, 15.0f, 115.0f, 0.0f, 121.0f, 15.0f, 0x30, 1);
        knob_[2] = sprites().create(kx, rowY(4) + 3.0f, 2.0f, 6.0f, 15.0f, 115.0f, 0.0f, 121.0f, 15.0f, 0x30, 1);
        for (int i = 0; i < 3; ++i)
            arrow_[i] = sprites().create(296.0f, rowY(6 + i), 1.0f, 10.0f, 13.0f, 105.0f, 0.0f, 115.0f, 13.0f, 0x30, 1);
        pcArrow_ = sprites().create(296.0f, rowY(kPcItem), 1.0f, 10.0f, 13.0f, 105.0f, 0.0f, 115.0f, 13.0f, 0x30, 1);   // PORT
        cameraCursor_ = 1;
        makeFrame2(0);
        sound().setMenuDuck(1);   // FUN_001161d0(snd, 1)
    }
    // orig FUN_0015e150 (work/decompiled/0015e150.c)
    void close() override {
        freeFrame2();
        helpBar().clear(0);
        freeFrame();
        isOpen = 0;
        freeSprites();   // FUN_0015e280
        if (scenes().isCurrent("Game....")) {
            sound().setBgmFadeTarget(0.7f, 4);             // FUN_00115f00(0x3f333333, snd, 4)
            if (mapHidden_ != 0) hudMapShow();             // FUN_00126cc0(0x50a350)
            // iRam00517b68 (item screen open) -> uRam0051aa1c = uRam0051aa20 = 1: help slots 1 / 2 enabled again
            if (itemScreen().isOpen()) { helpBar().setEnabled(1, 1); helpBar().setEnabled(2, 1); }
        }
    }
    // orig FUN_0015e8c0 (work/decompiled/0015e8c0.c)
    void update() override {
        if (isOpen == 0) return;
        animateTitle();
        if (titleSettled != 0 && isOpen != 0) { drawItems(); defaultsDialog(); }
    }
    // orig FUN_0015fa70 (work/decompiled/0015fa70.c)
    void input() override {
        const PadPort& p = pad().port[0];
        if (p.pressed & 0x20) {
            if (isOpen == 0 || selected_ != -1) cancel();   // FUN_00160930
            else leave();                                    // FUN_0015fb90
        } else if (p.pressed & 0x40) confirm();              // FUN_00160680
        else if (p.repeat & (0x4000 | 0x20000)) cursorDown(); // FUN_0015fd70
        else if (p.repeat & (0x1000 | 0x10000)) cursorUp();   // FUN_0015fe80
        else if (p.repeat & (0x8000 | 0x40000)) adjust(-1);   // FUN_0015ff80
        else if (p.repeat & (0x2000 | 0x80000)) adjust(+1);   // FUN_00160360
    }
    // PORT: mouse menu layer. Hover on a row = the Down step of cursorDown() onto it (help text + SE 6) while no item
    // is being edited; in the camera window (item 8) hover moves the camera cursor the same way.
    void mouseInput() override {
        if (isOpen == 0) return;
        const int h = mouse::hovered(this);
        if (h < 0) return;
        if (selected_ == -1 && !messageBox().isOpen() && h <= kPcItem && h != gConfigCursor) {
            gConfigCursor = prevItem(h);
            cursorDown();
        } else if (selected_ == 8 && h >= 101 && h <= 103 && h - 100 != cameraCursor_) {
            cameraCursor_ = (h == 101) ? 3 : h - 101;
            cursorDown();
        }
    }
    // PORT: the eleven rows of drawItems() (labels at rowY, values up to the bar end x 471): a click is Cross
    // (select / confirm). While a toggle (items 0, 1, 5) is edited, a click on its value flips it (Right). While a value
    // is edited the wheel is Left/Right. Camera window rows (drawCameraList): label = Cross, value = Right (toggle).
    void mouseItems() override {
        if (isOpen == 0 || titleSettled == 0) return;
        if (!messageBox().isOpen()) {
            for (int i = 0; i <= kPcItem; ++i) {
                const float y = rowY(i);
                const bool toggle = (selected_ == i) && (i == 0 || i == 1 || i == 5);
                if (toggle) {
                    mouse::row(this, i, rowX(i) - 8.0f, 272.0f, y, 28.0f);
                    mouse::row(this, i, 272.0f, 476.0f, y, 28.0f, platform::PAD_RIGHT);
                } else {
                    mouse::row(this, i, rowX(i) - 8.0f, 476.0f, y, 28.0f);
                }
            }
            if (selected_ >= 0 && selected_ <= 5) mouse::wheelHorizontal();
        } else if (selected_ == 8 && messageBox().currentId() == 0x41) {
            const float labelX = (language() == 1) ? 86.0f : 76.0f;
            const float valueX = (language() == 1) ? 208.0f : 268.0f;
            const float rowY[3] = {205.0f, 246.0f, 283.0f};
            for (int r = 0; r < 3; ++r) {
                mouse::row(this, 101 + r, labelX - 8.0f, valueX - 8.0f, rowY[r], 37.0f, mouse::kConfirm, mouse::kLayerDialog);
                mouse::row(this, 101 + r, valueX - 8.0f, 444.0f, rowY[r], 37.0f, platform::PAD_RIGHT, mouse::kLayerDialog);
            }
        }
    }
    bool wantsNext() const override { return wantsNext_ != 0; }
    // orig FUN_0015e840 (work/decompiled/0015e840.c)
    MenuPage* nextPage() override {
        if (selected_ == 6) return newKeyAssignPage();
        if (selected_ == 7) return newBrightnessPage();
        if (selected_ == kPcItem) return newPcSettingsPage(1);   // PORT
        return newOptionPage();
    }
private:
    void setHelp() {
        helpBar().set(32.0f, 403.0f, 420.0f, 40.0f, 0, gConfigCursor == kPcItem ? kPcHelp : str(gConfigCursor + 0x39));   // PORT: item 10
        helpBar().setSpeed(0, 4);
    }
    // orig FUN_0015fb90 (work/decompiled/0015fb90.c): outside the Game scene -> hand over to the OPTION page
    void leave() {
        if (scenes().isCurrent("Game....")) {
            close();
            sound().playSe(1.0f, 8);
            sound().setMenuDuck(0);
            if (scenes().isCurrent("Game....")) eventManager().requestTransition(0.0f, 5);   // FUN_00177cc0(0, 0x51b620, 5)
        }
        else { wantsNext_ = 1; sound().playSe(1.0f, 8); }
        gOptionsChanged = 0;
    }
    // orig FUN_0015fd70 (work/decompiled/0015fd70.c)
    void cursorDown() {
        if (isOpen == 0) return;
        if (selected_ == -1) {
            gConfigCursor = nextItem(gConfigCursor);   // PORT: original + 1, wrap > 9 -> 0; 8 -> 10 -> 9
            setHelp(); sound().playSe(1.0f, 6);
        } else if (selected_ == 8) {
            cameraCursor_ += 1; if (cameraCursor_ > 3) cameraCursor_ = 1;
            sound().playSe(1.0f, 6);
        }
    }
    // orig FUN_0015fe80 (work/decompiled/0015fe80.c)
    void cursorUp() {
        if (isOpen == 0) return;
        if (selected_ == -1) {
            gConfigCursor = prevItem(gConfigCursor);   // PORT: original - 1, wrap < 0 -> 9; 9 -> 10 -> 8
            setHelp(); sound().playSe(1.0f, 6);
        } else if (selected_ == 8) {
            cameraCursor_ -= 1; if (cameraCursor_ < 1) cameraCursor_ = 3;
            sound().playSe(1.0f, 6);
        }
    }
    // orig FUN_0015ff80 (dir -1) / FUN_00160360 (dir +1) (work/decompiled/0015ff80.c, 00160360.c)
    void adjust(int dir) {
        if (isOpen == 0) return;
        Options& o = options();
        SoundManager& snd = sound();
        switch (selected_) {
        case 0: o.cursorPos = (o.cursorPos == 0) ? 1u : 0u; snd.playSe(1.0f, 6); break;
        case 1: o.mono = (o.mono == 0) ? 1u : 0u; snd.setMono(o.mono != 0); snd.playSe(1.0f, 6); break;
        case 2:
            if (dir < 0) { o.bgmVol = static_cast<int8_t>(o.bgmVol - 1); if (o.bgmVol < 0) o.bgmVol = 0; else snd.playSe(1.0f, 6); }
            else { o.bgmVol = static_cast<int8_t>(o.bgmVol + 1); if (o.bgmVol < 11) snd.playSe(1.0f, 6); else o.bgmVol = 10; }
            snd.setBgmVolume(kVolumeTable[o.bgmVol][0]);
            break;
        case 3:
            if (dir < 0) { o.seVol = static_cast<int8_t>(o.seVol - 1); if (o.seVol < 0) o.seVol = 0; else { snd.setSeVolume(kVolumeTable[o.seVol][1]); snd.playSe(1.0f, 6); } }
            else { o.seVol = static_cast<int8_t>(o.seVol + 1); if (o.seVol < 11) { snd.setSeVolume(kVolumeTable[o.seVol][1]); snd.playSe(1.0f, 6); } else o.seVol = 10; }
            break;
        case 4:
            if (dir < 0) { o.voiceVol = static_cast<int8_t>(o.voiceVol - 1); if (o.voiceVol < 0) o.voiceVol = 0; else { snd.setVoiceVolume(kVolumeTable[o.voiceVol][2]); snd.playSe(1.0f, 6); snd.playVoice(1.0f, 0x703); } }
            else { o.voiceVol = static_cast<int8_t>(o.voiceVol + 1); if (o.voiceVol < 11) { snd.setVoiceVolume(kVolumeTable[o.voiceVol][2]); snd.playSe(1.0f, 6); snd.playVoice(1.0f, 0x703); } else o.voiceVol = 10; }
            break;
        case 5:
            o.vibrationOff = (o.vibrationOff == 0) ? 1u : 0u;
            if (o.vibrationOff == 0) buttons().flags |= 1u; else buttons().flags &= ~1u;
            snd.playSe(1.0f, 6);
            break;
        case 8: toggleCamera(); break;   // FUN_00160280
        default: break;
        }
    }
    // orig FUN_00160280 (work/decompiled/00160280.c)
    void toggleCamera() {
        uint32_t& c = options().camera;
        if (cameraCursor_ == 1) c ^= 1u;
        else if (cameraCursor_ == 2) c ^= 2u;
        else if (cameraCursor_ == 3) c ^= 4u;
        sound().playSe(1.0f, 6);
    }
    // orig FUN_00160680 (work/decompiled/00160680.c)
    void confirm() {
        if (isOpen == 0) return;
        if (selected_ == -1) {
            switch (gConfigCursor) {
            case 0: case 1: case 2: case 3: case 4: case 5: selected_ = gConfigCursor; sound().playSe(1.0f, 1); break;
            case 6: case 7: case kPcItem: selected_ = gConfigCursor; wantsNext_ = 1; sound().playSe(1.0f, 8); break;   // PORT: 10
            case 8: if (!messageBox().isOpen()) { selected_ = gConfigCursor; messageBox().open(0x41, 999999); } break;
            case 9: if (!messageBox().isOpen()) { selected_ = gConfigCursor; messageBox().open(0x40, 999999); } break;
            default: break;
            }
            optionBackupTake();
        } else if (selected_ != 9) {
            if (messageBox().isOpen()) messageBox().close();   // FUN_00160ac0
            selected_ = -1;
            sound().playSe(1.0f, 1);
            optionApply();
            gOptionsChanged = 1;
        }
    }
    // orig FUN_00160930 (work/decompiled/00160930.c)
    void cancel() {
        if (isOpen == 0 || selected_ == -1 || selected_ == 9) return;
        if (messageBox().isOpen()) messageBox().close();
        selected_ = -1;
        sound().playSe(1.0f, 2);
        optionBackupRestore();
        optionApply();
    }
    // orig FUN_0015e920 (work/decompiled/0015e920.c)
    void defaultsDialog() {
        if (selected_ != 9) return;
        const int c = messageBox().choice();
        if (c == 1) {
            optionDefaults();
            options().initialized = 1;
            optionApply();
            messageBox().close();
            selected_ = -1;
            gOptionsChanged = 1;
        } else if (c == 2) { selected_ = -1; messageBox().close(); }
    }
    static uint32_t col(uint32_t rgb, int alpha) { return rgb + (static_cast<uint32_t>(alpha) << 24); }
    // value pair "A / B" at x = 280 on row `row`, highlighted according to `current`
    void drawToggle(int row, int idx, int current, int strA, int strB, int blink) {
        TextRenderer& t = text();
        const uint32_t base = (selected_ == idx) ? kText : kDim;
        t.halfW = 8.0f; t.halfH = 9.0f; t.x = 280.0f; t.z = 0.9f; t.y = rowY(row);   // PORT: kConfigRowPos[row][1]
        const bool active = (selected_ == idx);
        // first value
        if (current == 0) t.color = active ? col(0x22227f, blink) : col(0x22227f, alpha_);
        else t.color = col(base, alpha_);
        t.draw("%s", str(strA));
        t.color = col(base, alpha_);
        t.draw("\x81^");   // 0x44c110 = full-width slash
        if (current == 0) t.color = col(base, alpha_);
        else t.color = active ? col(0x22227f, blink) : col(0x22227f, alpha_);
        t.draw("%s", str(strB));
    }
    // orig FUN_0015e9d0 (work/decompiled/0015e9d0.c)
    void drawItems() {
        angle_ += 0xc;
        if (angle_ > 0x168) angle_ = -360;
        float blinkF = (std::sin(static_cast<float>(angle_) * 0.017453292f) + 1.0f) * 32.0f + 32.0f;
        if (blinkF >= 2.1474836e+09f) blinkF -= 2.1474836e+09f;
        const int blink = static_cast<int>(blinkF);
        if (alpha_ < 0x40 && fade().finished()) { alpha_ += 6; if (alpha_ > 0x40) alpha_ = 0x40; }
        else if (!(alpha_ < 0x40)) alpha_ = 0x40;
        TextRenderer& t = text();
        t.layerFlag = 1; t.halfW = 8.0f; t.halfH = 9.0f;
        for (int i = 0; i <= kPcItem; ++i) {   // PORT: + item 10 (PC SETTINGS), y = rowY (kConfigRowPos[i][1])
            t.y = rowY(i); t.x = rowX(i); t.z = 0.9f;
            t.color = (gConfigCursor == i) ? col(0x22227f, alpha_) : col(kText, alpha_);
            t.draw("%s", i == kPcItem ? kPcLabel : str(i + 0x2f));
        }
        const Options& o = options();
        drawToggle(0, 0, static_cast<int>(o.cursorPos), 0x43, 0x44, blink);
        drawToggle(1, 1, static_cast<int>(o.mono), 0x45, 0x46, blink);
        for (int r = 0; r < 3; ++r) for (int i = 0; i < 9; ++i) if (bars_[r][i]) bars_[r][i]->color = col(0x7f7f7f, alpha_);
        const int8_t vols[3] = {o.bgmVol, o.seVol, o.voiceVol};
        for (int r = 0; r < 3; ++r) {
            const uint32_t base = (selected_ == 2 + r) ? kText : kDim;
            if (knob_[r]) {
                knob_[r]->cx = static_cast<float>(vols[r]) * 0.1f * 160.0f + kConfigBarPos[0][0] + -16.0f + 8.0f + 0.0f;
                knob_[r]->color = (selected_ == 2 + r) ? col(base, blink) : col(base, alpha_);
            }
        }
        for (Sprite* a : arrow_) if (a) a->color = col(kDim, alpha_);
        if (pcArrow_) pcArrow_->color = col(kDim, alpha_);   // PORT
        drawToggle(5, 5, static_cast<int>(o.vibrationOff), 0x47, 0x48, blink);
        if (messageBox().ready() && messageBox().currentId() == 0x41) {   // FUN_001ab3f0 && iRam0053d73c == 0x41
            if (selected_ == 8) drawCameraList(blink);
            t.layerFlag = 1;
        }
        if (!helpBar().isSet(0) && alpha_ >= 0x40) setHelp();
        t.layerFlag = 1;
        t.color = kTextDefault;
    }
    // camera sub-list (message window 0x41): the remainder of FUN_0015e9d0
    void drawCameraList(int blink) {
        TextRenderer& t = text();
        const float labelX = (language() == 1) ? 86.0f : 76.0f;    // 0x42ac0000 / 0x42980000
        const float valueX = (language() == 1) ? 208.0f : 268.0f;  // 0x43500000 / 0x43860000
        t.layerFlag = 1;
        t.halfW = kHelpScaleW * 8.0f; t.halfH = kHelpScaleH * 9.0f;
        t.color = col(kText, alpha_);
        float cx = 0.0f, cy = 144.0f;   // 0x43100000
        centreOnScreen(t.measure("%s", str(0x37)), 0.0f, &cx, &cy);
        t.x = cx; t.y = cy; t.z = 5.1f;   // 0x40a33333
        t.draw("%s", str(0x37));
        t.halfW = 8.0f; t.halfH = 9.0f;
        const uint32_t cam = options().camera;
        const float rowY[3] = {205.0f, 246.0f, 283.0f};   // 0x434d0000, 0x43760000, 0x438d8000
        const int labelIds[3] = {0x49, 0x4a, 0x4d};
        const int normalIds[3] = {0x4b, 0x4b, 0x4e};
        const int reverseIds[3] = {0x4c, 0x4c, 0x4f};
        for (int r = 0; r < 3; ++r) {
            const bool sel = (cameraCursor_ == r + 1);
            const int hi = sel ? blink : 0x20;
            const int lo = sel ? 0x40 : 0x20;
            t.color = sel ? 0x4022227fu : col(kText, 0x20);
            t.y = rowY[r]; t.z = 5.1f; t.x = labelX;
            t.draw("%s", str(labelIds[r]));
            t.y = rowY[r]; t.z = 5.1f; t.x = valueX;
            const bool reversed = (cam & (1u << r)) != 0;
            if (!reversed) {
                t.color = col(0x22227f, hi); t.draw("%s", str(normalIds[r]));
                t.color = col(kText, lo); t.draw("\x81^"); t.draw("%s", str(reverseIds[r]));
            } else {
                t.color = col(kText, lo); t.draw("%s", str(normalIds[r])); t.draw("\x81^");
                t.color = col(0x22227f, hi); t.draw("%s", str(reverseIds[r]));
            }
        }
    }
    // orig FUN_0015e280 (work/decompiled/0015e280.c)
    void freeSprites() {
        for (auto& row : bars_) for (Sprite*& s : row) sprites().destroyRef(s);
        for (Sprite*& s : knob_) sprites().destroyRef(s);
        for (Sprite*& s : arrow_) sprites().destroyRef(s);
        sprites().destroyRef(pcArrow_);   // PORT
    }
    int wantsNext_ = 0;           // +0x470
    signed char selected_ = -1;   // +0x474
    Sprite* bars_[3][9] = {};     // +0x478 / +0x4a0 / +0x4c8
    Sprite* knob_[3] = {};        // +0x49c / +0x4c4 / +0x4ec
    Sprite* arrow_[3] = {};       // +0x4f0..+0x4f8
    Sprite* pcArrow_ = nullptr;   // PORT: arrow of item 10 (PC SETTINGS)
    int cameraCursor_ = 1;        // +0x4fc
    int mapHidden_ = 0;           // +0x510 (Game scene: the 2-D map was hidden by open)
    int angle_ = 0;               // +0x504
    unsigned alpha_ = 0;          // +0x508
};
}
MenuPage* newGameConfigPage() { return new GameConfigPage(); }   // FUN_00163a20
}
