// PORT: mouse layer of the in-game item screen (work/reports/MOUSE_MENUS_NATIVE.md). Not part of the original game:
// the translated ItemScreen code (item_screen.cpp) is untouched. Clicks only produce the pad presses the pad would
// (row / "Yourself" / context entry = Cross, page arrows and tabs = Left / Right, right click = Circle, wheel = Up /
// Down) and hover sets the cursor the way the pad's step functions do, with the same SE and description refresh.
// Installed into gItemScreenMouse (item_screen.hpp), called by hud_api.cpp around FUN_00157a60 and FUN_001591e0.
#include "game/items/item_screen.hpp"
#include "game/items/item_effects.hpp"
#include "game/ui/mouse_menu.hpp"

namespace baroque::game {

namespace {
#include "game/items/item_screen_tables.inc"

// index space of the registered items (row items use the absolute list index)
constexpr int kIdxSelf = 0x200;    // "Yourself" line (page 0): cursor / target -1
constexpr int kIdxCtx = 0x100;     // + k: context-menu entry k
constexpr int kIdxTab = 0x300;     // + i: page tab i (click only)
constexpr int kIdxArrow = 0x400;   // + i: page arrow i (click only)

// rows of the list: text at (288, 102 + 20 row) with 16x18 glyphs; the hit band spans the list panel (258..484)
constexpr float kRowX0 = 262.0f, kRowX1 = 482.0f, kRowPitch = 20.0f;
// context entries: text at (280, 134 + 20 k) inside the box (275, 134) 115 x 78
constexpr float kCtxX0 = 276.0f, kCtxX1 = 389.0f;
}  // namespace

struct ItemScreenMouse {
    // The guards of FUN_00157a60 before its button dispatch.
    static bool takesInput(const ItemScreen& s, ItemSystem& sys, const ItemScreenEnv& env) {
        if (s.open_ == 0 || !s.ready()) return false;
        CharacterAccess& pl = sys.game->player();
        if ((pl.actorType() == 0 || pl.actorType() == 2) && !pl.isAlive()) return false;
        return env.sceneIsGame && !env.systemMenuOpen;
    }
    // A row index the pad's Up / Down can put the cursor (or the target cursor) on.
    static bool rowSelectable(ItemScreen& s, ItemSystem& sys, int i) {
        if (s.mode_ == 3 && s.page_ == 0) return useLegal(sys, s.cursor_[0], i) && s.cursor_[0] != i;   // FUN_00157760 / FUN_00157890
        return true;
    }
    // last list index + 1 of the visible page, as FUN_00159bd0 computes it
    static int visibleEnd(ItemScreen& s, ItemSystem& sys) {
        const int p = s.page_ == 1 ? 1 : 0;
        int end = 0;
        if (s.page_ == 0) {
            end = sys.inventory->count;
            if ((s.listPage_[0] + 1) * 10 <= end) end = (s.listPage_[0] + 1) * 10;
        } else if (s.page_ == 1) {
            end = sys.bag->capacity(*sys.game);
            if (!(end < (s.listPage_[1] + 1) * 10)) end = (s.listPage_[p] + 1) * 10;
        }
        return end;
    }

    static void hover(ItemScreen& s, ItemSystem& sys, const ItemScreenEnv& env) {
        if (!takesInput(s, sys, env)) return;
        const int h = ui::mouse::hovered(&s);
        if (h < 0) return;
        if (s.ctxOpen_ != 0) {
            // FUN_00158430 / FUN_00158500 with the context open: FUN_0015b290 step + SE 6
            if (h < kIdxCtx || h >= kIdxCtx + 4 || h - kIdxCtx == s.ctxAction_) return;
            s.ctxAction_ = h - kIdxCtx;
            sys.game->se(6);
            return;
        }
        if (s.pageCount(sys) < 1) return;   // FUN_00158430 / FUN_00158500: no step on an empty list
        const int p = s.page_ == 1 ? 1 : 0;
        int target;
        if (h == kIdxSelf) target = -1;
        else if (h < 0x100) target = h;
        else return;
        if (s.mode_ == 3 && s.page_ == 0) {
            // target mode: FUN_00157760 / FUN_00157890 move +0x38 (SE 6), then FUN_00153f00
            if (target == s.targetCursor_) return;
            s.targetCursor_ = target;
            sys.game->se(6);
            s.buildDescription(sys);
            return;
        }
        // FUN_00157410 / FUN_001575f0 move +0x28 / +0x2c (SE 6; -1 on page 0 is the wrapped "Yourself" state), then
        // FUN_00153f00
        if (target == s.cursor_[p]) return;
        s.pageWrapped_ = target == -1 ? 1 : 0;
        s.cursor_[p] = target;
        sys.game->se(6);
        s.buildDescription(sys);
    }

    static void items(ItemScreen& s, ItemSystem& sys, const ItemScreenEnv& env) {
        if (!takesInput(s, sys, env)) return;
        using namespace ui::mouse;
        if (s.ctxOpen_ != 0) {
            // only the context entries (the pad's Up / Down / Cross act on them while the box is open)
            if (s.cursor_[0] < 0 || s.cursor_[0] >= sys.inventory->count) return;
            const Item* it = sys.inventory->slots[s.cursor_[0]];
            const int cls = it->cls < 14 ? it->cls : 0;
            for (int k = 0; k < 4; ++k) {
                if (kContextActions[cls][k] == -1) continue;   // not drawn
                row(&s, kIdxCtx + k, kCtxX0, kCtxX1, static_cast<float>(k) * 20.0f + 134.0f, kRowPitch);
            }
            return;
        }
        const int p = s.page_ == 1 ? 1 : 0;
        const int end = visibleEnd(s, sys);
        for (int i = s.listPage_[p] * 10; i < end; ++i) {
            if (!rowSelectable(s, sys, i)) continue;
            const float y = static_cast<float>((i - s.listPage_[p] * 10) * 0x14 + 0x66);
            row(&s, i, kRowX0, kRowX1, y, kRowPitch);
        }
        if (s.page_ == 0 && (s.mode_ != 3 || useLegal(sys, s.cursor_[0], -1)))   // "Yourself" (0x1c7) at y 314
            row(&s, kIdxSelf, kRowX0, kRowX1, 314.0f, kRowPitch);
        // page arrows (FUN_001591e0 lights them when a step is possible) = Left / Right; tabs = one Left / Right step
        // toward the clicked page (FUN_00158170 / FUN_001582c0 move one page per press)
        const int count = s.pageCount(sys);
        if (count >= 0xb) {
            if (0 < s.listPage_[p])
                item(&s, kIdxArrow, kIsArrowRect[0][0] - 3.0f, kIsArrowRect[0][1] - 3.0f, kIsArrowRect[0][2] + 6.0f,
                     kIsArrowRect[0][3] + 6.0f, platform::PAD_LEFT);
            if (s.listPage_[p] < count / 0xb)
                item(&s, kIdxArrow + 1, kIsArrowRect[1][0] - 3.0f, kIsArrowRect[1][1] - 3.0f, kIsArrowRect[1][2] + 6.0f,
                     kIsArrowRect[1][3] + 6.0f, platform::PAD_RIGHT);
            for (int i = 0; i < s.tabCount_ && i < 5; ++i) {
                if (i == s.listPage_[p]) continue;
                item(&s, kIdxTab + i, kIsTabRect[i][0] - 1.0f, kIsTabRect[i][1] - 2.0f, kIsTabRect[i][2] + 2.0f,
                     kIsTabRect[i][3] + 4.0f, i < s.listPage_[p] ? platform::PAD_LEFT : platform::PAD_RIGHT);
            }
        }
    }
};

namespace {
bool install() {
    gItemScreenMouse.hover = &ItemScreenMouse::hover;
    gItemScreenMouse.items = &ItemScreenMouse::items;
    return true;
}
const bool gInstalled = install();
}  // namespace

}  // namespace baroque::game
