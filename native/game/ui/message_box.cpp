// Message box 0x53d730 and its window 0x51b010, translated from FUN_001ab430 / FUN_001ac020 / FUN_001ac240 /
// FUN_001ac4a0 / FUN_001ac510 and the per-id handlers, FUN_00164f40 / FUN_00165350 / FUN_00165560 / FUN_001655a0.
// The set-up jump tables PTR_LAB_00498f50 (FUN_001ac9a0) and PTR_LAB_00498f80 (FUN_001aca10) were read from the ELF:
// every entry stores +0x14 = 2, +0x18 = 0; entry 3 of the first and entries 2 / 5 of the second also call FUN_001b0c60
// when the language is JP. Circle SE ids (dropped by Ghidra) are from the disassembly (0x1b077c, 0x1afdcc, 0x1b0358,
// 0x1b0ab4: a1 = 2).
#include "game/ui/message_box.hpp"
#include "game/flag_manager.hpp"
#include "game/hud/hud_api.hpp"
#include "game/pad.hpp"
#include "game/scene_manager.hpp"
#include "game/scenes/front_scenes.hpp"
#include "game/sound_stub.hpp"
#include "game/ui/gradient_texture.hpp"
#include "game/ui/menu.hpp"
#include "game/ui/mouse_menu.hpp"
#include "game/ui/sprite.hpp"
#include "game/ui/strings.hpp"
#include "game/ui/text.hpp"
#include "game/ui/texture_registry.hpp"
#include <bit>
#include <cstdio>
#include <cstring>
namespace baroque::game::ui {
namespace {
#include "game/ui/message_box_tables.inc"

MessageBox gBox;

// PORT: PC save wording -- the unused sub-messages draw kPs2None "(PS2 NONE)" in the original; the port drops the
// console name (the generated table keeps the original bytes).
constexpr char kPcNone[] = "(NONE)";
static_assert(sizeof kPs2None > sizeof kPcNone);

constexpr uint32_t kText = 0xbbbbbb;            // DAT_0044c190
constexpr uint32_t kTextDefault = 0x40bbbbbb;   // DAT_0044c198
constexpr uint32_t kSelected = 0x22227f;
constexpr float kX = 78.0f;                     // 0x429c0000
constexpr float kY = 118.0f;                    // 0x42ec0000
constexpr float kZ = 5.0f;                      // 0x40a00000
const float kZ51 = std::bit_cast<float>(0x40a33333u);   // 5.1
const float kZCentre = std::bit_cast<float>(0x409fae14u);   // 4.99 (window centre piece)

bool sceneIsGame() { return scenes().isCurrent("Game...."); }   // uRam0053ea90 == s_Game_____00462790

// Window object 0x51b010 (only the message box uses it)
struct MessageWindow {
    Sprite* piece[0x3a] = {};   // 0x51b010
    Sprite* centre = nullptr;   // 0x51b0f8 (tex 0x25)
    Sprite* backdrop = nullptr; // 0x51b100
    int open = 0;               // 0x51b108
    int settled = 0;            // 0x51b110
    int steps = 0;              // 0x51b118 (slide steps left; counts below 0 after a close)
    int closeTarget = 0;        // 0x51b120 (-close delay)
    int recentClose = 0;        // 0x51b128 (ticks since a close, from 5 down)
    float yOffset = 0.0f;       // DAT_0044c1a8
    float scale = 0.0f;         // DAT_0044c1b0
};
MessageWindow gWin;

// orig FUN_00164f40 (work/decompiled/00164f40.c)
void windowOpen(float yOffset, float scale, bool withBackdrop) {
    MessageWindow& w = gWin;
    if (w.open == 1) return;
    w.yOffset = yOffset;
    w.scale = scale;
    for (Sprite*& s : w.piece) s = nullptr;   // the 0x3a + 1 word clear (pointers only)
    w.centre = nullptr;
    w.open = 1;
    w.settled = 0;
    if (withBackdrop && w.backdrop == nullptr) {
        // FUN_00166820(-10 / 2, -5, 3, 522, 458, 0, 0, 1, 0x129, 3) (v1 1, texture size 128 on the stack)
        w.backdrop = sprites().create(-10.0f / 2.0f, -10.0f / 2.0f, 3.0f, 522.0f, 458.0f, 0.0f, 0.0f, 1.0f, 1.0f, kTexNone, 3);
        // iRam0050b144 == 0, or the player (type 0 / 2) with HP 0
        bool noPlayer = false;
        if (sceneIsGame()) {
            const CharacterAccess* p = statusHud().player;
            noPlayer = (p == nullptr) || ((p->actorType() == 0 || p->actorType() == 2) && !p->isAlive());
        }
        // UNVERIFIED: hudWorld().floorId mirrors iRam0050a2a8 (Game scene +8), which the original keeps after the
        // Game scene ends; the port refreshes it only while a dungeon is loaded.
        const int floorId = hudWorld().floorId;
        const bool dark = (sceneIsGame() && noPlayer) || floorId == 0x5f || floorId == 99;   // FUN_0011c100(0x50a2a0)
        if (w.backdrop != nullptr) w.backdrop->color = dark ? 0x80000000u : 0x44000000u;
    }
    w.steps = (w.recentClose == 0) ? 7 : 1;
    for (int i = 0; i < 0x3a; ++i) {
        const float* p = kWinPiece[i];
        const float* uv = kWinPieceUv[i];
        w.piece[i] = sprites().create(p[4], w.scale * p[5] + w.yOffset + 0.0f, kZ, p[6], w.scale * p[7], uv[0], uv[1], uv[2], uv[3], 0x27, 1);
        if (w.piece[i] != nullptr) w.piece[i]->color = 0x407f7f7f;
    }
    w.centre = sprites().create(kWinCentre[1][0], kWinCentre[1][1] * w.scale + w.yOffset + 0.0f, kZCentre, kWinCentre[1][2],
                                kWinCentre[1][3] * w.scale, kWinCentreUv[0], kWinCentreUv[1], kWinCentreUv[2], kWinCentreUv[3], 0x25, 1);
    if (w.centre != nullptr) w.centre->color = 0x447f7f7f;
}

// orig FUN_00165350 (work/decompiled/00165350.c)
void windowClose(int delay) {
    MessageWindow& w = gWin;
    if (w.open == 0) return;
    w.open = 0;
    w.closeTarget = -delay;
    w.settled = 0;
    w.recentClose = 5;
    for (Sprite*& s : w.piece) sprites().destroyRef(s);
    sprites().destroyRef(w.centre);
}
}  // namespace

MessageBox& messageBox() { return gBox; }

// orig FUN_00165560 (work/decompiled/00165560.c)
bool messageWindowReady() {
    if (gWin.open == 0) return false;
    return gWin.steps < 1;
}

// orig FUN_001655a0 (work/decompiled/001655a0.c; per-step deltas checked against the disassembly 0x165710..0x1658e0:
// the centre x moves by (dx + dw) / 7 and the half width by dw / 7, as written)
void messageWindowTick() {
    MessageWindow& w = gWin;
    if (w.open == 0 || w.steps < 1) {
        if (0 < w.recentClose) w.recentClose -= 1;
        if (w.open == 0 && w.closeTarget <= w.steps) w.steps -= 1;
        if (w.open == 0 && w.backdrop != nullptr && w.steps <= w.closeTarget) sprites().destroyRef(w.backdrop);
        return;
    }
    w.steps -= 1;
    for (int i = 0; i < 0x3a; ++i) {
        const float* p = kWinPiece[i];
        Sprite* s = w.piece[i];
        const float dw = (p[2] - p[6]) / 7.0f;
        const float dh = (p[3] - p[7]) / 7.0f;
        if (s == nullptr) continue;   // PORT: the original dereferences the piece unchecked
        s->cx = s->cx + ((p[0] - p[4]) / 7.0f + dw);
        s->cy = w.scale * ((p[1] - p[5]) / 7.0f + dh) + s->cy + 0.0f;
        s->hw = s->hw + dw;
        s->hh = w.scale * dh + s->hh + 0.0f;
    }
    const float* f = kWinCentre[0];   // DAT_00492d10..
    const float* c = kWinCentre[1];   // DAT_00492d20..
    const float dw = (f[2] - c[2]) / 7.0f;
    const float dh = (f[3] - c[3]) / 7.0f;
    const float dy = f[1] - c[1];
    if (w.centre != nullptr) {
        w.centre->cx = ((f[0] - c[0]) / 7.0f + dw) * 0.5f + w.centre->cx + 0.0f;
        w.centre->cy = w.scale * (dy / 7.0f + dh) * 0.5f + w.centre->cy + 0.0f;
        w.centre->hw = dw * 0.5f + w.centre->hw + 0.0f;
        w.centre->hh = w.scale * dh * 0.5f + w.centre->hh + 0.0f;
    }
    if (w.steps != 0) return;
    // last step: snap every piece to its final rectangle
    w.settled = 1;
    for (int i = 0; i < 0x3a; ++i) {
        const float* p = kWinPiece[i];
        Sprite* s = w.piece[i];
        if (s == nullptr) continue;
        s->cx = p[2] * 0.5f + p[0] + 0.0f;
        s->cy = w.scale * (p[3] * 0.5f + p[1] + 0.0f) + w.yOffset + 0.0f;
        s->hw = p[2] * 0.5f;
        s->hh = w.scale * (p[3] * 0.5f);
    }
    if (w.centre != nullptr) {
        w.centre->cx = f[2] * 0.5f + f[0] + 0.0f;
        w.centre->cy = w.scale * (f[1] + f[3] * 0.5f) + w.yOffset + 0.0f;
        w.centre->hw = f[2] * 0.5f;
        w.centre->hh = f[3] * 0.5f * w.scale;
    }
}

// orig FUN_001ab3f0 (work/decompiled/001ab3f0.c)
bool MessageBox::ready() const {
    if (!messageWindowReady()) return false;
    return inputDelay_ == 0;
}
// orig FUN_001ac7f0 (work/decompiled/001ac7f0.c)
int MessageBox::choice() const { return open_ == 0 ? 0 : result_; }
// orig FUN_001ac7d0 (work/decompiled/001ac7d0.c)
int MessageBox::highlighted() const { return open_ == 0 ? 0 : cursor_; }

// orig FUN_001ac240 (work/decompiled/001ac240.c): the three sprites, in the order +0x2c, +0x30, +0x28
void MessageBox::releaseSprites() {
    sprites().destroyRef(bar_);
    sprites().destroyRef(barBack_);
    sprites().destroyRef(jpSprite_);
}

// orig FUN_001b0c60 (work/decompiled/001b0c60.c; v1 128 and the texture size 128 from the disassembly 0x1b0cb8)
void MessageBox::jpAccessSprite() {
    if (!textures().isLoaded(0x30)) textures().load(0x30);   // FUN_00106090(0x4e6a68, 0x30, 1)
    jpSprite_ = sprites().create(97.0f, 212.0f, 7.0f, 118.0f, 16.0f, 0.0f, 112.0f, 118.0f, 128.0f, 0x30, 0);
}

// The switch of FUN_001ab430 with the set-up functions FUN_001ac9a0 / FUN_001aca10 (jump tables, see top) /
// FUN_001acab0 / FUN_001acb60 / FUN_001acb80 / FUN_001acc50 / FUN_001acc70..FUN_001acca0 / FUN_001accb0 /
// FUN_001accf0 / FUN_001acd70 / FUN_001acd80 / FUN_001acd90 / FUN_001acda0 / FUN_001acdc0 / FUN_001acdd0.
// `bar` = lVar4 (progress bar), `backdrop` = lVar3 (window backdrop).
void MessageBox::setup(int msgId, bool& bar, bool& backdrop) {
    const auto systemMenuGeometry = [&] {   // +0x40 = 85.0, +0x44 = 0.85, no backdrop
        yOffset_ = 85.0f;                                  // 0x42aa0000
        scale_ = std::bit_cast<float>(0x3f59999au);        // 0.85
        backdrop = false;
    };
    const auto table0 = [&](int sub) {   // FUN_001ac9a0 (PTR_LAB_00498f50)
        sub_ = sub;
        if (sub == 3 && language() == 1) jpAccessSprite();
        cursor_ = 2;
        result_ = 0;
    };
    const auto table1 = [&](int sub) {   // FUN_001aca10 (PTR_LAB_00498f80)
        sub_ = sub;
        if ((sub == 2 || sub == 5) && language() == 1) jpAccessSprite();
        result_ = 0;
        cursor_ = 2;
    };
    if (1 <= msgId && msgId <= 10) {
        table0(msgId);
        if (msgId == 3) bar = true;
    } else if (0xb <= msgId && msgId <= 0x10) {
        table1(msgId - 10);
        if (msgId == 0xc || msgId == 0xf) bar = true;
    } else if (0x11 <= msgId && msgId <= 0x14) {
        systemMenuGeometry();
        // orig FUN_001acab0
        sub_ = msgId - 0x10;
        if ((sub_ == 1 || sub_ == 3) && language() == 1) jpAccessSprite();
        result_ = 0;
        cursor_ = 2;
        closeDelay_ = 5;
        if (msgId == 0x11 || msgId == 0x13) bar = true;
    } else if (0x15 <= msgId && msgId <= 0x1a) {
        systemMenuGeometry();
        sub_ = msgId - 0x14; cursor_ = 2; result_ = 0;   // orig FUN_001acb60
    } else if (0x1b <= msgId && msgId <= 0x27) {
        systemMenuGeometry();
        // orig FUN_001acb80
        sub_ = msgId - 0x1a;
        cursor_ = 2;
        if (sub_ == 2) cursor_ = 1;
        else if ((sub_ == 1 || sub_ == 6 || sub_ == 0xc) && language() == 1) jpAccessSprite();
        result_ = 0;
        if (msgId == 0x1b || msgId == 0x20 || msgId == 0x26) bar = true;
    } else if (0x28 <= msgId && msgId <= 0x32) {
        systemMenuGeometry();
        sub_ = msgId - 0x27; cursor_ = 2; result_ = 0;   // orig FUN_001acc50
    } else if (0x33 <= msgId && msgId <= 0x36) {
        result_ = 0; cursor_ = 1;                         // orig FUN_001acc70 / FUN_001acc80 / FUN_001acc90 / FUN_001acca0
    } else if (msgId == 0x37 || msgId == 0x38) {
        sub_ = msgId - 0x36;                              // orig FUN_001accb0
        cursor_ = (sub_ == 2) ? 6 : 1;
        result_ = 0;
    } else if (0x39 <= msgId && msgId <= 0x3c) {
        // orig FUN_001accf0
        sub_ = msgId - 0x38;
        if (sub_ == 4 || sub_ == 1) cursor_ = 2;
        else cursor_ = (lastId_ == 0x39) ? 5 : 3;         // sub 2 / 3: +0x20 is the previous message
        result_ = 0;
        closeDelay_ = 5;
    } else if (msgId == 0x3d) {
        systemMenuGeometry();
        result_ = 0; cursor_ = 1;                         // orig FUN_001acd70
    } else if (msgId == 0x3e) {
        yOffset_ = 30.0f;                                 // 0x41f00000
        result_ = 0; cursor_ = 1;                         // orig FUN_001acd80
    } else if (msgId == 0x3f) {
        yOffset_ = 30.0f;
        result_ = 0; cursor_ = 0xe;                       // orig FUN_001acd90
    } else if (msgId == 0x40 || msgId == 0x41) {
        sub_ = msgId - 0x3f; result_ = 0; cursor_ = 2;    // orig FUN_001acda0
    } else if (msgId == 0x42) {
        systemMenuGeometry();
        result_ = 0; cursor_ = 0x12;                      // orig FUN_001acdc0
    } else if (0x43 <= msgId && msgId <= 0x46) {
        systemMenuGeometry();
        sub_ = msgId - 0x42; result_ = 0; cursor_ = 2;    // orig FUN_001acdd0
        if (msgId == 0x44) bar = true;
    }
}

// orig FUN_001ab430 (work/decompiled/001ab430.c; sprite stack arguments from the disassembly 0x1abe2c..0x1abf24)
void MessageBox::open(int msgId, int arg) {
    if (open_ != 0) close();
    yOffset_ = 0.0f;
    scale_ = 1.0f;
    bool bar = false;        // lVar4
    bool backdrop = true;    // lVar3
    releaseSprites();        // FUN_001ac240
    result_ = 0;
    cursor_ = 0;
    alpha_ = 0;
    closeDelay_ = 0;
    setup(msgId, bar, backdrop);
    if (!menu().isOpen()) {  // FUN_00167d70(0x51b140) == 0: the window keeps its base geometry and its backdrop
        yOffset_ = 0.0f;
        scale_ = 1.0f;
        backdrop = true;
    }
    windowOpen(yOffset_, scale_, backdrop);   // FUN_00164f40
    open_ = 1;
    frameCount_ = 0;
    msgId_ = msgId;
    inputDelay_ = 10;
    arg_ = arg;
    if (bar) {
        const float y = yOffset_ * std::bit_cast<float>(0x3f59999au) * scale_ + 292.0f;
        bar_ = (gradientTextureId() != -1)
                   ? sprites().create(126.0f, y, 6.0f, 64.0f, 4.0f, 0.0f, 0.0f, 32.0f, 1.0f, gradientTextureId(), 2)
                   : nullptr;   // PORT: the original indexes the texture table with DAT_0044c1a0 unchecked
        if (bar_ != nullptr) {  // the original divides by the 128 x 128 passed on the stack, not the 32 x 32 texture size
            bar_->u0 = 0.0f; bar_->v0 = 0.0f; bar_->u1 = 32.0f / 128.0f; bar_->v1 = 1.0f / 128.0f;
        }
        barBack_ = sprites().create(126.0f, y, 6.0f, 260.0f, 4.0f, 0.0f, 0.0f, 32.0f, 16.0f, kTexNone, 1);
    }
    if (sceneIsGame()) {
        if (!hudMapShown()) {
            mapHidden_ = 0;
        } else {
            hudMapHide();                     // FUN_00126fb0(0x50a350)
            mapHidden_ = 1;
        }
        sound().setMenuDuck(1);               // FUN_001161d0(0x4e9790, 1)
    }
    sound().playSe(1.0f, 4);                  // FUN_00115390(1.0, snd, 4)
}

// orig FUN_001ac020 (work/decompiled/001ac020.c)
void MessageBox::close() {
    if (open_ == 0) return;
    open_ = 0;
    windowClose(closeDelay_);                 // FUN_00165350(+0x38)
    releaseSprites();                         // FUN_001ac240
    if (sceneIsGame() && !menu().isOpen() && textures().isLoaded(0x30)) textures().release(0x30);
    if (sceneIsGame()) {
        if (mapHidden_ != 0) hudMapShow();    // FUN_00126cc0(0x50a350)
        sound().setMenuDuck(0);               // FUN_001161d0(0x4e9790, 0)
    }
    lastId_ = msgId_;
    lastResult_ = result_;
}

// orig FUN_001ac4a0 (work/decompiled/001ac4a0.c)
void MessageBox::update() {
    mouseMoved_ = false;
    hitCount_ = 0;
    if ((open_ != 0 || !menu().isOpen()) && gWin.open != 0) {
        if (inputDelay_ != 0) inputDelay_ -= 1;
        frame();
    }
}

// orig FUN_001ac510 (work/decompiled/001ac510.c)
void MessageBox::frame() {
    if (!messageWindowReady()) {
        alpha_ = 0;
    } else if (alpha_ < 0x40) {
        alpha_ += 0x40;
        if (0x40 < alpha_) alpha_ = 0x40;
    }
    if (jpSprite_ != nullptr) jpSprite_->color = alphaBits() + 0x7f7f7f;
    TextRenderer& t = text();
    t.layerFlag = 1;
    t.halfW = 8.0f;    // 0x41000000
    t.halfH = 9.0f;    // 0x41100000
    t.color = kText + alphaBits();
    mouseHover();      // PORT: mouse menu layer
    const int id = msgId_;
    if (1 <= id && id <= 10) group1();
    else if (0xb <= id && id <= 0x10) group2();
    else if (0x11 <= id && id <= 0x14) group3();
    else if (0x15 <= id && id <= 0x1a) group4();
    else if (0x1b <= id && id <= 0x27) group5();
    else if (0x28 <= id && id <= 0x32) group6();
    else if (id == 0x33) question(0x215);
    else if (id == 0x34) question(0x216);
    else if (id == 0x35) question(0x217);
    else if (id == 0x36) bagCheck();
    else if (id == 0x37 || id == 0x38) warpList();
    else if (0x39 <= id && id <= 0x3c) stairsMenu();
    else if (id == 0x3d) databaseStart();
    else if (id == 0x3e) startConfirm();
    else if (id == 0x3f) difficulty();
    else if (id == 0x40 || id == 0x41) configQuestion();
    else if (id == 0x42) fileMenu();
    else if (0x43 <= id && id <= 0x46) deleteFile();
    if (bar_ != nullptr && barBack_ != nullptr) barBack_->color = alphaBits() + 0x3f3f4f;
    if (bar_ != nullptr) {
        bar_->color = alphaBits() + 0xffffff;
        bar_->cx = bar_->cx + 6.0f;
        if (388.0f < bar_->cx + bar_->hw) bar_->cx = 152.0f;   // 0x43180000
    }
    frameCount_ += 1;
    t.layerFlag = 1;
    t.color = kTextDefault;
    mouseItems();      // PORT: mouse menu layer
}

// ---- text idioms -------------------------------------------------------------------------------------------------
void MessageBox::lines(float y) {
    TextRenderer& t = text();
    t.x = kX; t.y = y; t.z = kZ;
}
// the FUN_00113cf0 / FUN_00167250 idiom starting from (78, y0): only x changes (height 0), y += +0x40 * +0x44
void MessageBox::centred(int strId, float y0) {
    TextRenderer& t = text();
    float x = kX, y = y0;
    centreOnScreen(t.measure(str(strId)), 0.0f, &x, &y);
    t.x = x;
    t.y = yOffset_ * scale_ + y + 0.0f;
    t.z = kZ;
    t.draw(str(strId));
}
void MessageBox::draw(int strId) { text().draw(str(strId)); }
void MessageBox::drawArg(int strId) { text().draw(str(strId), arg_); }

// ---- handlers ----------------------------------------------------------------------------------------------------
// orig FUN_001acdf0 (work/decompiled/001acdf0.c): BootMC card messages (y fixed at 118)
void MessageBox::group1() {
    switch (sub_) {
    case 1: lines(kY); draw(0x1c9); drawArg(0x1ca); draw(0x1cb); yesNo(); inputYesNo(); return;
    case 2: lines(kY); draw(0x1cc); drawArg(0x1cd); draw(0x1ce); yesNo(); inputYesNo(); break;
    case 3: lines(kY); draw(0x1cf); draw(0x1d0); break;
    case 4: lines(kY); draw(0x1d1); draw(0x1d2); yesNo(); inputYesNo(); break;
    case 5: case 6: case 7: case 8: case 9: case 10: lines(kY); text().draw(kPcNone); break;
    default: break;
    }
}
// orig FUN_001ad190 (work/decompiled/001ad190.c): format messages
void MessageBox::group2() {
    switch (sub_) {
    case 1: lines(118.0f); draw(0x1d3); draw(0x1d4); yesNo(); inputYesNo(); return;
    case 2: lines(118.0f); draw(0x1d5); draw(0x1d6); break;
    case 3: centred(0x1d7, 206.0f); break;
    case 4: lines(118.0f); draw(0x1d8); draw(0x1d9); break;
    case 5: lines(118.0f); draw(0x1da); draw(0x1db); break;
    case 6: lines(118.0f); draw(0x1dc); break;
    default: break;
    }
}
// orig FUN_001ad430 (work/decompiled/001ad430.c): load
void MessageBox::group3() {
    const float y = yOffset_ * scale_ + 118.0f;
    if (sub_ == 4) centred(0x1e3, 206.0f);
    else if (sub_ == 3) { lines(y); drawArg(0x1e1); draw(0x1e2); }
    else if (sub_ == 2) { lines(y); drawArg(0x1df); draw(0x1e0); yesNo(); inputYesNo(); }
    else if (sub_ == 1) { lines(y); draw(0x1dd); draw(0x1de); }
}
// orig FUN_001ad680 (work/decompiled/001ad680.c): load errors
void MessageBox::group4() {
    const float y = yOffset_ * scale_ + 118.0f;
    switch (sub_) {
    case 1: lines(y); draw(0x1e4); draw(0x1e5); return;
    case 2: lines(y); draw(0x1e6); draw(0x1e7); break;
    case 3: lines(y); drawArg(0x1e8); draw(0x1e9); break;
    case 4: lines(y); draw(0x1ea); draw(0x1eb); break;
    case 5: lines(y); draw(0x1ec); draw(0x1ed); break;
    case 6: lines(y); draw(0x1ee); draw(0x1ef); break;
    default: break;
    }
}
// orig FUN_001ad980 (work/decompiled/001ad980.c): save
void MessageBox::group5() {
    const float y = yOffset_ * scale_ + 118.0f;
    switch (sub_) {
    case 1: lines(y); draw(0x1f0); draw(0x1f1); return;
    case 2: lines(y); draw(0x1f2); draw(499); yesNo(); inputYesNo(); break;
    case 3: lines(y); draw(500); draw(0x1f5); yesNo(); inputYesNo(); break;
    case 4: lines(y); draw(0x1f6); drawArg(0x1f7); draw(0x1f8); yesNo(); inputYesNo(); break;
    case 5: lines(y); draw(0x1f9); drawArg(0x1fa); draw(0x1fb); yesNo(); inputYesNo(); break;
    case 6: lines(y); draw(0x1fc); draw(0x1fd); break;
    case 7: centred(0x1fe, 206.0f); break;
    case 8: lines(y); drawArg(0x1ff); draw(0x200); yesNo(); inputYesNo(); break;
    case 9: lines(y); drawArg(0x201); drawArg(0x202); yesNo(); inputYesNo(); break;
    case 10: lines(y); draw(0x203); drawArg(0x204); draw(0x205); yesNo(); inputYesNo(); break;
    case 0xb: lines(y); draw(0x206); drawArg(0x207); yesNo(); inputYesNo(); break;
    case 0xc: lines(y); drawArg(0x208); draw(0x209); break;
    case 0xd: centred(0x20a, 206.0f); break;
    default: break;
    }
}
// orig FUN_001ae0e0 (work/decompiled/001ae0e0.c): save errors
void MessageBox::group6() {
    const float y = yOffset_ * scale_ + 118.0f;
    switch (sub_) {
    case 1: lines(y); draw(0x20b); drawArg(0x20c); draw(0x20d); return;
    case 2: lines(y); draw(0x20e); drawArg(0x20f); draw(0x210); break;
    case 3: lines(y); draw(0x211); draw(0x212); break;
    case 4: lines(y); draw(0x213); draw(0x214); break;
    case 5: case 6: case 7: case 8: case 9: case 10: case 0xb: lines(118.0f); text().draw(kPcNone); break;
    default: break;
    }
}
// orig FUN_001ae4f0 / FUN_001ae5b0 / FUN_001ae670 (0x33 Neuro Tower, 0x34 go down, 0x35 training dungeon)
void MessageBox::question(int strId) {
    centred(strId, 206.0f);
    yesNo();
    inputYesNo();
}
// orig FUN_001ae730 (work/decompiled/001ae730.c): research lab (0x218) or "bag is full" (0x219) when iRam0053d910 == 20
void MessageBox::bagCheck() {
    if (hudInventoryCount() == 0x14) {
        centred(0x219, 206.0f);
        if (messageWindowReady() && 0x3f < alpha_ && result_ == 0 && ((pad().port[0].pressed & 0x40) != 0 || (pad().port[0].pressed & 0x20) != 0)) {
            cursor_ = 2;
            result_ = 2;
            sound().playSe(1.0f, 1);
        }
    } else {
        centred(0x218, 206.0f);
        yesNo();
        inputYesNo();
    }
}
// orig FUN_001ae900 (work/decompiled/001ae900.c): 0x37 coffin dungeon question, 0x38 dungeon list (cursor 6..12)
void MessageBox::warpList() {
    TextRenderer& t = text();
    const uint32_t textRgb = kText;   // iVar1 = DAT_0044c190
    if (sub_ == 2) {
        float left = 0.0f, right = 0.0f;
        if (language() == 1) { left = 96.0f; right = 272.0f; }          // 0x42c00000 / 0x43880000
        else if (language() == 0) { left = 68.0f; right = 262.0f; }     // 0x42880000 / 0x43830000
        float x = kX, y = 206.0f;
        centreOnScreen(t.measure(str(0x21b)), 0.0f, &x, &y);
        t.x = x; t.y = 118.0f; t.z = kZ;
        t.color = textRgb + alphaBits();
        draw(0x21b);
        t.y = 176.0f; t.z = kZ;
        t.color = (cursor_ == 6) ? kSelected : kText;
        t.color = t.color + alphaBits();
        t.x = left;
        hit(6, t.x, 176.0f, t.measure(str(0x21c)));
        draw(0x21c);
        // entries 7 / 8 / 10 / 11: unlocked flag (normal colour), else seen flag (grey name) or "?????"
        const auto entry2 = [&](int value, float ex, float ey, uint32_t unlocked, uint32_t seen, int strId) {
            t.y = ey; t.z = kZ; t.x = ex;
            hit(value, ex, ey, t.measure(str(strId)));
            if (!flags().test(unlocked)) {
                t.color = ((cursor_ == value) ? kSelected : 0x7f7f7fu) + alphaBits();
                if (!flags().test(seen)) t.draw(kUnknownSlot);
                else draw(strId);
            } else {
                t.color = ((cursor_ == value) ? kSelected : kText) + alphaBits();
                draw(strId);
            }
        };
        const auto entry1 = [&](int value, float ex, float ey, uint32_t unlocked, int strId) {   // entries 9 / 12
            t.y = ey; t.z = kZ; t.x = ex;
            hit(value, ex, ey, t.measure(str(strId)));
            if (!flags().test(unlocked)) {
                t.color = ((cursor_ == value) ? kSelected : 0x7f7f7fu) + alphaBits();
                t.draw(kUnknownSlot);
            } else {
                t.color = ((cursor_ == value) ? kSelected : kText) + alphaBits();
                draw(strId);
            }
        };
        entry2(7, left, 212.0f, 0x4ed, 0x4e7, 0x21d);
        entry2(8, left, 248.0f, 0x4ee, 0x4e8, 0x21e);
        entry1(9, left, 284.0f, 0x4e9, 0x21f);
        entry2(10, right, 212.0f, 0x4ef, 0x4ea, 0x220);
        entry2(0xb, right, 248.0f, 0x4f0, 0x4eb, 0x221);
        entry1(0xc, right, 284.0f, 0x4ec, 0x222);
        inputWarp();
    } else if (sub_ == 1) {
        centred(0x21a, 206.0f);
        yesNo();
        inputYesNo();
    }
}
// orig FUN_001af100 (work/decompiled/001af100.c): 0x39 / 0x3c return to title, 0x3a / 0x3b resume / save / title (3..5)
void MessageBox::stairsMenu() {
    TextRenderer& t = text();
    if (sub_ == 4) {
        lines(118.0f);
        draw(0x223);
        draw(0x225);
        yesNo();
        inputYesNo();
    } else if (sub_ == 3 || sub_ == 2) {
        float x = 0.0f, y = 178.0f;
        const int ids[3] = {0x226, 0x227, 0x228};
        for (int i = 0; i < 3; ++i) {
            centreOnScreen(t.measure(str(ids[i])), 0.0f, &x, &y);
            t.x = x;
            t.y = y + 40.0f * static_cast<float>(i);   // fStack_10, + 40.0, + 80.0
            t.z = kZ;
            t.color = ((cursor_ == 3 + i) ? kSelected : kText) + alphaBits();
            hit(3 + i, t.x, t.y, t.measure(str(ids[i])));
            draw(ids[i]);
        }
        inputStairs();
    } else if (sub_ == 1) {
        lines(118.0f);
        if (fileSelect().saved == 0) draw(0x224);   // iRam0053ea30
        else draw(0x225);
        yesNo();
        inputYesNo();
    }
}
// orig FUN_001af650 (work/decompiled/001af650.c)
void MessageBox::databaseStart() {
    lines(yOffset_ * scale_ + 118.0f);
    draw(0x22f);
    draw(0x230);
    yesNo();
    inputYesNo();
}
// orig FUN_001af6f0 (work/decompiled/001af6f0.c): "Difficulty : " + the difficulties of flags 0x4f1..0x4f4, start?
void MessageBox::startConfirm() {
    TextRenderer& t = text();
    char buf[0x48] = {};   // auStack_50, 0x40 bytes cleared
    std::snprintf(buf, sizeof buf, "%s", str(0x243));                  // FUN_0023f540 strcpy
    const uint32_t flagIds[4] = {0x4f1, 0x4f2, 0x4f3, 0x4f4};
    for (int i = 0; i < 4; ++i) {
        if (!flags().test(flagIds[i])) continue;
        const size_t used = std::strlen(buf);   // FUN_0023f140 strcat (PORT: bounded to the 0x48-byte buffer)
        std::snprintf(buf + used, sizeof buf - used, "%s", str(0x23f + i));
    }
    float x = 0.0f, y = 0.0f;
    centreOnScreen(t.measure("%s", buf), 0.0f, &x, &y);
    t.y = 194.0f;   // 0x43420000
    t.x = x;
    t.z = kZ;
    t.draw("%s", buf);
    x = 0.0f; y = 0.0f;
    centreOnScreen(t.measure(str(0x229)), 0.0f, &x, &y);
    t.y = 240.0f;   // 0x43700000
    t.x = x;
    t.z = kZ;
    draw(0x229);
    centreOnScreen(t.measure(str(0x22a)), 0.0f, &x, &y);
    t.y = 258.0f;   // 0x43810000
    t.x = x;
    t.z = kZ;
    draw(0x22a);
    yesNo();
    inputYesNo();
}
// orig FUN_001af950 (work/decompiled/001af950.c): difficulty 0xd..0xf (0x10 EXTRA HARD once the game was cleared)
void MessageBox::difficulty() {
    TextRenderer& t = text();
    float x = 0.0f, y = 0.0f;
    centreOnScreen(t.measure(str(0x23e)), 0.0f, &x, &y);
    t.x = x;
    t.y = 190.0f;   // 0x433e0000
    t.z = kZ51;
    draw(0x23e);
    const float rowY[4] = {220.0f, 250.0f, 280.0f, 310.0f};   // 0x435c0000 / 0x437a0000 / 0x438c0000 / 0x439b0000
    const int n = (front().ea48 != 0) ? 4 : 3;                // iRam0053ea48
    for (int i = 0; i < n; ++i) {
        const int id = 0x23f + i;
        centreOnScreen(t.measure(str(id)), 0.0f, &x, &y);
        t.y = rowY[i];
        t.x = x;
        t.z = kZ51;
        t.color = ((cursor_ == 0xd + i) ? kSelected : kText) + alphaBits();
        hit(0xd + i, t.x, rowY[i], t.measure(str(id)));
        draw(id);
    }
    inputDifficulty();
}
// orig FUN_001afe00 (work/decompiled/001afe00.c): 0x40 restore defaults (YES/NO); 0x41 draws nothing (the GAME
// CONFIG page draws its camera list inside the window)
void MessageBox::configQuestion() {
    TextRenderer& t = text();
    if (sub_ != 1) return;
    if (language() == 1) {
        t.x = kX; t.y = 118.0f; t.z = kZ51;
        draw(0x22b);
        draw(0x22c);
    } else if (language() == 0) {
        float x = kX, y = 206.0f;   // 0x434e0000
        centreOnScreen(t.measure(str(0x22b)), 0.0f, &x, &y);
        t.x = x;
        t.y = y;
        t.z = kZ51;
        draw(0x22b);
    }
    yesNo();
    inputYesNo();
}
// orig FUN_001aff60 (work/decompiled/001aff60.c): New Game / Continue / Database / Delete (0x11..0x14)
void MessageBox::fileMenu() {
    TextRenderer& t = text();
    float x = 0.0f, y = 0.0f;
    const float rowY[4] = {203.0f, 243.0f, 283.0f, 323.0f};   // 0x434b0000 / 0x43730000 / 0x438d8000 / 0x43a18000
    for (int i = 0; i < 4; ++i) {
        const int id = 0x231 + i;
        centreOnScreen(t.measure(str(id)), 0.0f, &x, &y);
        t.y = rowY[i];
        t.x = x;
        t.z = kZ51;
        t.color = ((cursor_ == 0x11 + i) ? kSelected : kText) + alphaBits();
        hit(0x11 + i, t.x, rowY[i], t.measure(str(id)));
        draw(id);
    }
    inputFileMenu();
}
// orig FUN_001b0390 (work/decompiled/001b0390.c): delete file %d
void MessageBox::deleteFile() {
    TextRenderer& t = text();
    if (sub_ == 4) {
        float x = kX, y = 206.0f;
        centreOnScreen(t.measure(str(0x23c), arg_), 0.0f, &x, &y);
        t.x = x;
        t.y = yOffset_ * scale_ + y + 0.0f;
        t.z = kZ;
        drawArg(0x23c);
    } else if (sub_ == 3) {
        float x = kX, y = 206.0f;
        char buf[0x100] = {};   // auStack_110, 0xff bytes cleared
        std::snprintf(buf, sizeof buf, str(0x23b), arg_);   // FUN_0023edb8 sprintf
        centreOnScreen(t.measure("%s", buf), 0.0f, &x, &y);
        t.x = x;
        t.z = kZ;
        t.y = yOffset_ * scale_ + y + 0.0f;
        t.draw("%s", buf);
    } else if (sub_ == 2) {
        t.y = yOffset_ * scale_ + 118.0f; t.x = kX; t.z = kZ51;
        drawArg(0x239);
        draw(0x23a);
    } else if (sub_ == 1) {
        t.y = yOffset_ * scale_ + 118.0f; t.x = kX; t.z = kZ51;
        drawArg(0x235);
        draw(0x236);
        draw(0x237);
        draw(0x238);
        yesNo();
        inputYesNo();
    }
}

// orig FUN_001ac810 (work/decompiled/001ac810.c): full-width "ＹＥＳ／ＮＯ" centred at y 284 (264 with an offset)
void MessageBox::yesNo() {
    TextRenderer& t = text();
    float x = 0.0f;
    float y = 284.0f;
    if (0.0f < yOffset_) y = 264.0f;
    centreOnScreen(t.measure(kYesNoAll), 0.0f, &x, &y);
    t.x = x;
    t.y = yOffset_ * scale_ + y + 0.0f;
    t.z = kZ51;
    t.color = (cursor_ == 1) ? alphaBits() + kSelected : kText + alphaBits();
    hit(1, t.x, t.y, t.measure(kYesNoYes));
    t.draw(kYesNoYes);
    t.color = kText + alphaBits();
    t.draw(kYesNoSlash);
    t.color = (cursor_ == 2) ? alphaBits() + kSelected : kText + alphaBits();
    hit(2, t.x, t.y, t.measure(kYesNoNo));
    t.draw(kYesNoNo);
    mouse::wheelHorizontal();   // PORT: the wheel steps the YES / NO cursor with Left / Right like the pad
}

// ---- input -------------------------------------------------------------------------------------------------------
// orig FUN_001b0680 (work/decompiled/001b0680.c): left / right (repeat, D-pad and stick) toggle, Cross, Circle
void MessageBox::inputYesNo() {
    if (inputDelay_ != 0 || !messageWindowReady() || alpha_ <= 0x3f || result_ != 0) return;
    const uint32_t rep = pad().port[0].repeat;     // uRam004e6290
    const uint32_t prs = pad().port[0].pressed;    // uRam004e6288
    if ((rep & 0x8000) == 0 && (rep & 0x2000) == 0 && (rep & 0x40000) == 0 && (rep & 0x80000) == 0) {
        if ((prs & 0x40) != 0) {
            result_ = cursor_;
            sound().playSe(1.0f, 1);
            return;
        }
        if ((prs & 0x20) == 0) return;
        cursor_ = 2;
        result_ = 2;
        sound().playSe(1.0f, 2);
        return;
    }
    cursor_ = (cursor_ == 1) ? 2 : 1;
    sound().playSe(1.0f, 6);
}
// orig FUN_001b07b0 (work/decompiled/001b07b0.c): dungeon list 6 (left column 6..9, right column 10..12)
void MessageBox::inputWarp() {
    if (inputDelay_ != 0 || !messageWindowReady() || alpha_ < 0x40 || result_ != 0) return;
    const uint32_t rep = pad().port[0].repeat;
    const uint32_t prs = pad().port[0].pressed;
    if ((rep & 0x1000) != 0 || (rep & 0x10000) != 0) {
        cursor_ -= 1;
        sound().playSe(1.0f, 6);
    } else if ((rep & 0x4000) != 0 || (rep & 0x20000) != 0) {
        cursor_ += 1;
        sound().playSe(1.0f, 6);
    } else if ((rep & 0x8000) != 0 || (rep & 0x2000) != 0 || (rep & 0x40000) != 0 || (rep & 0x80000) != 0) {
        if (cursor_ != 6) {
            if (cursor_ == 7) cursor_ = 10;
            else if (cursor_ == 10) cursor_ = 7;
            else if (cursor_ == 8) cursor_ = 0xb;
            else if (cursor_ == 0xb) cursor_ = 8;
            else if (cursor_ == 9) cursor_ = 0xc;
            else if (cursor_ == 0xc) cursor_ = 9;
            sound().playSe(1.0f, 6);
        }
    } else if ((prs & 0x40) != 0) {
        const bool open = (cursor_ == 6) || (cursor_ == 7 && flags().test(0x4ed)) || (cursor_ == 8 && flags().test(0x4ee)) ||
                          (cursor_ == 9 && flags().test(0x4e9)) || (cursor_ == 10 && flags().test(0x4ef)) ||
                          (cursor_ == 0xb && flags().test(0x4f0)) || (cursor_ == 0xc && flags().test(0x4ec));
        if (open) {
            result_ = cursor_;
            sound().playSe(1.0f, 1);
        } else {
            sound().playSe(1.0f, 3);
        }
    } else if ((prs & 0x20) != 0) {
        cursor_ = 2;
        result_ = 2;
        sound().playSe(1.0f, 2);
    }
    // LAB_001b0ad8 (also after Circle, which leaves the cursor on 12)
    if (cursor_ < 6) cursor_ = 0xc;
    else if (0xc < cursor_) cursor_ = 6;
}
// orig FUN_001b0b10 (work/decompiled/001b0b10.c): resume / save / title (3..5), no Circle
void MessageBox::inputStairs() {
    if (inputDelay_ != 0 || !messageWindowReady() || alpha_ < 0x40 || result_ != 0) return;
    const uint32_t rep = pad().port[0].repeat;
    const uint32_t prs = pad().port[0].pressed;
    if ((rep & 0x1000) != 0 || (rep & 0x10000) != 0) {
        cursor_ -= 1;
        sound().playSe(1.0f, 6);
    } else if ((rep & 0x4000) != 0 || (rep & 0x20000) != 0) {
        cursor_ += 1;
        sound().playSe(1.0f, 6);
    } else if ((prs & 0x40) != 0) {
        result_ = cursor_;
        sound().playSe(1.0f, 1);
    }
    if (cursor_ < 3) cursor_ = 5;
    else if (5 < cursor_) cursor_ = 3;
}
// orig FUN_001afc80 (work/decompiled/001afc80.c): EASY..HARD (EXTRA HARD when iRam0053ea48)
void MessageBox::inputDifficulty() {
    if (inputDelay_ != 0 && alpha_ <= 0x3f) return;
    const int last = (front().ea48 == 0) ? 0xf : 0x10;
    if (!messageWindowReady() || result_ != 0) return;
    const uint32_t rep = pad().port[0].repeat;
    const uint32_t prs = pad().port[0].pressed;
    if ((rep & 0x4000) != 0 || (rep & 0x20000) != 0) {
        cursor_ += 1;
        if (last < cursor_) cursor_ = 0xd;
        sound().playSe(1.0f, 6);
    } else if ((rep & 0x1000) != 0 || (rep & 0x10000) != 0) {
        cursor_ -= 1;
        if (cursor_ < 0xd) cursor_ = last;
        sound().playSe(1.0f, 6);
    } else if ((prs & 0x40) != 0) {
        result_ = cursor_;
        sound().playSe(1.0f, 1);
    } else if ((prs & 0x20) != 0) {
        cursor_ = 2;
        result_ = 2;
        sound().playSe(1.0f, 2);
    }
}
// orig FUN_001b0220 (work/decompiled/001b0220.c): file menu 0x11..0x14
void MessageBox::inputFileMenu() {
    if (inputDelay_ != 0 && alpha_ <= 0x3f) return;
    if (!messageWindowReady() || result_ != 0) return;
    const uint32_t rep = pad().port[0].repeat;
    const uint32_t prs = pad().port[0].pressed;
    if ((rep & 0x4000) != 0 || (rep & 0x20000) != 0) {
        cursor_ += 1;
        if (0x14 < cursor_) cursor_ = 0x11;
        sound().playSe(1.0f, 6);
    } else if ((rep & 0x1000) != 0 || (rep & 0x10000) != 0) {
        cursor_ -= 1;
        if (cursor_ < 0x11) cursor_ = 0x14;
        sound().playSe(1.0f, 6);
    } else if ((prs & 0x40) != 0) {
        result_ = cursor_;
        sound().playSe(1.0f, 1);
    } else if ((prs & 0x20) != 0) {
        cursor_ = 2;
        result_ = 2;
        sound().playSe(1.0f, 2);
    }
}

// ---- PORT: mouse menu layer --------------------------------------------------------------------------------------
// The open window is modal (dialog layer). Every choice a handler drew registers its text rectangle with its cursor
// value; hovering a choice moves the cursor like the pad (SE 6), a click arrives as Cross on it in the next sample.
// A window without choices (information / progress messages) is one item whose click is Cross.
void MessageBox::hit(int value, float x, float y, float w) {
    if (hitCount_ >= 8) return;
    hits_[hitCount_++] = Hit{value, x, y, w};
}
void MessageBox::mouseHover() {
    if (inputDelay_ != 0 || !messageWindowReady() || alpha_ < 0x40 || result_ != 0) return;
    const int h = mouse::hovered(this);
    if (h <= 0 || h == cursor_) return;
    cursor_ = h;
    sound().playSe(1.0f, 6);
    mouseMoved_ = true;
}
void MessageBox::mouseItems() {
    // window bounds: the final rectangles of the frame pieces
    float x0 = 512.0f, y0 = 448.0f, x1 = 0.0f, y1 = 0.0f;
    for (const auto& p : kWinPiece) {
        const float py = gWin.scale * p[1] + gWin.yOffset, ph = gWin.scale * p[3];
        if (p[0] < x0) x0 = p[0];
        if (py < y0) y0 = py;
        if (p[0] + p[2] > x1) x1 = p[0] + p[2];
        if (py + ph > y1) y1 = py + ph;
    }
    mouse::block(x0, y0, x1 - x0, y1 - y0, mouse::kLayerDialog);
    if (result_ != 0) return;
    if (hitCount_ == 0) {
        mouse::item(this, -2, x0, y0, x1 - x0, y1 - y0, mouse::kConfirm, mouse::kLayerDialog);
        return;
    }
    for (int i = 0; i < hitCount_; ++i) {
        const Hit& h = hits_[i];
        mouse::item(this, h.value, h.x - 6.0f, h.y - 6.0f, h.w + 12.0f, 30.0f, mouse::kConfirm, mouse::kLayerDialog);
    }
}
}  // namespace baroque::game::ui
