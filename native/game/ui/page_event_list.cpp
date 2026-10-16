// EVENT LIST page (OPTION mode 1; vtable 0x4cf190; ctor FUN_001f56d0; open FUN_001f5840, close FUN_001f5bc0,
// is-active 0x1f6e00 (+0x46c), update 0x1f6130 (title + FUN_001f62b0), input FUN_001f6180, wants-next 0x1f6e10
// (+0x470), next page 0x1f6120 (= FUN_00163b10 OPTION)). SCENES.md 6: Cross on an unlocked event plays it through
// the EvMovie scene (movie index event + 2), which then re-opens this page (mode 1).
// View globals: uRam0053e918 page (0/1), uRam0053e920 column (0..3), uRam0053e928 row (0..2); event = col + row*4 + page*12.
#include "game/ui/database.hpp"
#include "game/ui/menu.hpp"
#include "game/fade.hpp"
#include "game/flag_manager.hpp"
#include "game/options.hpp"
#include "game/pad.hpp"
#include "game/scene_manager.hpp"
#include "game/scenes/menu_scenes.hpp"
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
int gEvPage = 0;   // uRam0053e918
int gEvCol = 0;    // uRam0053e920
int gEvRow = 0;    // uRam0053e928

int eventIndex() { return gEvCol + gEvRow * 4 + gEvPage * 0xc; }

class EventListPage final : public MenuPage {
public:
    EventListPage() = default;   // orig FUN_001f56d0 (all fields 0)
    const char* debugName() const override { return "EVENT LIST"; }

    // orig FUN_001f5840 (work/decompiled/001f5840.c)
    void open() override {
        makeFrame();
        makeTitle(str(3));
        isOpen = 1;
        wantsNext_ = 0;
        angle_ = 0;
        alpha_ = 0;
        if (options().cursorPos == 1) {
            gEvPage = 0;
            gEvCol = 0;
            gEvRow = 0;
        }
        textures().loadRange(0x33, 0x3c);
        cursor_ = sprites().create(42.0f, 71.0f, 0.0f, 104.0f, 72.0f, 0.0f, 1.0f, 32.0f, 32.0f, gradientTextureId(), 1);
        int piece = 0;
        int group = 0;
        for (int i = 0; i < 0xd8; ++i) {
            const float* p = kEventFramePos[piece];
            const float* uv = kEventFrameUv[piece];
            const float* g = kEventGroupOfs[group];
            frame_[i] = sprites().create(p[0] + g[0], p[1] + g[1], 1.0f, p[2], p[3], uv[0], uv[1], uv[2], uv[3], 0x2e, 1);
            ++piece;
            if (0x11 < piece) {
                ++group;
                piece = 0;
            }
        }
        int col = 0;
        int row = 0;
        for (int i = 0; i < 0xc; ++i) {
            cover_[i] = sprites().create(static_cast<float>(col) * 112.0f + 46.0f, static_cast<float>(row) * 98.0f + 77.0f, 1.5f, 96.0f,
                                         60.0f, 32.0f, 0.0f, 128.0f, 60.0f, 0x3c, 0);
            ++col;
            if (3 < col) {
                col = 0;
                ++row;
            }
        }
        arrowUp_ = sprites().create(23.0f, 60.0f, 1.0f, 12.0f, 13.0f, 13.0f, 17.0f, 25.0f, 30.0f, 0x28, 1);
        arrowDown_ = sprites().create(23.0f, 321.0f, 1.0f, 13.0f, 13.0f, 0.0f, 17.0f, 13.0f, 30.0f, 0x28, 1);
        setPage(gEvPage);                                  // FUN_001f6630
        makeFrame2(2);
        if (sound().currentBgm() == -1) sound().playBgm(0x18, 4, 1);   // FUN_00115e80 == -1
    }

    // orig FUN_001f5bc0 (work/decompiled/001f5bc0.c)
    void close() override {
        freeFrame2();
        helpBar().clear(0);
        freeFrame();
        isOpen = 0;
        for (Sprite*& s : thumb_) sprites().destroyRef(s);
        sprites().destroyRef(cursor_);
        for (Sprite*& s : frame_) sprites().destroyRef(s);
        for (Sprite*& s : cover_) sprites().destroyRef(s);
        sprites().destroyRef(arrowUp_);
        sprites().destroyRef(arrowDown_);
        for (int id = 0x33; id < 0x3d; ++id) textures().release(id);
    }

    // orig 0x1f6130 (disassembled)
    void update() override {
        if (isOpen == 0) return;
        animateTitle();
        if (titleSettled != 0) draw();                     // FUN_001f62b0
    }

    // orig FUN_001f6180 (work/decompiled/001f6180.c)
    void input() override {
        if (isOpen == 0 || alpha_ <= 0x3f) return;
        const PadPort& p = pad().port[0];
        const uint32_t rep = p.repeat;
        if (p.pressed & 0x40) {
            decide();                                      // FUN_001f6c40
        } else if (p.pressed & 0x20) {
            if (isOpen != 0) {                             // FUN_001f6dc0
                wantsNext_ = 1;
                sound().playSe(1.0f, 8);
            }
        } else {
            if ((rep & 0x8000) != 0 || (rep & 0x40000) != 0) left();          // FUN_001f6b60
            else if ((rep & 0x2000) != 0 || (rep & 0x80000) != 0) right();   // FUN_001f6bd0
            if ((rep & 0x1000) != 0 || (rep & 0x10000) != 0) up();            // FUN_001f6a80
            else if ((rep & 0x4000) != 0 || (rep & 0x20000) != 0) down();    // FUN_001f69a0
        }
    }

    // PORT: mouse menu layer. Hover on a picture = the cursor move of left/right/up/down onto it (help + SE 6); a
    // click is Cross (play the event). The page arrows are Up/Down (row 2 -> next page, as with the pad).
    void mouseInput() override {
        if (isOpen == 0 || alpha_ <= 0x3f) return;                            // the guard of input()
        const int h = mouse::hovered(this);
        if (h < 0 || h >= 0xc) return;
        const int col = h % 4, row = h / 4;
        if (col == gEvCol && row == gEvRow) return;
        gEvCol = col;
        gEvRow = row;
        help();
        sound().playSe(1.0f, 6);
    }
    // PORT: the 4 x 3 pictures where draw() places the cursor frame (x 42 + 112 col, y 71 + 98 row, 104 x 72).
    void mouseItems() override {
        if (isOpen == 0 || titleSettled == 0) return;
        for (int i = 0; i < 0xc; ++i)
            mouse::item(this, i, static_cast<float>(i % 4) * 112.0f + 42.0f, static_cast<float>(i / 4) * 98.0f + 71.0f, 104.0f, 72.0f);
        if (gEvPage == 1) mouse::item(this, 0x10, 19.0f, 56.0f, 20.0f, 21.0f, platform::PAD_UP);
        if (gEvPage == 0) mouse::item(this, 0x11, 19.0f, 317.0f, 21.0f, 21.0f, platform::PAD_DOWN);
    }

    bool wantsNext() const override { return wantsNext_ != 0; }   // 0x1f6e10
    MenuPage* nextPage() override { return newOptionPage(); }       // 0x1f6120

private:
    // orig FUN_001f6630 (work/decompiled/001f6630.c)
    void setPage(int page) {
        gEvPage = page;
        alpha_ = 0;
        coverAlpha_ = 0x40;
        for (Sprite*& s : thumb_) sprites().destroyRef(s);   // FUN_001f6750 (0x24 slots)
        for (int i = 0; i < 0x12; ++i) {                      // FUN_001f6670
            const int k = gEvPage * 0x12 + i;
            const float* p = kEventThumbPos[k];
            const float* uv = kEventThumbUv[k];
            thumb_[i] = sprites().create(p[0], p[1], 0.0f, p[2], p[3], uv[0], uv[1], uv[2], uv[3], kEventThumbTex[k], 0);
        }
    }
    // orig FUN_001f6870 (work/decompiled/001f6870.c)
    void help() {
        helpBar().clear(0);
        const int e = eventIndex();
        // the names 0x53e930[] are str(0x66 + event), filled by the static ctor FUN_004c9de0
        if (!flags().test(static_cast<uint32_t>(kEventFlags[e])))
            helpBar().set(32.0f, 369.0f, 448.0f, 32.0f, 0, "%s\n", str(0x7e));
        else
            helpBar().set(32.0f, 369.0f, 448.0f, 32.0f, 0, "%s\n", str(0x66 + e));
        helpBar().setSpeed(0, 4);
    }
    // orig FUN_001f69a0 (work/decompiled/001f69a0.c)
    void down() {
        if (isOpen == 0) return;
        const int r = gEvRow + 1;
        if (gEvPage == 0 && 2 < r) {
            gEvPage = 1;
            gEvRow = 0;
            setPage(1);
            help();
            sound().playSe(1.0f, 8);
        } else if (r < 3) {
            gEvRow = r;
            help();
            sound().playSe(1.0f, 6);
        }
    }
    // orig FUN_001f6a80 (work/decompiled/001f6a80.c)
    void up() {
        if (isOpen == 0) return;
        const int r = gEvRow - 1;
        if (gEvPage == 1 && r < 0) {
            gEvPage = 0;
            gEvRow = 2;
            setPage(0);
            help();
            sound().playSe(1.0f, 8);
        } else if (-1 < r) {
            gEvRow = r;
            help();
            sound().playSe(1.0f, 6);
        }
    }
    // orig FUN_001f6b60 (work/decompiled/001f6b60.c)
    void left() {
        if (isOpen != 0 && -1 < gEvCol - 1) {
            --gEvCol;
            help();
            sound().playSe(1.0f, 6);
        }
    }
    // orig FUN_001f6bd0 (work/decompiled/001f6bd0.c)
    void right() {
        if (isOpen != 0 && gEvCol + 1 < 4) {
            ++gEvCol;
            help();
            sound().playSe(1.0f, 6);
        }
    }
    // orig FUN_001f6c40 (work/decompiled/001f6c40.c)
    void decide() {
        if (isOpen == 0) return;
        if (!flags().test(static_cast<uint32_t>(kEventFlags[eventIndex()]))) {
            sound().playSe(1.0f, 3);
            return;
        }
        const int e = eventIndex();
        close();
        evMovie().movieIndex = e + 2;                      // FUN_002064c0
        evMovie().optionMode = 1;                          // FUN_002064d0(1)
        scenes().requestScene("EvMovie.");
        sound().stopBgm(8);
    }

    // orig FUN_001f62b0 (work/decompiled/001f62b0.c)
    void draw() {
        angle_ += 8;
        if (0x168 < angle_) angle_ = -360;
        // two FUN_00206940 calls whose results are unused
        if (alpha_ < 0x40) {
            if (fade().finished()) {
                alpha_ += 3;
                if (0x40 < alpha_) alpha_ = 0x40;
            }
        }
        if (alpha_ == 0x40 && coverAlpha_ != 0) coverAlpha_ -= 2;
        if (!helpBar().isSet(0) && alpha_ == 0x40) help();
        const uint32_t c = (alpha_ << 24) + 0x7f7f7f;
        if (cursor_ != nullptr) {
            cursor_->color = c;
            cursor_->cx = static_cast<float>(gEvCol) * 112.0f + 42.0f + 52.0f;
            cursor_->cy = static_cast<float>(gEvRow) * 98.0f + 71.0f + 36.0f;
        }
        for (Sprite* s : thumb_)
            if (s != nullptr) s->color = (alpha_ < 0x40) ? 0u : c;
        for (Sprite* s : frame_)
            if (s != nullptr) s->color = c;
        for (int i = 0; i < 0xc; ++i) {
            Sprite* s = cover_[i];
            if (s == nullptr) continue;
            if (!flags().test(static_cast<uint32_t>(kEventFlags[i + gEvPage * 0xc]))) s->color = c;
            else if (alpha_ < 0x40) s->color = c;
            else s->color = static_cast<uint32_t>(coverAlpha_) * 0x1000000u + 0x7f7f7f;
        }
        if (arrowUp_ != nullptr) arrowUp_->color = (gEvPage == 1) ? c : 0u;
        if (arrowDown_ != nullptr) arrowDown_->color = (gEvPage == 0) ? c : 0u;
        text().layerFlag = 1;
        text().color = 0x40bbbbbb;                         // DAT_0044c198
    }

    int wantsNext_ = 0;              // +0x470
    int angle_ = 0;                  // +0x474
    unsigned alpha_ = 0;             // +0x478
    int coverAlpha_ = 0;             // +0x47c
    Sprite* frame_[0xd8] = {};       // +0x480
    Sprite* cover_[0xc] = {};        // +0x7e0 (tex 0x3c, hidden as an event is unlocked)
    Sprite* thumb_[0x24] = {};       // +0x810 (0x12 used per page)
    Sprite* cursor_ = nullptr;       // +0x8a0
    Sprite* arrowUp_ = nullptr;      // +0x8a4
    Sprite* arrowDown_ = nullptr;    // +0x8a8
};
}  // namespace
MenuPage* newEventListPage() { return new EventListPage(); }   // FUN_001691f0 (ctor FUN_001f56d0)
}  // namespace baroque::game::ui
