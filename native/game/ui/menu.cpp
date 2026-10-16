#include <algorithm>
#include "game/ui/menu.hpp"
#include "game/fade.hpp"
#include "game/options.hpp"
#include "game/pad.hpp"
#include "game/render2d.hpp"   // PORT: widescreen anchors of the frame pieces
#include "game/hud/hud.hpp"
#include "game/hud/hud_api.hpp"
#include "game/sound_stub.hpp"
#include "game/scene_manager.hpp"
#include "game/ui/gradient_texture.hpp"
#include "game/ui/help_bar.hpp"
#include "game/ui/menu_tables.hpp"
#include "game/ui/message_box.hpp"
#include "game/ui/sprite.hpp"
#include "game/ui/strings.hpp"
#include "game/ui/texture_registry.hpp"
#include "game/ui/ui_tables.hpp"
#include <bit>
#include <cstring>
namespace baroque::game::ui {
namespace {
constexpr uint32_t kSinTable[1024] = {
#include "game/sin_table.inc"
};
MenuObject gMenu;
NameEntryGlobals gNameEntry;
bool frontEndScene() {   // the "Option.. || NameEntr || GameLoad" test in FUN_00167390 / FUN_00167910
    const SceneManager& sm = scenes();
    return sm.isCurrent("Option..") || sm.isCurrent("NameEntr") || sm.isCurrent("GameLoad");
}
}
float sinDeg(float deg) { return std::bit_cast<float>(kSinTable[size_t(int(deg * 2.8444445f) & 0x3ff)]); }
signed char gOptionCursor = 0;
signed char gConfigCursor = 0;
int gOptionsChanged = 0;
MenuObject& menu() { return gMenu; }
NameEntryGlobals& nameEntry() { return gNameEntry; }
// orig FUN_0016ace0 (work/decompiled/0016ace0.c)
void nameEntryReset() {
    std::memcpy(gNameEntry.name, kNameEntryDefault, std::strlen(kNameEntryDefault) + 1);   // FUN_0023f540 strcpy(0x51b5f8, 0x44c260)
    gNameEntry.pos = 0;
}
// orig FUN_0016ac80 (work/decompiled/0016ac80.c): reset, then copy the bytes (no terminator copied, as in the original)
void nameEntrySet(const char* name) {
    nameEntryReset();
    for (int i = 0; name[i] != 0 && i < 15; ++i) gNameEntry.name[i] = name[i];   // port guard: buffer is 16 bytes
}
// orig FUN_0016ad10 (work/decompiled/0016ad10.c)
bool nameEntryChanged() { return std::strcmp(gNameEntry.name, kNameEntryDefault) != 0; }

// ---- page base -------------------------------------------------------------------------------------------------
// orig FUN_001642d0 (work/decompiled/001642d0.c): 22 frame pieces from tex 0x31 at z 0.5
void MenuPage::makeFrame() {
    if (frameMade != 0) return;
    frameMade = 1;
    titleY = -80.0f;   // 0xc2a00000
    titleSettled = 0;
    for (int i = 0; i < 22; ++i) {
        const float* p = kMenuFramePos[i];
        const float* uv = kMenuFrameUv[i];
        frame[i] = sprites().create(p[0] - 1.0f, p[1] - 1.0f, 0.5f, p[2], p[3], uv[0], uv[1], uv[2], uv[3], 0x31, 1);
        if (frame[i]) frame[i]->color = 0x407f7f7f;
        // PORT widescreen: the 22 pieces are the left pipe column (x 0..18, y 285..448) and its bottom-left foot
        // (x 0..120, y 432..448): all of them stay at the area's left edge
        if (frame[i] && portWideLeft != 0) frame[i]->anchor = kAnchorLeft;
    }
    // FUN_001642d0 tail: in the Game scene the status HUD 0x5179c0 is released while the page is open
    if (scenes().isCurrent("Game....") && statusHud().built()) statusHud().release();   // FUN_0014f8c0(0x5179c0)
}
// orig FUN_00164910 (work/decompiled/00164910.c): the opaque menu backdrop over the 3-D view (BG01_00 / BG01_01).
// Tables 0x492000 {x, y, w, h}, 0x492040 {u0, v0, u1, v1} (v1 and the texture size 256 on the stack, disassembly
// 0x164990..0x1649d0), 0x492080 texture ids; z -5, layer flag 0, colour 0x407f7f7f.
void MenuPage::makeGameBackdrop() {
    static constexpr float kPos[4][4] = {{136.0f, 0.0f, 256.0f, 256.0f}, {136.0f, 256.0f, 256.0f, 192.0f},
                                         {0.0f, 0.0f, 136.0f, 448.0f}, {392.0f, 0.0f, 512.0f, 448.0f}};
    static constexpr float kUv[4][4] = {{0.0f, 0.0f, 256.0f, 256.0f}, {0.0f, 0.0f, 256.0f, 192.0f},
                                        {0.0f, 0.0f, 1.0f, 1.0f}, {0.0f, 0.0f, 1.0f, 1.0f}};
    static constexpr int kTex[4] = {0x3d, 0x3e, 0x3d, 0x3d};
    if (!textures().isLoaded(0x3d)) textures().load(0x3d);   // FUN_00106090(0x4e6a68, 0x3d, 1)
    if (!textures().isLoaded(0x3e)) textures().load(0x3e);
    for (int i = 0; i < 4; ++i) {
        if (backdropTiles[i] != nullptr) continue;
        const float* p = kPos[i];
        backdropTiles[i] = sprites().create(p[0] - 1.0f, p[1] - 1.0f, -5.0f, p[2], p[3], kUv[i][0], kUv[i][1], kUv[i][2], kUv[i][3], kTex[i], 0);
        if (backdropTiles[i] != nullptr) backdropTiles[i]->color = 0x407f7f7f;
    }
}
// orig FUN_00164a30 (work/decompiled/00164a30.c) + FUN_00164d00 (work/decompiled/00164d00.c)
void MenuPage::makeTitle(const char* title) {
    char buf[80] = {};
    std::memcpy(buf, title, std::min<size_t>(std::strlen(title), 0x40));
    titleBar = sprites().create(512.0f, 40.0f, 0.5f, 512.0f, 4.0f, 0, 0, 0, 0, kTexNone, 0);
    if (titleBar) titleBar->color = 0x703f3f3f;
    if (titleBar) titleBar->wide = kWideLine;   // PORT widescreen: the flat grey rule joins both area edges
    if (gradientTextureId() != -1) {
        underline = sprites().create(512.0f, 41.0f, 0.5f, 220.0f, 2.0f, 0.0f, 0.0f, 32.0f, 1.0f, gradientTextureId(), 1);
        if (underline) underline->color = 0x407f7f7f;
        if (underline) underline->wide = kWideSheen;   // PORT widescreen: the sheen travels along the whole rule
    }
    float x = 0.0f;
    for (int i = 0; i < 0x40; ++i) {
        const char c = buf[i];
        if (c < 'A' || c > 'Z') { x += 16.0f; continue; }
        const float* uv = kTitleLetterUv[c - 'A'];
        const float w = uv[2] - uv[0];
        const float h = uv[3] - uv[1];
        const int tex = (c == 'Q' || c > 'U') ? 0x2d : 0x2c;
        letters[i] = sprites().create(x + 26.0f, titleY, 0.9f, w, h, 0, 0, 0, 0, tex, 1);
        if (letters[i]) {
            letters[i]->color = 0x407f7f7f;
            letters[i]->cy = -80.0f;
            // FUN_00164d00: set 2 (tex 0x2d, TEX_UI07 128x64) divides v by 64
            const float vdiv = (c == 'Q' || c > 'U') ? 64.0f : 128.0f;
            letters[i]->u0 = uv[0] / 128.0f; letters[i]->v0 = uv[1] / vdiv;
            letters[i]->u1 = uv[2] / 128.0f; letters[i]->v1 = uv[3] / vdiv;
        }
        x += w;
    }
}
// orig FUN_001664b0 (work/decompiled/001664b0.c): 61 pieces from tex 0x2e (z 1.1) + help bar background from tex 0x25
void MenuPage::makeFrame2(int variant) {
    for (int i = 0; i < 0x3d; ++i) {
        const float* p = &kMenuFrame2Pos[i][variant * 4];
        const float* uv = kMenuFrame2Uv[i];
        frame2[i] = sprites().create(p[0] - 1.0f, p[1] - 1.0f, 1.1f, p[2], p[3], uv[0], uv[1], uv[2], uv[3], 0x2e, 1);
        if (frame2[i]) frame2[i]->color = 0x407f7f7f;
        if (frame2[i]) frame2[i]->wide = frame2WideRole(i, p, frame2[i]->anchor);   // PORT widescreen
    }
    const float* hp = kMenuHelpBarPos[variant];
    helpBg = sprites().create(hp[0] - 1.0f, hp[1] - 1.0f, 1.1f, hp[2], hp[3], kMenuHelpBarUv[0], kMenuHelpBarUv[1], kMenuHelpBarUv[2], kMenuHelpBarUv[3], 0x25, 1);
    if (helpBg) helpBg->color = 0x407f7f7f;
    // PORT widescreen: the help-bar background (a flat 8x8 texel patch of TEX_UI02) continues to the moved brackets
    if (helpBg && hp[2] > 0.0f) {
        if (portWideLeft != 0 && hp[0] == 26.0f) helpBg->wide |= kWidePadL;
        if (hp[0] + hp[2] == 486.0f) helpBg->wide |= kWidePadR;
    }
}
// PORT widescreen role of a FUN_001664b0 piece (kMenuFrame2Pos / kMenuFrame2Uv, presentation only). The table has
// left caps 0..3 and 58 (x 0..26: help-bar connector, left bracket), right caps 4..11 and 59 (x 486..512: right
// bracket, connector and end column; in variant 5 pieces 5 / 6 / 60 close the narrow window at x 366 instead), and
// 20x4 straight pipe segments 12..57 (top row uv y 81..85, bottom row uv y 85..89) from x 26 to x 486. Caps at the
// raster edges move to the area edges (anchor); the segment starting at x 26 / ending at x 486 repeats into the gap.
int MenuPage::frame2WideRole(int i, const float* p, int& anchor) const {
    if (p[2] <= 0.0f || p[3] <= 0.0f) return 0;
    const bool leftCap = (i >= 0 && i <= 3) || i == 58;
    const bool rightCap = (i >= 4 && i <= 11) || i == 59 || i == 60;
    if (leftCap && portWideLeft != 0 && p[0] + p[2] <= 26.0f) anchor = kAnchorLeft;
    if (rightCap && p[0] >= 486.0f) anchor = kAnchorRight;
    if (i < 12 || i > 57) return 0;
    int role = 0;
    if (portWideLeft != 0 && p[0] == 26.0f) role |= kWideTileL;
    if (p[0] + p[2] == 486.0f) role |= kWideTileR;
    return role;
}
// orig FUN_00164dd0 (work/decompiled/00164dd0.c): title letters drop in, bar slides left, underline scrolls
void MenuPage::animateTitle() {
    if (letters[0] == nullptr && titleBar == nullptr) return;   // the original tests +0x34 only
    if (letters[0] == nullptr) return;
    for (int i = 0; i < 0x40; ++i) if (letters[i]) letters[i]->cy = titleY + letters[i]->hh / 2.0f;
    if (titleBar) { if (256.0f < titleBar->cx && fade().finished()) titleBar->cx -= 32.0f; }
    if (underline && fade().finished()) {
        underline->cx -= 16.0f;
        if (underline->cx < -512.0f) underline->cx = underline->hw + 512.0f;
    }
    if (titleY < 28.0f) {
        titleY += 4.0f;
        if (28.0f <= titleY) { titleY = 28.0f; titleSettled = 1; }
    }
}
// orig FUN_00164460 (work/decompiled/00164460.c)
void MenuPage::freeFrame() {
    if (frameMade == 0) return;
    frameMade = 0;
    bool tiles = false;
    for (Sprite*& s : backdropTiles) {
        if (s == nullptr) continue;
        sprites().destroyRef(s);
        tiles = true;
    }
    if (tiles) {
        textures().release(0x3d);   // FUN_00105f00(0x4e6a68, 0x3d)
        textures().release(0x3e);
    }
    sprites().destroyRef(titleBar);
    sprites().destroyRef(underline);
    for (Sprite*& s : letters) sprites().destroyRef(s);
    for (Sprite*& s : frame) sprites().destroyRef(s);
}
// orig FUN_00166640 (work/decompiled/00166640.c)
void MenuPage::freeFrame2() {
    for (Sprite*& s : frame2) sprites().destroyRef(s);
    sprites().destroyRef(helpBg);
}

// orig FUN_00165a10 (work/decompiled/00165a10.c): sub-window A (KEY ASSIGN choice frame) at vertical offset y
void MenuPage::openSubWindowA(float y) {
    if (subWindowA == 1) return;
    subWindowA = 1;
    for (int i = 0; i < 0x2d; ++i) {
        const float* p = kSubWinAPos[i];
        const float* uv = kSubWinAUv[i];
        subA[i] = sprites().create(p[0], y + p[1], 5.0f, p[2], p[3], uv[0], uv[1], uv[2], uv[3], 0x2e, 1);
        if (subA[i]) subA[i]->color = 0x407f7f7f;
    }
    for (int i = 0; i < 2; ++i) {
        const float* p = kSubWinAPos2[i];
        const float* uv = kSubWinAUv2[i];
        subA2[i] = sprites().create(p[0], y + p[1], 5.0f, p[2], p[3], uv[0], uv[1], uv[2], uv[3], 0x27, 1);
        if (subA2[i]) subA2[i]->color = 0x407f7f7f;
    }
}
// orig FUN_00165b80 (work/decompiled/00165b80.c)
void MenuPage::closeSubWindowA() {
    subWindowA = 0;
    for (Sprite*& s : subA) sprites().destroyRef(s);
    for (Sprite*& s : subA2) sprites().destroyRef(s);
}
// orig FUN_00165d90 (work/decompiled/00165d90.c): sub-window B (KEY ASSIGN L/R pair frame)
void MenuPage::openSubWindowB() {
    if (subWindowB == 1) return;
    subWindowB = 1;
    for (int i = 0; i < 0x2b; ++i) {
        const float* p = kSubWinBPos[i];
        const float* uv = kSubWinBUv[i];
        subB[i] = sprites().create(p[0], p[1], 5.0f, p[2], p[3], uv[0], uv[1], uv[2], uv[3], 0x2e, 1);
        if (subB[i]) subB[i]->color = 0x407f7f7f;
    }
    for (int i = 0; i < 2; ++i) {
        const float* p = kSubWinBPos2[i];
        const float* uv = kSubWinBUv2[i];
        subB2[i] = sprites().create(p[0], p[1], 5.0f, p[2], p[3], uv[0], uv[1], uv[2], uv[3], 0x27, 1);
        if (subB2[i]) subB2[i]->color = 0x407f7f7f;
    }
}
// orig FUN_00165ef0 (work/decompiled/00165ef0.c)
void MenuPage::closeSubWindowB() {
    subWindowB = 0;
    for (Sprite*& s : subB) sprites().destroyRef(s);
    for (Sprite*& s : subB2) sprites().destroyRef(s);
}

// orig FUN_00166100 (work/decompiled/00166100.c): sub-window C (SOUND TEST panel)
void MenuPage::openSubWindowC() {
    if (subWindowC == 1) return;
    subWindowC = 1;
    for (int i = 0; i < 0x1d; ++i) {
        const float* p = kSubWinCPos[i];
        const float* uv = kSubWinCUv[i];
        subC[i] = sprites().create(p[0], p[1], 1.0f, p[2], p[3], uv[0], uv[1], uv[2], uv[3], 0x2e, 1);
    }
    const float* p = kSubWinCPart[0];
    const float* uv = kSubWinCPart[1];
    subC2 = sprites().create(p[0], p[1], 1.0f, p[2], p[3], uv[0], uv[1], uv[2], uv[3], 0x27, 1);
}
// orig FUN_00166240 (work/decompiled/00166240.c)
void MenuPage::closeSubWindowC() {
    subWindowC = 0;
    for (Sprite*& s : subC) sprites().destroyRef(s);
    sprites().destroyRef(subC2);
}
// orig FUN_00166420 (work/decompiled/00166420.c)
void MenuPage::colorSubWindowC(uint32_t alphaBits) {
    if (subWindowC == 0) return;
    const uint32_t c = alphaBits + 0x7f7f7f;
    for (Sprite* s : subC)
        if (s != nullptr) s->color = c;
    if (subC2 != nullptr) subC2->color = c;
}

// ---- menu object -----------------------------------------------------------------------------------------------
// orig FUN_00167390 (work/decompiled/00167390.c)
void MenuObject::loadPageTextures() {
    TextureRegistry& t = textures();
    for (int id : {0x2c, 0x2d, 0x2e, 0x30, 0x31}) if (!t.isLoaded(id)) t.load(id);
    if (frontEndScene()) for (int id : {0x23, 0x25, 0x27, 0x28}) if (!t.isLoaded(id)) t.load(id);
}
void MenuObject::releasePageTextures() {
    TextureRegistry& t = textures();
    for (int id : {0x2c, 0x2d, 0x2e, 0x30, 0x31}) if (t.isLoaded(id)) t.release(id);
    if (frontEndScene()) for (int id : {0x23, 0x25, 0x27, 0x28}) if (t.isLoaded(id)) t.release(id);
}
// the switch in FUN_001677c0 (work/decompiled/001677c0.c)
MenuPage* MenuObject::createPage(int mode) {
    switch (mode) {
    case 0: case 1: case 2: case 3: case 4: case 5: case 6: return newDatabasePage(mode);
    case 8: return newNameEntryPage();
    case 9: return newFileSelectPage();
    case 10: return newGameConfigPage();
    case 11: return newPcSettingsPage(scenes().isCurrent("Game....") ? 1 : 0);   // PORT: PC SETTINGS (--menu-at 11)
    default: return newOptionPage();   // -1 and 7
    }
}
// orig FUN_001677c0
void MenuObject::open(int mode) {
    loadPageTextures();
    messageLog().clear();                     // FUN_0015d0a0(0x5180f0): message log lines, counters, origin (24, 290)
    isOpen_ = 1;
    counter_ = 0;
    if (page_ == nullptr) {
        page_ = createPage(mode);
        page_->open();
    }
}
// orig FUN_00167910 (work/decompiled/00167910.c)
void MenuObject::close() {
    isOpen_ = 0;
    if (page_ != nullptr) {
        if (page_->isActive()) page_->close();
        delete page_;   // FUN_00168d60
        page_ = nullptr;
    }
    releasePageTextures();
}
// orig FUN_00167d80 (work/decompiled/00167d80.c)
void MenuObject::update() {
    if (page_ != nullptr) {
        if (!page_->isActive()) close();
        else {
            if (counter_ >= 0x1f) {
                page_->mouseInput();               // PORT: mouse hover (no-op unless the mouse moved over an item)
                page_->input();
            }
            page_->update();
            if (page_->wantsNext()) {
                MenuPage* next = page_->nextPage();
                page_->close();
                delete page_;
                page_ = nullptr;
                if (next == nullptr) close();
                else { page_ = next; page_->open(); counter_ = 0; }
            }
            counter_ += 1;
            gradientAnimate();
            // PORT: register the page's mouse items once it takes input (next tick's counter >= 0x1f)
            if (page_ != nullptr && page_->isActive() && counter_ >= 0x1f) page_->mouseItems();
        }
    }
    selectOpensConfig();                      // FUN_00167eb0
    messageWindowTick();                      // FUN_001655a0 (message box window 0x51b010)
}
// orig FUN_00167eb0 (work/decompiled/00167eb0.c): SELECT on the settled item screen of the Game scene opens GAME CONFIG
void MenuObject::selectOpensConfig() {
    const ItemScreen& is = itemScreen();      // 0x517b60: +8 iRam00517b68, FUN_001561b0, +0xc iRam00517b6c
    if (scenes().isCurrent("Game....") && is.isOpen() && is.ready() && !is.contextOpen() && !isOpen() &&
        !messageBox().isOpen() && (pad().port[0].pressed & 0x100) != 0) {
        open(10);                             // FUN_001677c0(0x51b140, 10)
        // uRam0051aa1c / uRam0051aa20 = 0x5180f0 + 0x2928 + 4 * slot: the enabled words of help slots 1 / 2 (the item
        // screen's description lines), hidden while the menu pages are open; FUN_0015e150 enables them again.
        if (is.isOpen()) { helpBar().setEnabled(1, 0); helpBar().setEnabled(2, 0); }
        sound().playSe(1.0f, 8);              // FUN_00115390(1.0, snd, 8)
    }
}
// The UI part of FUN_00101bd0 in its original order: FUN_0015c380, FUN_00167d80, FUN_00168010(menu, cRam0051af64).
// Called from Engine::tick() right after the message box update, as in the original.
void uiTick() {
    helpBar().update();                       // FUN_0015c380(0x5180f0)
    gMenu.update();                           // FUN_00167d80(0x51b140)
    gMenu.updateBrightness(options().brightness);   // FUN_00168010(0x51b140, cRam0051af64)
}
// orig FUN_00168010 (work/decompiled/00168010.c): full-screen dimming quad for the BRIGHTNESS option
void MenuObject::updateBrightness(int8_t brightness) {
    if (scenes().isCurrent("BootMC..")) return;
    if (brightness >= 11 || brightness < 0) return;
    if (brightnessCache_ == brightness) return;
    brightnessCache_ = brightness;
    if (brightness == 10) {
        sprites().destroyRef(brightnessQuad_);
    } else {
        sprites().destroyRef(brightnessQuad_);
        const int flag = (brightnessCache_ < 10) ? 3 : 0;
        brightnessQuad_ = sprites().create(-2.0f, -2.0f, 50.0f, 516.0f, 452.0f, 0, 0, 0, 0, kTexNone, flag);
    }
    if (brightnessQuad_ != nullptr && brightnessCache_ < 10) {
        float a = (1.0f - static_cast<float>(brightnessCache_) * 0.1f) * 64.0f;
        if (a >= 2.1474836e+09f) a -= 2.1474836e+09f;
        brightnessQuad_->color = static_cast<uint32_t>(static_cast<int>(a)) << 24;
    }
}
}
