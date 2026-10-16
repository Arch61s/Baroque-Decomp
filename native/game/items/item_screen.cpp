#include "game/items/item_screen.hpp"
#include "game/items/item_effects.hpp"
#include "game/render2d.hpp"   // PORT: keycapLabel (keyboard button prompts)
#include "game/ui/help_bar.hpp"
#include "game/ui/sprite.hpp"
#include "game/ui/text.hpp"
#include <cstdio>
#include <cstring>

namespace baroque::game {

// 0x48f580 [V words]: per class {action0..3}; -1 = none
const int kContextActions[14][4] = {{0, 1, 2, 11}, {0, 1, 2, 11}, {0, 1, 2, 11}, {3, 1, 2, 11}, {3, 1, 2, 11}, {4, 1, 2, 11}, {5, 1, 2, 11},
                                    {6, 1, 2, 11}, {7, 1, 2, 11}, {8, 9, 1, 11}, {3, 1, 2, 11}, {10, 1, 2, 11}, {0, 1, 2, 11}, {12, 1, 2, 11}};

namespace {
#include "game/items/item_screen_tables.inc"

constexpr uint32_t kIdBX03 = itemId("BX03");     // 0x44bf20
constexpr uint32_t kIdST10 = itemId("ST10");     // scattered bytes 0x491c58.. (FUN_00158890 first test)
constexpr uint32_t kIdST10b = itemId("ST10");    // 0x44bf18 (second test)
constexpr uint32_t kIdGN00 = itemId("GN00");     // 0x44be58
constexpr uint32_t kIdWM22 = itemId("WM22");     // 0x44be60
constexpr uint32_t kTextGrey = 0xbbbbbb;         // DAT_0044c190
constexpr uint32_t kTextDefault = 0x40bbbbbb;    // DAT_0044c198
constexpr uint32_t kOpaque = 0x407f7f7f;
// text-window slots 1 / 2 of 0x5180f0: rect (24, 369, 480, 40) [V 0x41c00000 0x43b88000 0x43f00000 0x42200000]
constexpr float kDescX = 24.0f, kDescY = 369.0f, kDescW = 480.0f, kDescH = 40.0f;
const char kWmSuffix[] = "\x92\x8e";             // 0x44be68 (JP name suffix; its strlen is cut / skipped)

// FUN_00166820 with the texture size 128 (every caller passes texW = texH = 128 [V disasm])
ui::Sprite* mk(float x, float y, float z, float w, float h, float u0, float v0, float u1, float v1, int tex, int flag) {
    return ui::sprites().create(x, y, z, w, h, u0, v0, u1, v1, tex, flag);
}
void setColour(ui::Sprite* s, uint32_t c) { if (s != nullptr) s->color = c; }
void kill(ui::Sprite*& s) { ui::sprites().destroyRef(s); }
template <size_t N> void killAll(ui::Sprite* (&a)[N]) { for (ui::Sprite*& s : a) kill(s); }
// the per-tick UV write of FUN_001591e0 for tabs / arrows: (u0 + 1) / 128 .. (v1 - 1) / 128
void setInsetUv(ui::Sprite* s, const float* uv) {
    if (s == nullptr) return;
    s->u0 = (uv[0] + 1.0f) / 128.0f;
    s->v0 = (uv[1] + 1.0f) / 128.0f;
    s->u1 = (uv[2] - 1.0f) / 128.0f;
    s->v1 = (uv[3] - 1.0f) / 128.0f;
}
// FUN_00112e80(0x4e7e90, fmt, ...) with the globals the caller set
void text(float x, float y, float z, uint32_t colour, const char* fmt, const char* arg) {
    ui::TextRenderer& t = ui::text();
    t.x = x; t.y = y; t.z = z; t.color = colour;
    t.draw(fmt, arg);
}
// strncat(dst, src, n) bounded to the destination
void appendPrefix(char* dst, size_t cap, const char* src, size_t n) {
    size_t l = std::strlen(dst);
    for (size_t i = 0; i < n && src[i] != '\0' && l + 1 < cap; ++i) dst[l++] = src[i];
    dst[l] = '\0';
}
size_t lenMinusSuffix(const char* s) {
    const size_t l = std::strlen(s), k = std::strlen(kWmSuffix);
    return l >= k ? l - k : 0;   // UNVERIFIED: the original's size_t subtraction wraps for names shorter than 2 bytes
}
}  // namespace

// orig FUN_001520d0 (work/decompiled/001520d0.c), screen fields
void ItemScreen::reset() {
    releaseAll();
    *this = ItemScreen{};
}

int ItemScreen::pageCount(ItemSystem& sys) const {   // orig FUN_0015c020 (work/decompiled/0015c020.c)
    if (static_cast<unsigned>(page_ + 1) < 2u) return sys.inventory->count;
    if (page_ == 1) return sys.bag->capacity(*sys.game);
    return -1;
}
Item* ItemScreen::itemAt(ItemSystem& sys, int idx) const {
    if (idx < 0) return nullptr;
    if (page_ == 0) return idx < sys.inventory->count ? sys.inventory->slots[idx] : nullptr;
    if (page_ == 1) return sys.bag->get(idx);   // FUN_001daff0(0x53dc00, idx)
    return nullptr;
}
// orig FUN_001561b0 (work/decompiled/001561b0.c)
bool ItemScreen::ready() const { return closeCountdown_ == -1 && fade_ >= 0x40 && open_ != 0; }
// orig FUN_0015bfe0 (work/decompiled/0015bfe0.c)
void ItemScreen::clampCursor(ItemSystem& sys) {
    if (static_cast<unsigned>(page_ + 1) < 2u && sys.inventory->count <= cursor_[0]) cursor_[0] = sys.inventory->count - 1;
}
// orig FUN_00152930 (work/decompiled/00152930.c): list page of the kept cursor
void ItemScreen::keepCursorPage(ItemSystem& sys) {
    const int lp = listPage_[0];
    if (lp == 0 || cursor_[0] > 9) {
        if (lp == 0 || sys.inventory->count > 10) {
            if (lp == 0 && cursor_[0] > 9) listPage_[0] = 1;
        } else {
            listPage_[0] = 0;
        }
    } else {
        listPage_[0] = 0;
    }
}

int ItemScreen::spriteCount() const {
    int n = 0;
    auto c = [&](const ui::Sprite* s) { if (s != nullptr) ++n; };
    c(backdrop_); for (auto* s : frame_) c(s); for (auto* s : frameVar_) c(s); for (auto* s : frame03_) c(s);
    for (auto* s : hints_) c(s); for (auto* s : panelPieces_) c(s); for (auto* s : panel2Pieces_) c(s);
    c(panel_); c(titleBar_); c(caption_); c(sortCaption_); c(sortIcon_); for (auto* s : tabs_) c(s); for (auto* s : arrows_) c(s);
    for (auto* s : equipMarks_) c(s); for (auto* s : sealMarks_) c(s); for (auto* s : unusedMarks_) c(s); for (auto* s : classIcons_) c(s);
    for (auto& r : numDigits_) for (auto* s : r) c(s);
    for (auto* s : ctxPieces_) c(s); c(ctxBox_); c(expLabel_); for (auto* s : expDigits_) c(s);
    c(atkLabel_); for (auto* s : atkDigits_) c(s); c(defLabel_); for (auto* s : defDigits_) c(s);
    return n;
}

// ---- sprite builders (FUN_00166820 stack arguments v1 / texW / texH read from the disassembly) ------------------

// orig FUN_00153790 (work/decompiled/00153790.c): z runs from -0.8 in steps of 0.01 over every piece
void ItemScreen::buildFrame() {
    float z = -0.8f;
    for (int i = 0; i < 32; ++i) {
        const float* r = kIsFrameRect[i];
        const float* uv = kIsFrameUv[i];
        frame_[i] = mk(r[0] - 1.0f, r[1] - 1.0f, z, r[2], r[3], uv[0], uv[1], uv[2], uv[3], kTexUi02, 1);
        setColour(frame_[i], kOpaque);
        z = z + 0.01f;
    }
    for (int i = 0; i < 8; ++i) {
        const float* r = kIsFrameVarRect[i][hintVariant_];
        const float* uv = kIsFrameVarUv[i];
        frameVar_[i] = mk(r[0] - 1.0f, r[1] - 1.0f, z, r[2], r[3], uv[0], uv[1], uv[2], uv[3], kTexUi02, 1);
        setColour(frameVar_[i], kOpaque);
        z = z + 0.01f;
    }
    for (int i = 0; i < 4; ++i) {
        const float* r = kIsFrame03Rect[i];
        const float* uv = kIsFrame03Uv[i];
        frame03_[i] = mk(r[0] - 1.0f, r[1] - 1.0f, z, r[2], r[3], uv[0], uv[1], uv[2], uv[3], kTexUi03, 1);
        setColour(frame03_[i], kOpaque);
        z = z + 0.01f;
    }
    if (hintVariant_ != 1) {
        if (hintVariant_ == 2 || hintVariant_ == 0) {   // 0x48ff50 rect / 0x48ff70 UV
            const float* r = kIsHintRect[0];
            const float* uv = kIsHintUv[0];
            hints_[0] = mk(r[0] - 1.0f, r[1] - 1.0f, z, r[2], r[3], uv[0], uv[1], uv[2], uv[3], kTexUi04, 1);
            setColour(hints_[0], kOpaque);
            z = z + 0.01f;
        }
        if (hintVariant_ == 3 || hintVariant_ == 0) {   // 0x48ff60 rect / 0x48ff80 UV
            const float* r = kIsHintRect[1];
            const float* uv = kIsHintUv[1];
            hints_[1] = mk(r[0] - 1.0f, r[1] - 1.0f, z, r[2], r[3], uv[0], uv[1], uv[2], uv[3], kTexUi04, 1);
            setColour(hints_[1], kOpaque);
        }
    }
}

// orig FUN_00153b20 (work/decompiled/00153b20.c)
void ItemScreen::releaseFrame() {
    if (hintVariant_ != 1) killAll(hints_);
    killAll(frame03_);
    killAll(frameVar_);
    killAll(frame_);
}

// orig FUN_00153710 / FUN_00153750 (work/decompiled/00153710.c, 00153750.c)
void ItemScreen::setFrameVariant(int v) {
    releaseFrame();
    hintVariant_ = v;
    buildFrame();
}

// orig FUN_00155970 (work/decompiled/00155970.c)
void ItemScreen::buildTabs(int n) {
    tabCount_ = n;
    for (int i = 0; i < n && i < 5; ++i) {   // UNVERIFIED: the table 0x491500 has 5 rows; a bag of > 50 would overrun
        const float* r = kIsTabRect[i];
        tabs_[i] = mk(r[0], r[1], -0.7f, r[2], r[3], 0.0f, 0.0f, 0.0f, 0.0f, kTexUi03, 1);   // 0xbf333333
    }
}

// orig FUN_00155500 (work/decompiled/00155500.c)
void ItemScreen::buildPanel(ItemSystem& sys) {
    const int two = twoPage_;
    panel_ = mk(258.0f, 96.0f, -0.8f, 226.0f, 207.0f, 50.0f, 42.0f, 58.0f, 50.0f, kTexUi02, 0);
    titleBar_ = mk(258.0f, 313.0f, -0.8f, 226.0f, 20.0f, 50.0f, 42.0f, 58.0f, 50.0f, kTexUi02, 0);
    float z = -0.79f;
    for (int i = 0; i < 0x38; ++i) {
        const float* r = kIsPanelRect[i];
        const float* uv = kIsPanelUv[i];
        panelPieces_[i] = mk(r[0], r[1], z, r[2], r[3], uv[0], uv[1], uv[2], uv[3], kTexUi03, 1);
        z = z + 0.01f;
    }
    for (int i = 0; i < 0x12; ++i) {
        const float* r = kIsPanel2Rect[i][two != 1 ? 1 : 0];
        const float* uv = kIsPanel2Uv[i];
        panel2Pieces_[i] = mk(r[0], r[1], z, r[2], r[3], uv[0], uv[1], uv[2], uv[3], kTexUi03, 1);
        z = z + 0.01f;
    }
    for (int i = 0; i < 2; ++i) {
        const float* r = kIsArrowRect[i];
        arrows_[i] = mk(r[0], r[1], z, r[2], r[3], 0.0f, 0.0f, 0.0f, 0.0f, kTexUi03, 1);
        z = z + 0.01f;
    }
    if (page_ == 0) {
        buildTabs(2);
    } else {
        const int cap = sys.bag->capacity(*sys.game);   // FUN_001dacd0(0x53dc00)
        buildTabs(cap / 10 + (cap % 10 != 0 ? 1 : 0));
    }
    caption_ = mk(336.0f, 74.0f, z, 38.0f, 11.0f, 63.0f, 0.0f, 101.0f, 11.0f, kTexUi04, 0);   // "ITEM"
    if (twoPage_ == 1) {   // sort hint: caption TEX_UI04 (0,0)-(63,13) + square glyph TEX_UI05 (32,0)-(48,17)
        sortCaption_ = mk(410.0f, 50.0f, z + 0.01f, 63.0f, 13.0f, 0.0f, 0.0f, 63.0f, 13.0f, kTexUi04, 0);
        sortIcon_ = mk(391.0f, 48.0f, z + 0.01f + 0.01f, 16.0f, 17.0f, 32.0f, 0.0f, 48.0f, 17.0f, kTexUi05, 0);
    }
    refreshRows(sys);
}

// orig FUN_00155a30 (work/decompiled/00155a30.c)
void ItemScreen::releasePanel() {
    kill(panel_);
    kill(titleBar_);
    killAll(panelPieces_);
    killAll(panel2Pieces_);
    if (tabCount_ != 0) for (int i = 0; i < tabCount_ && i < 5; ++i) kill(tabs_[i]);
    killAll(arrows_);
    kill(caption_);
    if (twoPage_ != 0) {
        kill(sortCaption_);
        kill(sortIcon_);
    }
    releaseRows();
}

// orig FUN_001554d0 (work/decompiled/001554d0.c)
void ItemScreen::dropSecondPage(ItemSystem& sys) {
    releasePanel();
    twoPage_ = 0;
    buildPanel(sys);
}

// orig FUN_0015a350 (work/decompiled/0015a350.c)
void ItemScreen::refreshRows(ItemSystem& sys) {
    releaseRows();
    buildRows(sys);
}

// orig FUN_0015ac90 (work/decompiled/0015ac90.c)
void ItemScreen::releaseRows() {
    killAll(equipMarks_);
    killAll(sealMarks_);
    killAll(unusedMarks_);
    killAll(classIcons_);
    for (auto& r : numDigits_) killAll(r);
}

// orig FUN_0015a580 (work/decompiled/0015a580.c)
void ItemScreen::buildRows(ItemSystem& sys) {
    const int p = page_ == 1 ? 1 : 0;
    int end = pageCount(sys);
    const int lim = (listPage_[p] + 1) * 10;
    if (lim <= end) end = lim;
    // equipped marks: TEX_UI04 (62,13)-(78,31), 16x18 at (270, 102 + 20 row) [V v1 31]
    if (page_ == 0) {
        int k = 0;
        unsigned row = 0;
        for (int i = listPage_[p] * 10; i < end; ++i, ++row) {
            if (sys.inventory->equippedSlotOfIndex(i) != -1 && k < 4) {   // FUN_001d7c80
                equipMarks_[k++] = mk(270.0f, static_cast<float>(row) * 20.0f + 102.0f, 0.0f, 16.0f, 18.0f, 62.0f, 13.0f, 78.0f, 31.0f, kTexUi04, 1);
            }
        }
    }
    // sealed marks (+0x70 & 2): TEX_UI04 (62,31)-(78,49) [V v1 49]
    {
        int k = 0;
        unsigned row = 0;
        for (int i = listPage_[p] * 10; i < end; ++i, ++row) {
            const Item* it = itemAt(sys, i);
            if (it != nullptr && (it->flags & kItemSealed) != 0 && k < 10)
                sealMarks_[k++] = mk(270.0f, static_cast<float>(row) * 20.0f + 102.0f, 0.0f, 16.0f, 18.0f, 62.0f, 31.0f, 78.0f, 49.0f, kTexUi04, 1);
        }
    }
    // value column: SD base+mod "%3d" (0x44bf60), CT the same with DEF, GN / identified BN count "K%2d" (0x44bf68);
    // three 7x12 digits at x 451 + 8i, y 108 + 20 row, layer flag 0
    {
        int k = 0;
        unsigned row = 0;
        for (int i = listPage_[p] * 10; i < end; ++i, ++row) {
            const Item* it = itemAt(sys, i);
            if (it == nullptr) continue;
            const int cls = it->cls;
            if (!(cls == kClsSD || cls == kClsGN || cls == kClsCT || (cls == kClsBN && (it->flags & kItemIdentified) != 0))) continue;
            char buf[16] = {};   // auStack_4 (4 bytes in the original)
            if (cls == kClsSD) std::snprintf(buf, sizeof buf, "%3d", it->atkBase + it->atkMod);
            else if (cls == kClsGN) std::snprintf(buf, sizeof buf, "K%2d", it->count);
            else if (cls == kClsCT) std::snprintf(buf, sizeof buf, "%3d", it->defBase + it->defMod);
            else std::snprintf(buf, sizeof buf, "K%2d", it->count);
            if (k >= 10) continue;
            for (int d = 0; d < 3; ++d) {
                ui::Sprite* s = mk(static_cast<float>(d * 8) + 451.0f, static_cast<float>(row) * 20.0f + 102.0f + 6.0f, 0.0f, 7.0f, 12.0f,
                                   0.0f, 0.0f, 0.0f, 0.0f, kTexUi00, 0);
                numDigits_[k][d] = s;
                if (s != nullptr) setDigitGlyph(*s, buf[d]);   // FUN_00166a50
            }
            ++k;
        }
    }
    // class icon (SD / CT): TEX_UI04 at (435, 102 + 20 row) 16x18, UVs written after the call
    {
        int k = 0;
        unsigned row = 0;
        for (int i = listPage_[p] * 10; i < end; ++i, ++row) {
            const Item* it = itemAt(sys, i);
            if (it == nullptr || static_cast<unsigned>(it->cls) >= 2u || k >= 10) continue;
            ui::Sprite* s = mk(435.0f, static_cast<float>(row) * 20.0f + 102.0f, 0.0f, 16.0f, 18.0f, 0.0f, 0.0f, 0.0f, 0.0f, kTexUi04, 1);
            classIcons_[k++] = s;
            if (s == nullptr) continue;
            if (it->cls == kClsSD || it->cls == kClsGN) {   // 0x3f1c0000 0x3dd00000 0x3f3c0000 0x3e780000
                s->u0 = 0.609375f; s->v0 = 0.1015625f; s->u1 = 0.734375f; s->v1 = 0.2421875f;
            } else {                                         // 0x3f1c0000 0x3e780000 0x3f3c0000 0x3ec40000
                s->u0 = 0.609375f; s->v0 = 0.2421875f; s->u1 = 0.734375f; s->v1 = 0.3828125f;
            }
        }
    }
}

// orig FUN_00152be0 (work/decompiled/00152be0.c)
void ItemScreen::releaseContext() {
    killAll(ctxPieces_);
    kill(ctxBox_);
}

// port: every slot (reset, and a re-open while the 8-tick close countdown runs, where the original overwrites the
// slot pointers and leaks the old sprites - UNVERIFIED whether that path is reachable)
void ItemScreen::releaseAll() {
    kill(backdrop_);
    kill(expLabel_); killAll(expDigits_);
    kill(atkLabel_); killAll(atkDigits_);
    kill(defLabel_); killAll(defDigits_);
    killAll(hints_);
    killAll(frame03_); killAll(frameVar_); killAll(frame_);
    kill(panel_); kill(titleBar_); killAll(panelPieces_); killAll(panel2Pieces_); killAll(tabs_); killAll(arrows_);
    kill(caption_); kill(sortCaption_); kill(sortIcon_);
    releaseRows();
    releaseContext();
}

// ---- open / close -------------------------------------------------------------------------------------------

// orig FUN_00157fa0 (work/decompiled/00157fa0.c)
void ItemScreen::openTick(ItemSystem& sys, const ItemScreenEnv& env) {
    if (openRequest_ == 0 || env.talkChoice || env.talkWindow) return;
    if (!env.systemMenuOpen && !ready() && !env.paused && !env.eventRunning && !env.talkBusy && env.floorGenerated) {
        openRequest_ = 0;
        bagRequest_ = 0;
        open(sys, 0, env);
        sys.game->se(10);
    } else {
        openRequest_ = 0;
    }
}

// orig FUN_001524b0 (work/decompiled/001524b0.c)
void ItemScreen::open(ItemSystem& sys, int page, const ItemScreenEnv& env) {
    if (open_ != 0 && closeCountdown_ == -1) return;
    if (open_ != 0) releaseAll();   // see releaseAll()
    page_ = page;
    ticks_ = 0;
    sys.game->logClear();                 // FUN_0015d0a0(0x5180f0)
    if (!sys.game->mapShown()) {          // iRam0050a3f8
        mapWasOpen_ = 0;
    } else {
        sys.game->mapHide();              // FUN_00126fb0(0x50a350)
        mapWasOpen_ = 1;
    }
    sys.game->bgmDuck(1);                 // FUN_001161d0(0x4e9790, 1)
    mode_ = 1;
    targetCursor_ = -1;
    open_ = 1;
    hintVariant_ = 1;
    twoPage_ = pageCount(sys) < 2 ? 0 : 1;
    closeCountdown_ = -1;
    fade_ = 0;
    if (page_ == 0) {
        if (cursor_[0] < sys.inventory->count && !env.cursorResetOption) {
            keepCursorPage(sys);
        } else {
            cursor_[0] = -1;
            listPage_[0] = 0;
        }
    } else if (page_ == 1) {
        const uint32_t c = static_cast<uint32_t>(cursor_[1]);
        const uint32_t cap = static_cast<uint32_t>(sys.bag->capacity(*sys.game));
        bool keep = false;
        if (c < cap) {   // unsigned compare
            if (c != 0xffffffffu && !env.cursorResetOption) keep = true;
            else cursor_[1] = 0;
        } else {
            cursor_[1] = 0;
        }
        if (!keep) listPage_[1] = 0;   // the keep path jumps over the +0x20 reset (LAB_00152618)
    }
    // backdrop (-5, -5, 522x458), untextured, colour written per tick by FUN_00159150
    backdrop_ = mk(-5.0f, -5.0f, -1.0f, 522.0f, 458.0f, 0.0f, 0.0f, 1.0f, 1.0f, kTexNoTexture, 0);
    // EXP / ATK / DEF labels (TEX_UI00, v1 24 [V]) and their 7x12 digit strips (tables 0x48f6e0 / 0x48f7b0)
    expLabel_ = mk(59.0f, 141.0f, 0.0f, 25.0f, 12.0f, 51.0f, 12.0f, 76.0f, 24.0f, kTexUi00, 1);
    for (int i = 0; i < 13; ++i) {
        const float* r = kIsExpDigitRect[i];
        expDigits_[i] = mk(r[0] - 1.0f, r[1] - 1.0f, 0.0f, r[2], r[3], 0.0f, 0.0f, 0.0f, 0.0f, kTexUi00, 1);
    }
    atkLabel_ = mk(58.0f, 159.0f, 0.0f, 27.0f, 12.0f, 76.0f, 12.0f, 103.0f, 24.0f, kTexUi00, 1);
    for (int i = 0; i < 10; ++i) {
        const float* r = kIsStatDigitRect[i];
        atkDigits_[i] = mk(r[0] - 1.0f, r[1] - 1.0f, 0.0f, r[2], r[3], 0.0f, 0.0f, 0.0f, 0.0f, kTexUi00, 1);
    }
    defLabel_ = mk(59.0f, 177.0f, 0.0f, 25.0f, 12.0f, 103.0f, 12.0f, 128.0f, 24.0f, kTexUi00, 1);
    for (int i = 0; i < 10; ++i) {
        const float* r = kIsStatDigitRect[i];
        defDigits_[i] = mk(r[0] - 1.0f, (r[1] + 18.0f) - 1.0f, 0.0f, r[2], r[3], 0.0f, 0.0f, 0.0f, 0.0f, kTexUi00, 1);
    }
    buildFrame();             // FUN_00153790
    buildDescription(sys);    // FUN_00153f00
    buildPanel(sys);          // FUN_00155500
}

// orig FUN_001561f0 (work/decompiled/001561f0.c); the caller clears +8
void ItemScreen::close(ItemSystem& sys) {
    if (open_ == 0) return;
    kill(backdrop_);
    kill(expLabel_);
    kill(atkLabel_);
    kill(defLabel_);
    killAll(expDigits_);
    killAll(atkDigits_);
    killAll(defDigits_);
    releaseFrame();           // FUN_00153b20
    releasePanel();           // FUN_00155a30
    if (mapWasOpen_ != 0) sys.game->mapShow();   // FUN_00126cc0(0x50a350)
    sys.game->bgmDuck(0);                        // FUN_001161d0(0x4e9790, 0)
    sys.game->talkItemScreenClosed();            // FUN_001a9ff0(0x53c6e0)
}

// orig FUN_0015c070 (work/decompiled/0015c070.c): the Collector's bag request +0x530 (written by the talk system
// FUN_001a3320) opens the bag page once no event camera (iRam0053c6e8) is active.
void ItemScreen::bagRequestTick(ItemSystem& sys, const ItemScreenEnv& env, bool playerDead, bool eventCamera, int& logEnabled) {
    if (playerDead) {
        bagRequest_ = 0;
    } else if (!env.floorGenerated || env.talkBusy) {
        bagRequest_ = 0;
    }
    if (bagRequest_ != 0 && !eventCamera) {
        bagRequest_ = 0;
        if (!playerDead) {
            logEnabled = 1;           // uRam0051abf0 = 1
            open(sys, 1, env);        // FUN_001524b0(0x517b60, 1) [a1 = 1 at 0x15c17c]
        }
    }
}

// orig FUN_001529a0 (work/decompiled/001529a0.c)
void ItemScreen::requestClose(ItemSystem& sys) {
    if (closeCountdown_ != -1) return;
    closeCountdown_ = 8;
    if (ctxOpen_ != 0) closeContext(sys);
    ui::helpBar().clear(1);   // FUN_0015c3f0(0x5180f0, 1)
    ui::helpBar().clear(2);
}

// orig FUN_00152a00 (work/decompiled/00152a00.c): box (275,134) 115x78 z 2 TEX_UI04 (50,42)-(58,50) layer 1 [V
// a1 = 1, v1 50], 24 pieces (table 0x491650 stride 48 bytes, UVs 0x491ad0) at z 2 + 0.01 i
void ItemScreen::openContext(ItemSystem& sys) {
    if (open_ == 0) return;
    ctxOpen_ = 1;
    ctxBox_ = mk(275.0f, 134.0f, 2.0f, 115.0f, 78.0f, 50.0f, 42.0f, 58.0f, 50.0f, kTexUi04, 1);
    setColour(ctxBox_, kOpaque);
    for (int i = 0; i < 24; ++i) {
        const float* r = kIsCtxRect[i];
        const float* uv = kIsCtxUv[i];
        ctxPieces_[i] = mk(r[0] - 1.0f, r[1] - 1.0f, static_cast<float>(static_cast<unsigned>(i)) * 0.01f + 2.0f, r[2], r[3],
                           uv[0], uv[1], uv[2], uv[3], kTexUi04, 1);
        setColour(ctxPieces_[i], kOpaque);
    }
    sys.game->se(4);
}
// orig FUN_00152ba0 (work/decompiled/00152ba0.c)
void ItemScreen::closeContext(ItemSystem& sys) {
    if (ctxOpen_ == 0) return;
    ctxOpen_ = 0;
    releaseContext();
    sys.game->se(5);
}

// ---- input --------------------------------------------------------------------------------------------------

// orig FUN_001580a0 (work/decompiled/001580a0.c)
void ItemScreen::cancel(ItemSystem& sys) {
    if (mode_ == 1) {
        if (ctxOpen_ == 0) {
            if (ready()) {
                requestClose(sys);
                sys.game->se(10);
            }
        } else {
            closeContext(sys);
        }
    } else if (mode_ == 3) {
        mode_ = 1;
        listPage_[0] = savedPage_;
        targetCursor_ = -1;
        clampCursor(sys);
        refreshRows(sys);
        buildDescription(sys);
        sys.game->se(2);
    }
}

// orig FUN_00158170 (work/decompiled/00158170.c)
void ItemScreen::pageLeft(ItemSystem& sys) {
    const int count = pageCount(sys);
    if (open_ == 0 || ctxOpen_ != 0 || count < 0xb) return;
    const int p = page_ == 1 ? 1 : 0;
    if (listPage_[p] - 1 < 0) return;
    listPage_[p]--;
    if (mode_ == 1 || page_ == 1) cursor_[p] = listPage_[p] * 10;
    else if (mode_ == 3) targetCursor_ = listPage_[p] * 10;
    if (mode_ == 3 && (!useLegal(sys, cursor_[0], targetCursor_) || cursor_[0] == targetCursor_)) targetNext(sys);
    refreshRows(sys);
    buildDescription(sys);
    sys.game->se(8);
}

// orig FUN_001582c0 (work/decompiled/001582c0.c)
void ItemScreen::pageRight(ItemSystem& sys) {
    const int count = pageCount(sys);
    if (open_ == 0 || ctxOpen_ != 0 || count < 0xb) return;
    const int p = page_ == 1 ? 1 : 0;
    if (count / 0xb < listPage_[p] + 1) return;
    listPage_[p]++;
    if (mode_ == 1 || page_ == 1) cursor_[p] = listPage_[p] * 10;
    else if (mode_ == 3) targetCursor_ = listPage_[p] * 10;
    if (mode_ == 3 && (!useLegal(sys, cursor_[0], targetCursor_) || cursor_[0] == targetCursor_)) targetNext(sys);
    refreshRows(sys);
    buildDescription(sys);
    sys.game->se(8);
}

// orig FUN_00157410 (work/decompiled/00157410.c), open-screen path
void ItemScreen::stepPrev(ItemSystem& sys) {
    const int count = pageCount(sys);
    const int p = page_ == 1 ? 1 : 0;
    if (open_ == 0 && count < 2) return;
    pageWrapped_ = 0;
    if (open_ != 0) { stepListPrev(sys, count, p); return; }
    cursor_[p]--;
    if (cursor_[p] < 0) cursor_[p] = count - 1;
    sys.game->se(6);
}

// PORT: with the screen open, the original FUN_00157410 / FUN_001575f0 keep Up / Down inside the
// current 10-row list page (past the last row: "Yourself" on the inventory, then the top of the same page), and only
// Left / Right reach the next page. Here Up / Down (and the wheel) run through the whole list instead: page 0 ->
// page 1 -> "Yourself" (inventory only) -> back to the top, turning the list page with its SE 8 as the cursor crosses.
void ItemScreen::showListPageOf(ItemSystem& sys, int p, int idx) {
    const int lp = idx < 0 ? listPage_[p] : idx / 10;
    if (lp == listPage_[p]) return;
    listPage_[p] = lp;
    refreshRows(sys);
    sys.game->se(8);
}
void ItemScreen::stepListPrev(ItemSystem& sys, int count, int p) {
    const bool self = page_ != 1;   // the "Yourself" line (-1) exists on the inventory only
    if (cursor_[p] == -1) cursor_[p] = count - 1;
    else if (cursor_[p] == 0 && self) { cursor_[p] = -1; pageWrapped_ = 1; }
    else cursor_[p] = cursor_[p] == 0 ? count - 1 : cursor_[p] - 1;
    showListPageOf(sys, p, cursor_[p]);
    sys.game->se(6);
}
void ItemScreen::stepListNext(ItemSystem& sys, int count, int p) {
    const bool self = page_ != 1;
    if (cursor_[p] == -1) cursor_[p] = 0;
    else if (cursor_[p] + 1 >= count && self) { cursor_[p] = -1; pageWrapped_ = 1; }
    else cursor_[p] = cursor_[p] + 1 >= count ? 0 : cursor_[p] + 1;
    showListPageOf(sys, p, cursor_[p]);
    sys.game->se(6);
}

// orig FUN_001575f0 (work/decompiled/001575f0.c)
void ItemScreen::stepNext(ItemSystem& sys) {
    const int count = pageCount(sys);
    const int p = page_ == 1 ? 1 : 0;
    if (open_ == 0 && count < 2) return;
    pageWrapped_ = 0;
    if (open_ != 0) { stepListNext(sys, count, p); return; }
    cursor_[p]++;
    if (count <= cursor_[p]) cursor_[p] = 0;
    sys.game->se(6);
}

// orig FUN_00157760 / FUN_00157890 (work/decompiled/00157760.c, 00157890.c). The originals recurse until a legal
// target is found (FUN_001579c0 guaranteed one before mode 3 was entered); the loop is bounded here.
void ItemScreen::targetPrev(ItemSystem& sys) {
    for (int guard = 0; guard < 64; ++guard) {
        targetCursor_--;
        if (targetCursor_ < -1) targetCursor_ = sys.inventory->count - 1;
        if (useLegal(sys, cursor_[0], targetCursor_) && cursor_[0] != targetCursor_) break;
    }
    if (listPage_[0] == 0 && targetCursor_ > 9) { listPage_[0] = 1; refreshRows(sys); buildDescription(sys); sys.game->se(8); }
    else if (listPage_[0] == 1 && targetCursor_ < 10) { listPage_[0] = 0; refreshRows(sys); buildDescription(sys); sys.game->se(8); }
    sys.game->se(6);
}
void ItemScreen::targetNext(ItemSystem& sys) {
    for (int guard = 0; guard < 64; ++guard) {
        targetCursor_++;
        if (sys.inventory->count <= targetCursor_) targetCursor_ = -1;
        if (useLegal(sys, cursor_[0], targetCursor_) && cursor_[0] != targetCursor_) break;
    }
    if (listPage_[0] == 0 && targetCursor_ > 9) { listPage_[0] = 1; refreshRows(sys); buildDescription(sys); sys.game->se(8); }
    else if (listPage_[0] == 1 && targetCursor_ < 10) { listPage_[0] = 0; refreshRows(sys); buildDescription(sys); sys.game->se(8); }
    sys.game->se(6);
}

// orig FUN_0015b290 (work/decompiled/0015b290.c)
void ItemScreen::actionStep(int delta) {
    if (delta == 0) { ctxAction_ = 0; return; }
    ctxAction_ += delta;
    if (ctxAction_ < 0) ctxAction_ = 3;
    else if (ctxAction_ > 3) ctxAction_ = 0;
}

// orig FUN_00158430 (work/decompiled/00158430.c)
void ItemScreen::cursorUp(ItemSystem& sys) {
    if (open_ == 0) return;
    const int count = pageCount(sys);
    if (ctxOpen_ == 0 && 0 < count) {
        if (mode_ == 1 || page_ == 1) stepPrev(sys);
        else if (mode_ == 3) targetPrev(sys);
        buildDescription(sys);
    } else if (ctxOpen_ != 0) {
        actionStep(-1);
        sys.game->se(6);
    }
}
// orig FUN_00158500 (work/decompiled/00158500.c)
void ItemScreen::cursorDown(ItemSystem& sys) {
    if (open_ == 0) return;
    const int count = pageCount(sys);
    if (ctxOpen_ == 0 && 0 < count) {
        if (mode_ == 1 || page_ == 1) stepNext(sys);
        else if (mode_ == 3) targetNext(sys);
        buildDescription(sys);
    } else if (ctxOpen_ != 0) {
        actionStep(1);
        sys.game->se(6);
    }
}

// orig FUN_001579c0 (work/decompiled/001579c0.c)
bool ItemScreen::targetExists(ItemSystem& sys) const {
    const int n = sys.inventory->count;
    for (int j = 0; j < n; ++j) if (cursor_[0] != j && useLegal(sys, cursor_[0], j)) return true;
    return useLegal(sys, cursor_[0], -1);
}

// orig FUN_0015b170 (work/decompiled/0015b170.c; jump table 0x48f6a0 read from the disassembly 0x15b1a8..0x15b27c)
bool ItemScreen::actionEnabled(ItemSystem& sys, int idx, int action) {
    Inventory& v = *sys.inventory;
    switch (action) {
        case kActEquip:   // FUN_001d7c80 == -1 ? FUN_001d77f0(idx, 1) : FUN_001d77f0(idx, 0)
            return v.equippedSlotOfIndex(idx) == -1 ? v.equipLegal(idx, true, sys) : v.equipLegal(idx, false, sys);
        case kActThrow: return action_.canThrow(sys, idx);                      // FUN_001d8cc0
        case kActPlace: case kActPlaceHeads: return action_.canPlace(sys, idx);  // FUN_001d8bb0(idx, 1)
        case kActPlaceTails: return action_.canPlace(sys, idx);                 // FUN_001d8bb0(idx, 0)
        case kActEat: case kActOpen: case kActInject: case kActDeport: return useLegal(sys, idx, 0);   // FUN_001d8950(idx, 0)
        case kActPush: case kActParasite: case kActFuse: return true;
        case kActGive: return action_.canGive(sys, idx);                        // FUN_001d8e40
        default: return false;
    }
}

// orig FUN_00158890 (work/decompiled/00158890.c)
void ItemScreen::executeContext(ItemSystem& sys) {
    GameAccess& g = *sys.game;
    Inventory& v = *sys.inventory;
    const int idx = cursor_[0];
    Item* it = v.slots[idx];
    const int cls = it->cls < 14 ? it->cls : 0;
    const int action = kContextActions[cls][ctxAction_];
    bool closeAfter = false, refresh = false;
    lastState_ = action;
    switch (action) {
        case kActEquip:
            if (v.equippedSlotOfIndex(idx) == -1) {
                if (!v.equipLegal(idx, true, sys)) {
                    g.se(3);
                } else {
                    v.equipItem(idx, true, sys);   // FUN_001d6d60(idx, 1)
                    refreshRows(sys);
                    refresh = true;
                    g.hudRefreshBars(0);           // FUN_00151af0(0x5179c0, 0)
                    g.se(0x57);
                }
            } else if (!v.equipLegal(idx, false, sys)) {
                const Item* e = v.slots[idx];
                const bool stuck = e->brand == kIdST10 || (e->flags & kItemCursedSealed) != 0;
                if (stuck) {
                    g.message(e->brand == kIdST10b ? 0x2e1 : 0x2b5);   // "It's stuck..." / "It won't come off."
                    g.se(0x59);
                }
            } else {
                v.equipItem(idx, false, sys);
                refreshRows(sys);
                refresh = true;
                g.hudRefreshBars(0);
                g.se(0x58);
            }
            break;
        case kActThrow:
            if (!action_.canThrow(sys, idx)) g.se(3);
            else { action_.beginThrow(sys, idx); closeAfter = refresh = true; }
            break;
        case kActPlace:
            if (!action_.canPlace(sys, idx)) g.se(3);
            else { action_.beginPlace(sys, idx, true); closeAfter = refresh = true; }
            break;
        case kActEat: case kActOpen: case kActInject: case kActDeport:
            if (!useLegal(sys, idx, 0)) {   // FUN_001d8950(0x53d910, idx, 0)
                if (it->id == kIdBX03) g.message(0x2b3);   // "It won't open."
                g.se(3);
            } else {
                useItem(sys, idx, 0);       // FUN_001d8810(0x53d910, idx, 0)
                closeAfter = cls == kClsTL;
                refresh = true;
                if (listPage_[0] == 1 && v.count < 0xb) listPage_[0] = 0;
                refreshRows(sys);
                g.hudLayoutBars(1);         // FUN_00151660(0x5179c0, 1)
            }
            break;
        case kActPush: case kActParasite: case kActFuse:
            if (!targetExists(sys)) {
                if (action == kActPush) {
                    g.message(0x2d2);                                   // "Cannot mark this Brand."
                } else if (action == kActParasite) {
                    messageName(sys, itemDisplayName(*it, sys));        // "%s\x81\x84" (0x44bf28) / "%s" (0x44bf30)
                    g.message(0x2db);                                   // "Cannot fuse the Parasite."
                } else {
                    g.message(0x2f3);                                   // "Cannot fuse."
                }
                g.se(3);
                g.se(3);
            } else {
                mode_ = 3;
                savedPage_ = listPage_[0];
                messageName(sys, itemDisplayName(*it, sys));
                g.message(action == kActPush ? 0x2b0 : action == kActParasite ? 0x2fe : 0x2b1);
                g.se(1);
                targetCursor_ = -1;
                targetNext(sys);
            }
            refresh = true;
            break;
        case kActPlaceHeads:
            if (action_.canPlace(sys, idx)) { action_.beginPlace(sys, idx, true); closeAfter = refresh = true; }
            break;
        case kActPlaceTails:
            if (!action_.canPlace(sys, idx)) g.se(3);
            else { action_.beginPlace(sys, idx, false); closeAfter = refresh = true; }
            break;
        case kActGive:
            if (!action_.canGive(sys, idx)) {
                g.se(3);
            } else {
                action_.beginGive(sys, idx);
                // FUN_0012c520(FUN_0011c100 ? 150 : 100, 30, 0x50b130, player, 1, 0) +0x1c
                CharacterAccess* t = action_.giveTarget(sys);
                placeTargetId_ = t != nullptr ? t->charId() : 0;
                placeItem_ = it;
                closeAfter = refresh = true;
            }
            break;
        default: break;
    }
    if (refresh) {
        closeContext(sys);
        clampCursor(sys);
        refreshRows(sys);
        buildDescription(sys);
    }
    if (closeAfter) requestClose(sys);
    else if (pageCount(sys) < 2 && twoPage_ != 0) dropSecondPage(sys);   // FUN_001554d0
}

// orig FUN_001585d0 (work/decompiled/001585d0.c)
void ItemScreen::executeTwoItem(ItemSystem& sys) {
    if (!useLegal(sys, cursor_[0], targetCursor_) || cursor_[0] == targetCursor_) { sys.game->se(3); return; }
    useItem(sys, cursor_[0], targetCursor_);
    mode_ = 1;
    listPage_[0] = sys.inventory->count < 0xb ? 0 : savedPage_;
    clampCursor(sys);
    refreshRows(sys);
    buildDescription(sys);
    sys.game->se(1);
}

// orig FUN_001586a0 (work/decompiled/001586a0.c)
void ItemScreen::bagTake(ItemSystem& sys) {
    GameAccess& g = *sys.game;
    Item* it = sys.bag->get(cursor_[1]);   // FUN_001daff0(0x53dc00, +0x2c)
    if (it == nullptr) { g.se(3); return; }
    if (sys.inventory->count == Inventory::kMaxItems) {
        g.message(0x28f);
        g.message(0x29c);                  // "Your inventory is full."
        g.voice(0x4fc);                    // FUN_001161c0 == 0: FUN_00115c10 + FUN_00115970(1.0, snd, 0x4fc, 0)
        g.se(3);
        return;
    }
    Item* taken = sys.bag->take(cursor_[1], sys);   // FUN_001daeb0
    sys.inventory->add(taken, sys);                 // FUN_001d65c0
    clampCursor(sys);
    refreshRows(sys);
    buildDescription(sys);
    g.message(0x298);
    g.message(0x248, itemDisplayName(*it, sys));    // "Here" "%s" "I'll give this back to you."
    g.message(0x2a0);
    g.voice(0x4fd);
    g.se(1);
}

// orig FUN_0015c1a0 (work/decompiled/0015c1a0.c): the Collector's greeting on tick 0x1a of the bag page
void ItemScreen::bagGreeting(ItemSystem& sys) {
    GameAccess& g = *sys.game;
    g.voiceResume();   // FUN_00115c50(0x4e9790, 0)
    if (sys.inventory->count == Inventory::kMaxItems) {
        g.voice(0x4fc);
        g.message(0x28f);
        g.message(0x29c);
    } else {
        g.voice(0x4fb);
        g.message(0x292);
        g.message(0x2b6);
    }
}

// ---- description (text-window slots 1 / 2) ---------------------------------------------------------------------

// orig FUN_001552e0 (work/decompiled/001552e0.c)
bool ItemScreen::descriptionPageValid(ItemSystem& sys, int delta) const {
    int cur = -1;
    if (mode_ == 1) cur = cursor_[page_ == 1 ? 1 : 0];
    else if (mode_ == 3) cur = targetCursor_;
    int p = descPage_ + delta;
    bool hasParasite = false, hasBrand = false, hasPP = false, hasCrystal = false;
    if (cur == -1 && page_ == 0) {
        CharacterAccess& pl = sys.game->player();   // iRam0050b144 != 0 while the screen is open
        hasParasite = pl.parasite() != nullptr;
        hasBrand = pl.brand() != 0;
        if (hasParasite) hasPP = pl.parasite()->parasite != nullptr;
    } else if (cur >= 0) {
        const Item* it = itemAt(sys, cur);
        if (it != nullptr) {   // the original dereferences it unconditionally
            hasParasite = it->parasite != nullptr;
            hasBrand = it->brand != 0;
            if (hasParasite) hasPP = it->parasite->parasite != nullptr;
            hasCrystal = canFuseCrystal(*it);   // FUN_001d4a20
        }
    }
    for (;;) {
        if (p < 0 || p > 4) return false;
        if (p == 0) return true;
        if (p == 1 && hasCrystal) return true;
        if (p == 2 && hasBrand) return true;
        if (p == 3 && hasParasite) return true;
        if (p == 4) { if (hasPP) return true; p = delta + 4; } else p += delta;
    }
}

// orig FUN_00154ba0 (work/decompiled/00154ba0.c; FUN_00153710 arguments 0 / 2 / 3 read at 0x154cdc / 0x154d14 /
// 0x154d5c): the L1 / R1 hint glyphs of the frame follow the description pages that exist around +0x40
void ItemScreen::updateHints(ItemSystem& sys, int cur) {
    bool brand = false, parasite = false;
    uint32_t crystal = 0;
    if (cur == -1 && page_ == 0) {
        CharacterAccess& pl = sys.game->player();
        brand = pl.brand() != 0;
        parasite = pl.parasite() != nullptr;
    } else if (cur > -1) {
        const Item* it = itemAt(sys, cur);
        if (it != nullptr && it->identified()) {
            brand = it->brand != 0;
            parasite = it->parasite != nullptr;
            crystal = crystalOfSword(it->id);   // FUN_001d4a50(item)
        }
    }
    if (brand || parasite || crystal != 0) {
        const bool next = descriptionPageValid(sys, 1), prev = descriptionPageValid(sys, -1);
        if (next && prev && hintVariant_ != 0) setFrameVariant(0);
        else if (next && !prev && hintVariant_ != 2) setFrameVariant(2);
        else if (prev && !next && hintVariant_ != 3) setFrameVariant(3);
    } else if (hintVariant_ != 1) {
        setFrameVariant(1);   // FUN_00153750
    }
}

// orig FUN_00154db0 (work/decompiled/00154db0.c); descriptions of the extra pages use "\x81\x7b%s\n" (0x44bf00)
void ItemScreen::descriptionPage(ItemSystem& sys, int delta) {
    if (delta > 0 && descPage_ > 3) return;
    if (delta < 0 && descPage_ < 1) return;
    if (!descriptionPageValid(sys, delta)) return;
    ui::HelpBar& hb = ui::helpBar();
    hb.clear(1);
    hb.clear(2);
    int cur = -1;
    if (mode_ == 1) cur = cursor_[page_ == 1 ? 1 : 0];
    else if (mode_ == 3) cur = targetCursor_;
    descPage_ += delta;
    const Item* it = cur >= 0 ? itemAt(sys, cur) : nullptr;
    CharacterAccess& pl = sys.game->player();
    bool hasParasite = false, hasBrand = false, hasPP = false;
    uint32_t crystal = 0;
    if (cur == -1 && page_ == 0) {
        hasParasite = pl.parasite() != nullptr;
        hasBrand = pl.brand() != 0;
        if (hasParasite) hasPP = pl.parasite()->parasite != nullptr;
    } else if (it != nullptr && it->identified()) {
        hasParasite = it->parasite != nullptr;
        hasBrand = it->brand != 0;
        if (hasParasite) hasPP = it->parasite->parasite != nullptr;
        crystal = crystalOfSword(it->id);   // FUN_001d4a50
    }
    for (;;) {
        const int p = descPage_;
        if (p < 0 || p > 5) break;
        if (p == 1 && crystal != 0) {
            updateHints(sys, cur);
            if (cur != -1) hb.set(kDescX, kDescY, kDescW, kDescH, 1, "\x81\x7b%s\n", sys.desc(kClsIS, itemIndexOf(crystal)));
            break;
        }
        if (p == 2 && hasBrand) {
            updateHints(sys, cur);
            uint32_t b = 0;   // FUN_001d2c60(-1 when no player)
            if (cur == -1) b = pl.brand();
            else if (it != nullptr) b = it->brand;
            hb.set(kDescX, kDescY, kDescW, kDescH, 1, "\x81\x7b%s\n", sys.desc(kClsST, itemIndexOf(b)));
            break;
        }
        if ((p == 3 && hasParasite) || (p == 4 && hasPP)) {
            updateHints(sys, cur);
            const Item* w = cur == -1 ? pl.parasite() : (it != nullptr ? it->parasite : nullptr);
            if (w != nullptr && descPage_ == 4 && hasPP) w = w->parasite;
            if (w != nullptr) hb.set(kDescX, kDescY, kDescW, kDescH, 1, "\x81\x7b%s\n", sys.desc(w->cls, w->idx));
            break;
        }
        descPage_ += delta;
    }
    if (descPage_ < 1 || descPage_ > 4) buildDescription(sys);
    hb.setSpeed(1, 4);   // uRam005181c0 = 4
    sys.game->se(8);
}

// orig FUN_00154ab0 (work/decompiled/00154ab0.c): the GN00 description with the first '5' replaced by the first
// digit of the remaining count (sprintf "%d" 0x44bef8, strstr "5" 0x4e5ca0)
void ItemScreen::gunDescription(ItemSystem& sys, const Item& it) {
    char num[16] = {};
    std::snprintf(num, sizeof num, "%d", it.count);
    char buf[0x110] = {};
    std::snprintf(buf, sizeof buf, "%s", sys.desc(it.cls, it.idx));
    char* p = std::strstr(buf, "5");
    if (p != nullptr) *p = num[0];   // UNVERIFIED: the original writes through a null strstr result
    ui::helpBar().set(kDescX, kDescY, kDescW, kDescH, 1, "%s\n", buf);   // fmt 0x44be88
}

// orig FUN_00153f00 (work/decompiled/00153f00.c; the 15 blocks Ghidra removed at 0x1547a4..0x15498c are the signed
// remainder fix-ups of "count % 4", unreachable for the non-negative count)
void ItemScreen::buildDescription(ItemSystem& sys) {
    ui::HelpBar& hb = ui::helpBar();
    hb.clear(1);
    hb.clear(2);
    int cur = -1;
    if (mode_ == 1) cur = cursor_[page_ == 1 ? 1 : 0];
    else if (mode_ == 3) cur = targetCursor_;
    const int count = pageCount(sys);
    if (!(-2 < cur && cur < count)) return;
    descPage_ = 0;
    updateHints(sys, cur);
    descPage_ = 0;
    GameAccess& g = *sys.game;
    if (cur == -1) {
        if (page_ == 0) {
            CharacterAccess& pl = g.player();
            char statuses[0x80] = {};
            unsigned n = 0;
            static const int kNames[7] = {0x1af, 0x1b0, 0x1b1, 0x1b2, 0x1b3, 0x1b4, 0x1b5};
            for (int b = 0; b < 7; ++b) {
                if (!(pl.statusMask() & (1u << b))) continue;
                appendStr(statuses, sizeof statuses, g.str(kNames[b]));
                n++;
                if ((n & 3) == 0) appendStr(statuses, sizeof statuses, "\n");   // 0x4e5c9c
            }
            const int brands = g.counter(1);   // uRam0053c5c4
            // 0x44bed0 "%s" + 12 x 0x8140 + "\x81\x7c%d\n%s" with the literal 1 when the counter is 0 [V t0 = a1 = 1
            // at 0x154a70]; 0x44bea0 "...\x81\x7b%d\n%s" with the counter capped at 99 otherwise
            if (brands == 0) {
                hb.set(kDescX, kDescY, kDescW, kDescH, 1,
                       "%s\x81\x40\x81\x40\x81\x40\x81\x40\x81\x40\x81\x40\x81\x40\x81\x40\x81\x40\x81\x40\x81\x40\x81\x40\x81\x7c%d\n%s",
                       g.str(0x1c7), 1, statuses);
            } else {
                hb.set(kDescX, kDescY, kDescW, kDescH, 1,
                       "%s\x81\x40\x81\x40\x81\x40\x81\x40\x81\x40\x81\x40\x81\x40\x81\x40\x81\x40\x81\x40\x81\x40\x81\x40\x81\x7b%d\n%s",
                       g.str(0x1c7), static_cast<uint32_t>(brands) > 99u ? 99 : brands, statuses);
            }
        }
    } else {
        const Item* it = itemAt(sys, cur);
        if (it != nullptr) {
            if (!it->identified()) {
                hb.set(kDescX, kDescY, kDescW, kDescH, 1, "%s\n%s\n", itemDisplayName(*it, sys), g.str(0x1b6));   // 0x44be90
            } else {
                if (it->id == kIdGN00) {
                    gunDescription(sys, *it);
                } else if (it->id == kIdWM22) {
                    // dead parasite: the living parasite's name (WM PRM row of +0x3c) and, when it carries one, the
                    // parasite's name; JP cuts the 2-byte suffix 0x44be68 off both, EN joins them with "/" (0x4e5c98)
                    // and skips the first strlen(0x44be68) bytes of the second
                    const char* host = sys.name(kClsWM, itemIndexOf(it->savedId));
                    char buf[0x40] = {};
                    if (it->parasite == nullptr) {
                        if (g.language() == 1) {
                            appendPrefix(buf, sizeof buf, host, lenMinusSuffix(host));   // FUN_0023fca0 strncpy
                            hb.set(kDescX, kDescY, kDescW, kDescH, 1, "%s\n%s%s", sys.desc(it->cls, it->idx), buf, g.str(0x1b7));   // 0x44be70
                        } else if (g.language() == 0) {
                            appendStr(buf, sizeof buf, host);                            // FUN_0023f540 strcpy
                            hb.set(kDescX, kDescY, kDescW, kDescH, 1, "%s\n%s %s", sys.desc(it->cls, it->idx), buf, g.str(0x1b7));  // 0x44be78
                        }
                    } else {
                        const char* para = sys.name(kClsWM, itemIndexOf(it->parasite->id));
                        if (g.language() == 1) {
                            appendPrefix(buf, sizeof buf, host, lenMinusSuffix(host));   // strncpy
                            appendPrefix(buf, sizeof buf, para, lenMinusSuffix(para));   // FUN_0023f938 strncat
                            hb.set(kDescX, kDescY, kDescW, kDescH, 1, "%s\n%s%s", sys.desc(it->cls, it->idx), buf, g.str(0x1b7));
                        } else if (g.language() == 0) {
                            appendStr(buf, sizeof buf, host);                            // strcpy
                            appendStr(buf, sizeof buf, "/");                             // strcat 0x4e5c98
                            const size_t skip = std::strlen(kWmSuffix);
                            appendStr(buf, sizeof buf, std::strlen(para) >= skip ? para + skip : "");   // inline strcat
                            hb.set(kDescX, kDescY, kDescW, kDescH, 1, "%s\n%s %s", sys.desc(it->cls, it->idx), buf, g.str(0x1b7));
                        }
                    }
                } else {
                    hb.set(kDescX, kDescY, kDescW, kDescH, 1, "%s\n", sys.desc(it->cls, it->idx));   // 0x44be88
                }
                // slot 2: the name line (colour 0x22227f) for items with a database entry (FUN_001f8ac0)
                if (page_ == 0 && g.itemHasDatabaseEntry(*it) && descPage_ == 0) {
                    if (g.language() == 1) {
                        hb.set(kDescX, kDescY, kDescW, kDescH, 2, g.str(0x248), sys.name(it->cls, it->idx));
                    } else if (g.language() == 0) {
                        // the description up to the first "\x8f\x49" (0x44be50) pair
                        char nm[0x401] = {};
                        std::snprintf(nm, sizeof nm, "%s", sys.desc(it->cls, it->idx));
                        if (nm[1] != '\0') {
                            for (int i = 0; nm[i + 1] != '\0'; ++i) {
                                if (nm[i] == '\x8f' && nm[i + 1] == '\x49') { nm[i] = '\0'; break; }
                            }
                        }
                        hb.set(kDescX, kDescY, kDescW, kDescH, 2, g.str(0x248), nm);
                    }
                    hb.setColor(2, 0x22227f);   // uRam0051ab10
                    hb.setSpeed(2, 4);          // uRam005181c4
                }
            }
        }
    }
    hb.setSpeed(1, 4);   // uRam005181c0
}

// ---- input --------------------------------------------------------------------------------------------------

// orig FUN_00157a60 (work/decompiled/00157a60.c)
void ItemScreen::input(ItemSystem& sys, const PadInput& pad, const ItemScreenEnv& env) {
    openTick(sys, env);
    CharacterAccess& pl = sys.game->player();
    if (!ready() && open_ != 0) return;
    if ((pl.actorType() == 0 || pl.actorType() == 2) && !pl.isAlive()) return;
    if (!env.sceneIsGame) return;
    const uint32_t pr = pad.pressed, rp = pad.repeat;
    if (pr & static_cast<uint32_t>(env.itemScreenButton)) {
        if (open_ == 1) {
            if (ctxOpen_ != 1 && ready()) {
                requestClose(sys);
                sys.game->se(10);
            }
        } else {
            openRequest_ = 1;
        }
        return;
    }
    if ((pr & PadInput::kCircle) && ready()) { cancel(sys); return; }
    if (pr & (PadInput::kLeft | PadInput::kLeft2)) { pageLeft(sys); return; }
    if (pr & (PadInput::kRight | PadInput::kRight2)) { pageRight(sys); return; }
    if (rp & (PadInput::kUp | PadInput::kUp2)) { cursorUp(sys); return; }
    if (rp & (PadInput::kDown | PadInput::kDown2)) { cursorDown(sys); return; }
    if (pr & PadInput::kCross) {
        if (open_ == 0) return;
        if (ctxOpen_ == 0) {
            if (page_ == 0 && mode_ == 1) {
                if (cursor_[0] == -1 || sys.inventory->count < 1) {
                    sys.game->se(3);
                } else {
                    openContext(sys);
                    actionStep(0);
                    if (ticks_ < 0x19 && ticks_ > 0xf) ticks_ = 0xf;
                }
            } else if (page_ == 0 && mode_ == 3) {
                executeTwoItem(sys);
            } else if (page_ == 1) {
                bagTake(sys);
            }
        } else if (cursor_[0] > -1 && cursor_[0] < sys.inventory->count) {
            executeContext(sys);
        }
        return;
    }
    if (pr & PadInput::kSquare) {
        if (open_ != 0 && mode_ != 3 && ctxOpen_ == 0 && twoPage_ == 1) {
            if (page_ == 0) {
                sys.inventory->sortItems(sys);   // FUN_001d6bf0
                keepCursorPage(sys);             // FUN_00152930
            } else if (page_ == 1) {
                sys.bag->sortItems(sys);         // FUN_001db1d0
            }
            clampCursor(sys);
            refreshRows(sys);
            buildDescription(sys);
            sys.game->se(1);
        }
        return;
    }
    if (pr & PadInput::kL1) {
        if (open_ != 0 && ctxOpen_ == 0 && (hintVariant_ == 0 || hintVariant_ == 2)) descriptionPage(sys, 1);
        return;
    }
    if ((pr & PadInput::kR1) && ctxOpen_ == 0 && open_ != 0 && (hintVariant_ == 0 || hintVariant_ == 3)) descriptionPage(sys, -1);
}

// ---- per tick -----------------------------------------------------------------------------------------------

// orig FUN_00159150 (work/decompiled/00159150.c): backdrop alpha = 0.9 * fade
void ItemScreen::updateBackdrop() {
    const float f = static_cast<float>(fade_) * 0.9f;
    if (backdrop_ != nullptr) backdrop_->color = static_cast<uint32_t>(static_cast<int>(f)) << 24;
}

// orig FUN_0015bdb0 (work/decompiled/0015bdb0.c): EXP "%d/%d" (0x44bf90), ATK / DEF "%3d/%3d%c%2d" (0x44bf98) with
// 'U' / 'D' and |equipment total| capped at 99
void ItemScreen::updateStats(ItemSystem& sys) {
    CharacterAccess& pl = sys.game->player();
    if (expDigits_[0] != nullptr) {
        char b[16] = {};
        std::snprintf(b, 14, "%d/%d", pl.exp(), pl.expNext());
        for (int i = 0; i < 13; ++i) if (expDigits_[i] != nullptr) setDigitGlyph(*expDigits_[i], b[i]);
    }
    auto row = [&](ui::Sprite* (&digits)[10], int cur, int max, int total) {
        if (digits[0] == nullptr) return;
        char ud = 'U';
        if (total < 0) { ud = 'D'; total = -total; }
        if (total >= 100) total = 99;
        char b[16] = {};
        std::snprintf(b, 11, "%3d/%3d%c%2d", cur, max, ud, total);
        for (int i = 0; i < 10; ++i) if (digits[i] != nullptr) setDigitGlyph(*digits[i], b[i]);
    };
    row(atkDigits_, pl.atkCur(), pl.atkMax(), sys.inventory->atkTotal(sys));   // FUN_001d83d0
    row(defDigits_, pl.defCur(), pl.defMax(), sys.inventory->defTotal(sys));   // FUN_001d85b0
}

// orig FUN_00159bd0 (work/decompiled/00159bd0.c): item rows at (288, 102 + 20 row), the "Yourself" / bag line at
// y 314, and the context-menu labels at (280, 134 + 20 k, z 3.1); half glyph 8x9, layer flag 1
void ItemScreen::drawRows(ItemSystem& sys) {
    ui::TextRenderer& t = ui::text();
    t.halfW = 8.0f;
    t.halfH = 9.0f;
    const int p = page_ == 1 ? 1 : 0;
    int end = 0;
    if (page_ == 0) {
        end = sys.inventory->count;
        if ((listPage_[0] + 1) * 10 <= end) end = (listPage_[0] + 1) * 10;
    } else if (page_ == 1) {
        end = sys.bag->capacity(*sys.game);
        if (!(end < (listPage_[1] + 1) * 10)) end = (listPage_[p] + 1) * 10;
    }
    t.layerFlag = 1;
    int sel = -1;   // unaff_s4 (mode is always 1 or 3)
    if (mode_ == 1) sel = cursor_[p];
    else if (mode_ == 3) sel = targetCursor_;
    const uint32_t a = fade_ << 24;
    for (int i = listPage_[p] * 10; i < end; ++i) {
        const float y = static_cast<float>((i + listPage_[p] * -10) * 0x14 + 0x66);
        uint32_t colour;
        if (sel == i) colour = ctxOpen_ == 0 ? a + 0x22227f : a + 0x11114f;
        else if (ctxOpen_ == 0 && (mode_ != 3 || (useLegal(sys, cursor_[0], i) && cursor_[0] != i))) colour = kTextGrey + a;
        else colour = a + 0x3f3f3f;
        const Item* it = itemAt(sys, i);
        char buf[0x100];
        if (it == nullptr) {
            text(288.0f, y, 0.0f, colour, "%s\n", "\x81\x45\x81\x45\x81\x45\x81\x45\x81\x45");   // 0x44bf50
        } else if ((it->flags & kItemIdentified) == 0) {
            text(288.0f, y, 0.0f, colour, "%s\n", itemDisplayName(*it, sys));
        } else if ((it->flags & kItemRotten) == 0) {
            text(288.0f, y, 0.0f, colour, "%s\n", isParasiteHostWithParasite(*it) ? composedParasiteName(*it, sys) : sys.name(it->cls, it->idx));
        } else if (sys.game->language() == 1) {
            std::snprintf(buf, sizeof buf, "%s\x81\x45\x95\x85\n", sys.name(it->cls, it->idx));   // 0x44bf38
            text(288.0f, y, 0.0f, colour, "%s", buf);
        } else if (sys.game->language() == 0) {
            std::snprintf(buf, sizeof buf, "%s\x81\x45rotten\n", sys.name(it->cls, it->idx));     // 0x44bf40
            text(288.0f, y, 0.0f, colour, "%s", buf);
        }
    }
    t.layerFlag = 1;
    uint32_t self;
    if (sel == -1 && page_ != 1) self = a + 0x22227f;
    else if (mode_ == 3 && !useLegal(sys, cursor_[0], -1)) self = a + 0x3f3f3f;
    else self = kTextGrey + a;
    if (page_ == 0) text(288.0f, 314.0f, 0.0f, self, "%s", sys.game->str(0x1c7));        // "Yourself"
    else if (page_ == 1) text(288.0f, 314.0f, 0.0f, self, "%s", sys.game->str(0x1c8));   // "The Collector's Bag"
    if (ctxOpen_ != 0 && cursor_[0] > -1 && cursor_[0] < sys.inventory->count) {
        const Item* it = sys.inventory->slots[cursor_[0]];
        const int cls = it->cls < 14 ? it->cls : 0;
        for (int k = 0; k < 4; ++k) {
            const int act = kContextActions[cls][k];
            uint32_t c;
            if (ctxAction_ == k) c = a + 0x22227f;
            else c = actionEnabled(sys, cursor_[0], act) ? kTextGrey + a : a + 0x3f3f3f;
            if (act == -1) continue;
            const float y = static_cast<float>(static_cast<unsigned>(k)) * 20.0f + 134.0f;
            if (act == kActEquip && sys.inventory->equippedSlotOfIndex(cursor_[0]) != -1) text(280.0f, y, 3.1f, c, "%s", sys.game->str(0x1ae));
            else text(280.0f, y, 3.1f, c, "%s\n", sys.game->str(act + 0x1a1));
        }
    }
}

// orig FUN_001591e0 (work/decompiled/001591e0.c)
void ItemScreen::update(ItemSystem& sys, const ItemScreenEnv& env) {
    if (closeCountdown_ == -1 && !env.systemMenuOpen) {
        if (fade_ <= 0x3f) {
            fade_ += 8;
            if (0x3f < fade_) fade_ = 0x40;
        }
    } else if (fade_ != 0) {
        fade_ = static_cast<int>(fade_) - 8 < 1 ? 0 : fade_ - 8;
    }
    const uint32_t c = (fade_ << 24) + 0x7f7f7f;
    setColour(expLabel_, c);
    setColour(atkLabel_, c);
    setColour(defLabel_, c);
    for (auto* s : expDigits_) setColour(s, c);
    for (auto* s : atkDigits_) setColour(s, c);
    for (auto* s : defDigits_) setColour(s, c);
    if (hintVariant_ != 1) for (auto* s : hints_) setColour(s, c);
    for (auto* s : frame03_) setColour(s, c);
    for (auto* s : frameVar_) setColour(s, c);
    for (auto* s : frame_) setColour(s, c);
    setColour(panel_, c);
    setColour(titleBar_, c);
    for (auto* s : panelPieces_) setColour(s, c);
    for (auto* s : panel2Pieces_) setColour(s, c);
    const int p = page_ == 1 ? 1 : 0;
    for (int i = 0; i < tabCount_ && i < 5; ++i) {
        setColour(tabs_[i], c);
        setInsetUv(tabs_[i], kIsTabUv[i][i == listPage_[p] ? 1 : 0]);
    }
    for (int i = 0; i < 2; ++i) {
        setColour(arrows_[i], c);
        const int count = pageCount(sys);
        int lit = 0;
        if (i == 0) { if (0 < listPage_[p]) lit = 1; }
        else if (listPage_[p] < count / 0xb) lit = 1;
        setInsetUv(arrows_[i], kIsArrowUv[i][lit]);
    }
    setColour(caption_, c);
    if (twoPage_ != 0) {
        setColour(sortCaption_, c);
        setColour(sortIcon_, c);
    }
    for (auto* s : equipMarks_) setColour(s, c);
    for (auto* s : sealMarks_) setColour(s, c);
    for (auto* s : unusedMarks_) setColour(s, c);
    for (auto* s : classIcons_) setColour(s, c);
    for (auto& r : numDigits_) if (r[0] != nullptr) for (auto* s : r) setColour(s, c);
    ui::text().color = kTextGrey + (fade_ << 24);
    updateStats(sys);   // FUN_0015bdb0
    drawRows(sys);      // FUN_00159bd0
}

// screen half of orig FUN_00156de0 (work/decompiled/00156de0.c) and of FUN_00157310 (work/decompiled/00157310.c)
void ItemScreen::tick(ItemSystem& sys, const ItemScreenEnv& env, bool hasPlayer, bool playerDead) {
    if (closeCountdown_ > -1 && --closeCountdown_ == 0) {
        if (page_ == 1) {
            sys.game->stopVoice();   // FUN_00115c10(0x4e9790)
            sys.game->logClear();    // FUN_0015d0a0(0x5180f0)
        }
        close(sys);
        open_ = 0;
        closeCountdown_ = -1;
        ui::helpBar().clear(1);      // FUN_0015c3f0(0x5180f0, 1)
        ui::helpBar().clear(2);
        page_ = -1;
        ui::text().color = kTextDefault;   // uRam004e96ac = DAT_0044c198
    }
    // FUN_00157310 (called when the screen or the ribbon is up or the system menu is open): the screen part
    if (open_ != 0) {
        updateBackdrop();             // FUN_00159150
        update(sys, env);             // FUN_001591e0
        if (ctxOpen_ == 0) {          // caption "SELECT button: GAME CONFIG" (0x2e) at (240, 24)
            ui::TextRenderer& t = ui::text();
            t.x = 240.0f;
            t.y = 24.0f;
            t.z = 0.0f;
            t.color = kTextGrey + fade_ * 0x1000000u;
            const char* caption = sys.game->str(0x2e);
            // PORT keyboard button prompts: while keyboard / mouse is active "SELECT button" names the PC key bound to
            // SELECT instead ("M key: GAME CONFIG"); the text after the full-width colon 0x8146 is the original's
            char keyCaption[160];
            if (const char* key = keycapLabel(0x100); key != nullptr) {
                if (const char* colon = std::strstr(caption, "\x81\x46"); colon != nullptr) {
                    std::snprintf(keyCaption, sizeof keyCaption, "%s%s%s", key, sys.game->language() == 1 ? "" : " key", colon);
                    caption = keyCaption;
                }
            }
            t.draw("%s", caption);
        }
        ui::text().layerFlag = 1;
        ui::text().color = kTextDefault;
    }
    if (open_ != 0) {
        ticks_++;
        if (page_ == 1 && ticks_ == 0x1a) bagGreeting(sys);   // FUN_0015c1a0
        if (ticks_ == 0x19) sys.game->stopAllSe();           // FUN_00115900(0x4e9790)
    }
    if (!hasPlayer) return;
    if (openRequest_ != 0) {
        if (env.paused || env.eventRunning || !env.sceneIsGame || env.talkBusy || playerDead || !env.floorGenerated ||
            env.systemMenuOpen || env.playerNoInput)
            openRequest_ = 0;
    }
}

}  // namespace baroque::game
