// ITEM LIST page (OPTION mode 2; vtable 0x4cf1c0; ctor FUN_001f6e20; open FUN_001f6f70, close FUN_001f71d0,
// is-active 0x1f8ce0 (+0x46c), update 0x1f7560 (title + FUN_001f76e0), input FUN_001f75b0, wants-next 0x1f8cf0
// (+0x470), next page 0x1f7550 (= FUN_00163b10 OPTION)).
// Grid of 14 x 7 category icons over the 0x142 item codes PTR_DAT_00466f10 (index = col + (row + top) * 14), each
// lit when its "seen" flag 0x49b950[index] is set. View globals: cRam004e5e10 column, cRam004e5e14 row,
// iRam0053e9c0 first row (0..0x10).
#include "game/ui/database.hpp"
#include "game/ui/menu.hpp"
#include "game/hud/hud_api.hpp"
#include "game/items/item.hpp"
#include "game/fade.hpp"
#include "game/flag_manager.hpp"
#include "game/options.hpp"
#include "game/pad.hpp"
#include "game/sound_stub.hpp"
#include "game/ui/gradient_texture.hpp"
#include "game/ui/help_bar.hpp"
#include "game/ui/menu_tables.hpp"
#include "game/ui/mouse_menu.hpp"
#include "game/ui/sprite.hpp"
#include "game/ui/strings.hpp"
#include "game/ui/text.hpp"
#include "game/ui/texture_registry.hpp"
namespace baroque::game::ui {
namespace {
signed char gItemCol = 0;   // cRam004e5e10
signed char gItemRow = 0;   // cRam004e5e14
int gItemTop = 0;           // iRam0053e9c0
int gItemsAllSeen = -1;     // DAT_00466498 (.data -1; FUN_001f8b60)

bool flagSet(int id) { return flags().test(static_cast<uint32_t>(id)); }
int current() { return gItemCol + (gItemRow + gItemTop) * 0xe; }

// orig FUN_001f8240 (work/decompiled/001f8240.c): category of an item code (first two letters), 0xe = none
int category(int index) {
    for (int i = 0; i < 0xe; ++i)
        if (kItemCategories[i][0] == kItemCodes[index][0] && kItemCategories[i][1] == kItemCodes[index][1]) return i;
    return 0xe;
}

// orig FUN_001f8b60 (work/decompiled/001f8b60.c): DAT_00466498 = every item with a flag has been seen
void allSeenCheck() {
    for (int i = 0; i < 0x142; ++i) {
        if (0 < kItemFlags[i] && !flagSet(kItemFlags[i])) {
            gItemsAllSeen = 0;
            return;
        }
    }
    gItemsAllSeen = 1;
}

class ItemListPage final : public MenuPage {
public:
    ItemListPage() = default;   // orig FUN_001f6e20 (all fields 0)
    const char* debugName() const override { return "ITEM LIST"; }

    // orig FUN_001f6f70 (work/decompiled/001f6f70.c)
    void open() override {
        makeFrame();
        makeTitle(str(4));
        isOpen = 1;
        wantsNext_ = 0;
        angle_ = 0;
        alpha_ = 0;
        textures().load(0x3c);
        if (options().cursorPos == 1) {
            gItemCol = 0;
            gItemTop = 0;
            gItemRow = 0;
        }
        setTop(gItemTop);                                  // FUN_001f7bb0
        allSeenCheck();                                    // FUN_001f8b60
        cursor_ = sprites().create(43.0f, 68.0f, 1.0f, 34.0f, 39.0f, 0.0f, 1.0f, 32.0f, 32.0f, gradientTextureId(), 1);
        for (int i = 0; i < 0x8c; ++i) {
            const float* p = kItemFramePos[i];
            const float* uv = kItemFrameUv[i];
            frame_[i] = sprites().create(p[0], p[1], 0.0f, p[2], p[3], uv[0], uv[1], uv[2], uv[3], 0x3c, 1);
        }
        arrowUp_ = sprites().create(23.0f, 60.0f, 1.0f, 12.0f, 13.0f, 13.0f, 17.0f, 25.0f, 30.0f, 0x28, 1);
        arrowDown_ = sprites().create(23.0f, 321.0f, 1.0f, 13.0f, 13.0f, 0.0f, 17.0f, 13.0f, 30.0f, 0x28, 1);
        count_ = 0x142;                                    // FUN_001f82b0
        makeFrame2(2);
    }

    // orig FUN_001f71d0 (work/decompiled/001f71d0.c)
    void close() override {
        freeFrame2();
        helpBar().clear(0);
        freeFrame();
        isOpen = 0;
        sprites().destroyRef(cursor_);
        for (Sprite*& s : frame_) sprites().destroyRef(s);
        sprites().destroyRef(arrowUp_);
        sprites().destroyRef(arrowDown_);
        freeIcons();                                       // FUN_001f7d30
        textures().release(0x3c);
    }

    // orig 0x1f7560 (disassembled)
    void update() override {
        if (isOpen == 0) return;
        animateTitle();
        if (titleSettled != 0) draw();                     // FUN_001f76e0
    }

    // orig FUN_001f75b0 (work/decompiled/001f75b0.c)
    void input() override {
        if (isOpen == 0 || alpha_ <= 0x3f) return;
        const PadPort& p = pad().port[0];
        const uint32_t rep = p.repeat;
        if (p.pressed & 0x40) {
            decide();                                      // FUN_001f8160
        } else if (p.pressed & 0x20) {
            if (isOpen != 0) {                             // FUN_001f8200
                wantsNext_ = 1;
                sound().playSe(1.0f, 8);
            }
        } else {
            if ((rep & 0x8000) != 0 || (rep & 0x40000) != 0) left();          // FUN_001f8050
            else if ((rep & 0x2000) != 0 || (rep & 0x80000) != 0) right();   // FUN_001f80d0
            if ((rep & 0x1000) != 0 || (rep & 0x10000) != 0) up();            // FUN_001f7f60
            else if ((rep & 0x4000) != 0 || (rep & 0x20000) != 0) down();    // FUN_001f7e70
        }
    }

    // PORT: mouse menu layer. Hover on an icon = the cursor move of left/right/up/down onto it (help + SE 6); a
    // click is Cross. The scroll arrows are Up/Down (scrolls at the edge row, as with the pad).
    void mouseInput() override {
        if (isOpen == 0 || alpha_ <= 0x3f) return;                            // the guard of input()
        const int h = mouse::hovered(this);
        if (h < 0 || h >= 0xe * 7) return;
        const signed char col = static_cast<signed char>(h % 0xe), row = static_cast<signed char>(h / 0xe);
        if (col == gItemCol && row == gItemRow) return;
        gItemCol = col;
        gItemRow = row;
        help(current());
        sound().playSe(1.0f, 6);
    }
    // PORT: the 14 x 7 cells where draw() places the cursor frame (x 43 + 32 col, y 68 + 40 row).
    void mouseItems() override {
        if (isOpen == 0 || titleSettled == 0) return;
        for (int r = 0; r < 7; ++r)
            for (int c = 0; c < 0xe; ++c)
                mouse::item(this, c + r * 0xe, static_cast<float>(c) * 32.0f + 43.0f, static_cast<float>(r) * 40.0f + 68.0f, 32.0f, 40.0f);
        if (gItemTop >= 1) mouse::item(this, 0x100, 19.0f, 56.0f, 20.0f, 21.0f, platform::PAD_UP);
        if (gItemTop < 0x10) mouse::item(this, 0x101, 19.0f, 317.0f, 21.0f, 21.0f, platform::PAD_DOWN);
    }

    bool wantsNext() const override { return wantsNext_ != 0; }   // 0x1f8cf0
    MenuPage* nextPage() override { return newOptionPage(); }       // 0x1f7550

private:
    // orig FUN_001f7bb0 (work/decompiled/001f7bb0.c)
    void setTop(int top) {
        gItemTop = top;
        freeIcons();                                       // FUN_001f7d30
        for (int c = 0; c < 0xe; ++c)                      // FUN_001f7bf0
            for (int r = 0; r < 7; ++r)
                icon_[c][r] = sprites().create(static_cast<float>(c) * 32.0f + 44.0f, static_cast<float>(r) * 40.0f + 72.0f, 2.0f, 32.0f,
                                               32.0f, kItemIconUv[0][0], kItemIconUv[0][1], kItemIconUv[0][2], kItemIconUv[0][3], 0x28, 1);
    }
    void freeIcons() {
        for (auto& col : icon_)
            for (Sprite*& s : col) sprites().destroyRef(s);
    }

    // orig FUN_001f82c0 (work/decompiled/001f82c0.c)
    void help(int index) {
        if (kItemFlags[index] == -1) {
            helpBar().clear(0);
            return;
        }
        if (!flagSet(kItemFlags[index])) {
            helpBar().set(32.0f, 369.0f, 448.0f, 120.0f, 0, "%s\n", str(0x7f));   // 0x4664a0
        } else {
            // the description pointer (+8) of the PRMITEM record FUN_001d2c60(code) of the code's category
            // (iRam0053d860 + class * 4: SD CT WG HT MT BX LQ ST TL TP BN WM GN IS)
            ItemSystem& sys = itemSystem();
            if (sys.rowCount(kClsSD) == 0) itemPrmLoad();   // PORT: the original loads PRMITEM at boot (FUN_00100730)
            const char* code = kItemCodes[index];
            const uint32_t word = static_cast<uint32_t>(static_cast<uint8_t>(code[0])) | (static_cast<uint32_t>(static_cast<uint8_t>(code[1])) << 8) |
                                  (static_cast<uint32_t>(static_cast<uint8_t>(code[2])) << 16) | (static_cast<uint32_t>(static_cast<uint8_t>(code[3])) << 24);
            const int idx = itemIndexOf(word);
            static constexpr struct { char a, b; int cls; } kCat[14] = {
                {'S', 'D', kClsSD}, {'G', 'N', kClsGN}, {'C', 'T', kClsCT}, {'W', 'G', kClsWG}, {'H', 'T', kClsHT},
                {'M', 'T', kClsMT}, {'B', 'X', kClsBX}, {'L', 'Q', kClsLQ}, {'S', 'T', kClsST}, {'T', 'L', kClsTL},
                {'T', 'P', kClsTP}, {'B', 'N', kClsBN}, {'W', 'M', kClsWM}, {'I', 'S', kClsIS}};
            for (const auto& c : kCat) {
                if (code[0] != c.a || code[1] != c.b) continue;
                helpBar().set(32.0f, 369.0f, 448.0f, 120.0f, 0, "%s\n", sys.desc(c.cls, idx));   // FUN_0015c490(.., 0x4664a0, desc)
                break;
            }
        }
        helpBar().setSpeed(0, 4);                          // uRam005181bc = 4
    }
    // orig FUN_001f7e70 (work/decompiled/001f7e70.c)
    void down() {
        if (isOpen == 0) return;
        const signed char r = static_cast<signed char>(gItemRow + 1);
        if (r < 7) {
            gItemRow = r;
            help(gItemCol + (r + gItemTop) * 0xe);
            sound().playSe(1.0f, 6);
        } else if (gItemTop < 0x10) {
            ++gItemTop;
            help(current());
            sound().playSe(1.0f, 6);
        }
    }
    // orig FUN_001f7f60 (work/decompiled/001f7f60.c)
    void up() {
        if (isOpen == 0) return;
        const signed char r = static_cast<signed char>(gItemRow - 1);
        if (r < 0) {
            if (0 < gItemTop) {
                --gItemTop;
                help(current());
                sound().playSe(1.0f, 6);
            }
        } else {
            gItemRow = r;
            help(gItemCol + (r + gItemTop) * 0xe);
            sound().playSe(1.0f, 6);
        }
    }
    // orig FUN_001f8050 (work/decompiled/001f8050.c)
    void left() {
        if (isOpen == 0) return;
        gItemCol = static_cast<signed char>(gItemCol - 1);
        if (gItemCol < 0) {
            gItemCol = 0;
        } else {
            help(current());
            sound().playSe(1.0f, 6);
        }
    }
    // orig FUN_001f80d0 (work/decompiled/001f80d0.c)
    void right() {
        if (isOpen == 0) return;
        gItemCol = static_cast<signed char>(gItemCol + 1);
        if (gItemCol < 0xe) {
            help(current());
            sound().playSe(1.0f, 6);
        } else {
            gItemCol = 0xd;
        }
    }
    // orig FUN_001f8160 (work/decompiled/001f8160.c)
    void decide() {
        if (isOpen == 0) return;
        const int i = current();
        if (i < count_ && 0 < kItemFlags[i] && !flagSet(kItemFlags[i])) sound().playSe(1.0f, 3);
    }

    // orig FUN_001f76e0 (work/decompiled/001f76e0.c)
    void draw() {
        angle_ += 8;
        if (0x168 < angle_) angle_ = -360;
        // two FUN_00206940 calls whose results are unused
        if (alpha_ < 0x40 && fade().finished()) {
            alpha_ += 6;
            if (0x3f < alpha_) {
                alpha_ = 0x40;
                if (!helpBar().isSet(0) && alpha_ == 0x40) {
                    help(current());
                    sound().playBgm(0x19, 4, 1);
                    flags().set(0x3b7);
                }
            }
        }
        const uint32_t c = (alpha_ << 24) + 0x7f7f7f;
        if (cursor_ != nullptr) {
            cursor_->cx = static_cast<float>(gItemCol) * 32.0f + 43.0f + cursor_->hw;
            cursor_->cy = static_cast<float>(gItemRow) * 40.0f + 68.0f + cursor_->hh;
            cursor_->color = c;
        }
        for (Sprite* s : frame_)
            if (s != nullptr) s->color = c;
        for (int r = 0; r < 7; ++r) {
            for (int col = 0; col < 0xe; ++col) {
                Sprite* s = icon_[col][r];
                if (s == nullptr) continue;
                const int idx = col + (r + gItemTop) * 0xe;
                if (idx < count_ && kItemCodes[idx] != nullptr) {
                    const float* uv = kItemIconUv[category(idx)];
                    s->u0 = uv[0] / 128.0f;
                    s->v0 = uv[1] / 128.0f;
                    s->u1 = uv[2] / 128.0f;
                    s->v1 = uv[3] / 128.0f;
                    const int f = kItemFlags[idx];
                    if (f == -1) s->color = 0;
                    else if (!flagSet(f)) s->color = (alpha_ << 24) + 0x2f2f2f;
                    else s->color = c;
                } else {
                    s->color = 0;
                }
            }
        }
        if (arrowUp_ != nullptr) arrowUp_->color = (gItemTop < 1) ? 0u : c;
        if (arrowDown_ != nullptr) arrowDown_->color = (gItemTop < 0x10) ? c : 0u;
        text().layerFlag = 1;
        text().color = 0x40bbbbbb;                         // DAT_0044c198
    }

    int wantsNext_ = 0;              // +0x470
    int angle_ = 0;                  // +0x474
    unsigned alpha_ = 0;             // +0x478
    int count_ = 0;                  // +0x47c
    Sprite* cursor_ = nullptr;       // +0x480
    Sprite* arrowUp_ = nullptr;      // +0x484
    Sprite* arrowDown_ = nullptr;    // +0x488
    Sprite* frame_[0x8c] = {};       // +0x48c
    Sprite* icon_[0xe][7] = {};      // +0x6bc (column stride 0x5c)
};
}  // namespace
int itemsAllSeen() { return gItemsAllSeen; }
MenuPage* newItemListPage() { return new ItemListPage(); }   // FUN_001691a0 (ctor FUN_001f6e20)
}  // namespace baroque::game::ui
