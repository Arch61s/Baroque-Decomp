// RANKING page (OPTION mode 0; vtable 0x4cf160; ctor FUN_001f2480; open FUN_001f2ed0, close FUN_001f31d0,
// is-active 0x1f56b0 (+0x46c), update 0x1f3d30 (title + FUN_001f3e80), input FUN_001f3d80, wants-next 0x1f56c0
// (+0x470), next page 0x1f3d20 (= FUN_00163b10 OPTION)). Data: ranking block 0x53deb0 (50 x 0x34), view globals
// iRam0053e8d8 (first row) / iRam0053e8e0 (column page 0 or 1).
// Entry layout as read by FUN_001f3e80: +0 rank (%2d), +4 name, +0x14 level (%2d), +0x18 value (%6d), +0x1c floor,
// +0x20 dungeon type, +0x24 sec, +0x25 min, +0x26 u16 hour, +0x28 u8 icon, +0x2c baroque id (str 0x82 + id),
// +0x30 u8 difficulty.
#include "game/ui/database.hpp"
#include "game/ui/menu.hpp"
#include "game/fade.hpp"
#include "game/options.hpp"
#include "game/pad.hpp"
#include "game/render2d.hpp"   // PORT: widescreen anchors of the header pipe
#include "game/sound_stub.hpp"
#include "game/ui/help_bar.hpp"
#include "game/ui/menu_tables.hpp"
#include "game/ui/mouse_menu.hpp"
#include "game/ui/sprite.hpp"
#include "game/ui/strings.hpp"
#include "game/ui/text.hpp"
#include "game/ui/texture_registry.hpp"
#include <cstdio>
#include <cstring>
namespace baroque::game::ui {
namespace {
int gRankTop = 0;    // iRam0053e8d8
int gRankPage = 0;   // iRam0053e8e0
constexpr uint32_t kTextRgb = 0xbbbbbb;         // DAT_0044c190
constexpr uint32_t kTextDefault = 0x40bbbbbb;   // DAT_0044c198

const uint8_t* entry(int i) { return rankingBlock().data() + i * kRankingStride; }
int32_t i32(const uint8_t* p, int off) { int32_t v; std::memcpy(&v, p + off, 4); return v; }

// orig FUN_00166d20 (work/decompiled/00166d20.c): rank digits of TEX_UI00 (10 px cells at v 0.1953..0.375)
void setRankGlyph(Sprite* s, char c) {
    if (s == nullptr) return;
    const int d = c - 0x30;
    if (d < 1 || 7 < d) {
        if (d == 8) {
            const float u = static_cast<float>(c - 0x31) * 10.0f;
            s->u0 = u / 128.0f; s->v0 = 0.1953125f; s->u1 = (u + 10.0f + 1.0f) / 128.0f; s->v1 = 0.375f;
        } else if (d == 9) {
            const float u = static_cast<float>(c - 0x31) * 10.0f;
            s->u0 = (u + 1.0f) / 128.0f; s->v0 = 0.1953125f; s->u1 = (u + 10.0f + 1.0f) / 128.0f; s->v1 = 0.375f;
        } else if (c == 0x30) {
            s->u0 = 0.7109375f; s->v0 = 0.1953125f; s->u1 = 0.796875f; s->v1 = 0.375f;
        } else {
            s->u0 = 0; s->v0 = 0; s->u1 = 0; s->v1 = 0;
        }
    } else {
        const float u = static_cast<float>(c - 0x31) * 10.0f;
        s->u0 = u / 128.0f; s->v0 = 0.1953125f; s->u1 = (u + 10.0f) / 128.0f; s->v1 = 0.375f;
    }
}

class RankingPage final : public MenuPage {
public:
    RankingPage() = default;   // orig FUN_001f2480 (all fields 0)
    const char* debugName() const override { return "RANKING"; }

    // orig FUN_001f2ed0 (work/decompiled/001f2ed0.c)
    void open() override {
        makeFrame();
        makeTitle(str(2));
        isOpen = 1;
        wantsNext_ = 0;
        angle_ = 0;
        alpha_ = 0;
        count_ = rankingCount();                           // FUN_001f3190
        if (options().cursorPos == 1 || count_ < gRankTop + 10) {
            gRankTop = 0;
            gRankPage = 0;
        }
        textures().load(0x22);
        setView(gRankTop, gRankPage);                      // FUN_001f4d40
        for (int i = 0; i < 0x11; ++i) {
            const float* p = kRankFramePos[i];
            const float* uv = kRankFrameUv[i];
            frame_[i] = sprites().create(p[0], p[1], static_cast<float>(i) * 0.2f + 1.0f, p[2], p[3], uv[0], uv[1], uv[2], uv[3], 0x2e, 1);
            // PORT widescreen (presentation only): the header pipe's end caps 0 / 1 (x 486..512) and 2 / 3 (x 0..26)
            // move to the area edges and its straight pipe 4 (x 26..486) repeats into both gaps; the column joints
            // 5..16 stay with the table
            if (frame_[i] == nullptr) continue;
            if (i == 0 || i == 1) frame_[i]->anchor = kAnchorRight;
            else if (i == 2 || i == 3) frame_[i]->anchor = kAnchorLeft;
            else if (i == 4) frame_[i]->wide = kWideTileL | kWideTileR;
        }
        for (int i = 0; i < 2; ++i) {
            const float* p = kRankTabPos[i];
            const float* uv = kRankTabUv[i];
            tab_[i] = sprites().create(p[0], p[1], 1.0f, p[2], p[3], uv[0], uv[1], uv[2], uv[3], 0x28, 1);
        }
        arrowUp_ = sprites().create(31.0f, 94.0f, 1.0f, 12.0f, 13.0f, 13.0f, 17.0f, 25.0f, 30.0f, 0x28, 1);
        arrowDown_ = sprites().create(31.0f, 404.0f, 1.0f, 12.0f, 13.0f, 0.0f, 17.0f, 13.0f, 30.0f, 0x28, 1);
        makeFrame2(4);
    }

    // orig FUN_001f31d0 (work/decompiled/001f31d0.c) -- no help-bar clear in this page
    void close() override {
        freeFrame2();
        freeFrame();
        isOpen = 0;
        for (Sprite*& s : header_) sprites().destroyRef(s);
        for (auto& r : rank_) for (Sprite*& s : r) sprites().destroyRef(s);
        for (auto& r : level_) for (Sprite*& s : r) sprites().destroyRef(s);
        for (auto& r : value_) for (Sprite*& s : r) sprites().destroyRef(s);
        for (auto& r : depth_) for (Sprite*& s : r) sprites().destroyRef(s);
        for (auto& r : time_) for (Sprite*& s : r) sprites().destroyRef(s);
        for (Sprite*& s : icon_) sprites().destroyRef(s);
        for (Sprite*& s : difficulty_) sprites().destroyRef(s);
        for (Sprite*& s : frame_) sprites().destroyRef(s);
        for (Sprite*& s : tab_) sprites().destroyRef(s);
        sprites().destroyRef(arrowUp_);
        sprites().destroyRef(arrowDown_);
        textures().release(0x22);
    }

    // orig 0x1f3d30 (disassembled): title animation, then FUN_001f3e80 once the title has settled
    void update() override {
        if (isOpen == 0) return;
        animateTitle();
        if (titleSettled != 0) draw();
    }

    // orig FUN_001f3d80 (work/decompiled/001f3d80.c)
    void input() override {
        if (isOpen == 0) return;
        const PadPort& p = pad().port[0];
        if ((p.repeat & 0x8000) != 0 || (p.repeat & 0x40000) != 0) {
            if (isOpen != 0 && gRankPage == 1) { gRankPage = 0; sound().playSe(1.0f, 6); }          // FUN_001f55a0
        } else if ((p.repeat & 0x2000) != 0 || (p.repeat & 0x80000) != 0) {
            if (isOpen != 0 && gRankPage == 0) { gRankPage = 1; sound().playSe(1.0f, 6); }          // FUN_001f5600
        } else if ((p.repeat & 0x1000) != 0 || (p.repeat & 0x10000) != 0) {
            if (isOpen != 0) {                                                                      // FUN_001f5540
                --gRankTop;
                if (gRankTop < 0) gRankTop = 0;
                else sound().playSe(1.0f, 6);
            }
        } else if ((p.repeat & 0x4000) != 0 || (p.repeat & 0x20000) != 0) {
            if (isOpen != 0 && gRankTop + 1 <= count_ - 10) { ++gRankTop; sound().playSe(1.0f, 6); }   // FUN_001f54e0
        } else if (p.pressed & 0x40) {
            // FUN_001f5660: empty
        } else if (p.pressed & 0x20) {
            if (isOpen != 0) { wantsNext_ = 1; sound().playSe(1.0f, 8); }                           // FUN_001f5670
        }
    }

    // PORT: mouse menu layer. No cursor on this page: the wheel scrolls (Up/Down), the scroll arrows are Up/Down
    // and the page tabs (kRankTabPos) switch the column page like Left/Right.
    void mouseItems() override {
        if (isOpen == 0 || titleSettled == 0) return;
        mouse::item(this, 0, 27.0f, 90.0f, 20.0f, 21.0f, platform::PAD_UP);
        mouse::item(this, 1, 27.0f, 400.0f, 20.0f, 21.0f, platform::PAD_DOWN);
        for (int i = 0; i < 2; ++i) {
            const float* p = kRankTabPos[i];
            mouse::item(this, 2 + i, p[0] - 4.0f, p[1] - 4.0f, p[2] + 8.0f, p[3] + 8.0f, gRankPage == 0 ? platform::PAD_RIGHT : platform::PAD_LEFT);
        }
    }

    bool wantsNext() const override { return wantsNext_ != 0; }   // 0x1f56c0
    MenuPage* nextPage() override { return newOptionPage(); }       // 0x1f3d20

private:
    // orig FUN_001f4d40 (work/decompiled/001f4d40.c)
    void setView(int top, int page) {
        gRankTop = top;
        gRankPage = page;
        for (Sprite*& s : header_) sprites().destroyRef(s);   // FUN_001f53c0
        build();                                                // FUN_001f4d80
    }
    // orig FUN_001f4d80 (work/decompiled/001f4d80.c)
    void build() {
        for (int i = 0; i < 8; ++i) {
            const float* p = kRankHeaderPos[i];
            const float* uv = kRankHeaderUv[i];
            header_[i] = sprites().create(p[0], p[1], 1.0f, p[2], p[3], uv[0], uv[1], uv[2], uv[3], 0x23, 2);
        }
        for (int r = 0; r < 10; ++r) {
            const float y = static_cast<float>(r) * 30.0f;
            for (int j = 0; j < 2; ++j) rank_[r][j] = sprites().create(static_cast<float>(0x19 + j * 0xb), y + 109.0f, 2.0f, 11.0f, 23.0f, 0, 0, 0, 0, 0x23, 1);
            for (int j = 0; j < 2; ++j) level_[r][j] = sprites().create(static_cast<float>(0x9e + j * 8), y + 116.0f, 2.0f, 8.0f, 12.0f, 0, 0, 0, 0, 0x23, 1);
            for (int j = 0; j < 6; ++j) value_[r][j] = sprites().create(static_cast<float>(0xbd + j * 8), y + 116.0f, 2.0f, 8.0f, 12.0f, 0, 0, 0, 0, 0x23, 1);
            for (int j = 0; j < 7; ++j) depth_[r][j] = sprites().create(static_cast<float>(0x110 + j * 8), y + 116.0f, 2.0f, 8.0f, 12.0f, 0, 0, 0, 0, 0x23, 1);
            for (int j = 0; j < 8; ++j) time_[r][j] = sprites().create(static_cast<float>(0x16a + j * 8), y + 116.0f, 2.0f, 8.0f, 12.0f, 0, 0, 0, 0, 0x23, 1);
        }
        for (int r = 0; r < 10; ++r)
            icon_[r] = sprites().create(439.0f, static_cast<float>(r) * 30.0f + 115.0f, 2.0f, 47.0f, 15.0f, 0, 0, 0, 0, 0x23, 1);
        if (language() == 1 || language() == 0)
            for (int r = 0; r < 10; ++r)
                difficulty_[r] = sprites().create(423.0f, static_cast<float>(r) * 30.0f + 115.0f, 2.0f, 47.0f, 12.0f, 0, 0, 0, 0, 0x22, 1);
    }

    static void setColor(Sprite* s, uint32_t c) { if (s != nullptr) s->color = c; }
    template <size_t N> static void glyphs(Sprite* (&row)[N], const char* buf, uint32_t c, bool show, void (*fn)(Sprite*, char)) {
        for (size_t j = 0; j < N; ++j) {
            if (show) {
                setColor(row[j], c);
                fn(row[j], buf[j]);
            } else {
                setColor(row[j], 0);
            }
        }
    }

    // orig FUN_001f3e80 (work/decompiled/001f3e80.c)
    void draw() {
        angle_ += 8;
        if (0x168 < angle_) angle_ = -360;
        // two FUN_00206940 calls whose results are unused
        if (alpha_ < 0x40 && fade().finished()) {
            alpha_ += 6;
            if (0x40 < alpha_) alpha_ = 0x40;
        }
        const uint32_t c = (alpha_ << 24) + 0x7f7f7f;
        for (int i = 0; i < 8; ++i) {
            Sprite* h = header_[i];
            if (h == nullptr) continue;
            if (gRankPage == 0) h->color = (i < 7) ? c : 0u;
            else if (gRankPage == 1) h->color = (i == 0 || i == 1 || i == 2 || i == 7) ? c : 0u;
        }
        char buf[24];
        for (int r = 0; r < 10; ++r) {                     // rank
            const int e = r + gRankTop;
            if (e < count_) {
                std::snprintf(buf, sizeof buf, "%2d", i32(entry(e), 0));   // 0x466448
                glyphs(rank_[r], buf, c, true, setRankGlyph);
            } else {
                setColor(rank_[r][0], 0);
                setColor(rank_[r][1], 0);
            }
        }
        TextRenderer& t = text();
        t.halfW = 8.0f;
        t.color = kTextRgb + (alpha_ << 24);
        t.halfH = 9.0f;
        t.layerFlag = 1;
        for (int r = 0; r < 10; ++r) {                     // name
            const int e = r + gRankTop;
            if (e < count_) {
                t.x = 63.0f;
                t.z = 1.0f;
                t.y = static_cast<float>(r) * 30.0f + 112.0f;
                t.draw("%s", reinterpret_cast<const char*>(entry(e) + 4));   // 0x466450
            }
        }
        for (int r = 0; r < 10; ++r) {                     // level
            const int e = r + gRankTop;
            if (e < count_) {
                std::snprintf(buf, sizeof buf, "%2d", i32(entry(e), 0x14));
                glyphs(level_[r], buf, c, true, setDigitGlyph);
            } else {
                setColor(level_[r][0], 0);
                setColor(level_[r][1], 0);
            }
        }
        for (int r = 0; r < 10; ++r) {                     // value (page 0)
            const int e = r + gRankTop;
            if (e < count_) {
                std::snprintf(buf, sizeof buf, "%6d", i32(entry(e), 0x18));   // 0x466458
                glyphs(value_[r], buf, c, gRankPage == 0, setDigitGlyph);
            } else {
                for (Sprite* s : value_[r]) setColor(s, 0);
            }
        }
        if (blink_ < 0x78) {                               // depth digits for 120 frames ...
            for (int r = 0; r < 10; ++r) {
                const int e = r + gRankTop;
                if (e < count_) {
                    const int fl = i32(entry(e), 0x1c);
                    if (fl == 99 && i32(entry(e), 0x20) == 1) std::snprintf(buf, sizeof buf, "%7d", -99999);   // 0x466460
                    else std::snprintf(buf, sizeof buf, "%7d", fl * -100);
                    glyphs(depth_[r], buf, c, gRankPage == 0, setDigitGlyph);
                } else {
                    for (Sprite* s : depth_[r]) setColor(s, 0);
                }
            }
        } else if (blink_ < 0xf0) {                        // ... then the dungeon name for 120 frames
            for (int r = 0; r < 10; ++r) {
                const int e = r + gRankTop;
                if (gRankPage == 0 && e < count_) {
                    const uint32_t type = static_cast<uint32_t>(i32(entry(e), 0x20));
                    if (type < 0xb) {
                        t.halfW = 8.0f;
                        t.halfH = 9.0f;
                        t.x = 252.0f;
                        t.y = static_cast<float>(r) * 30.0f + 112.0f;
                        t.z = 1.0f;
                        t.draw(str(static_cast<int>(type) + 0x50));
                    }
                }
                for (Sprite* s : depth_[r]) setColor(s, 0);
            }
        } else {
            blink_ = 0;
        }
        ++blink_;
        for (int r = 0; r < 10; ++r) {                     // play time
            const int e = r + gRankTop;
            if (e < count_) {
                const uint8_t* p = entry(e);
                uint16_t hour;
                std::memcpy(&hour, p + 0x26, 2);
                std::snprintf(buf, sizeof buf, "%02d:%02d:%02d", hour, p[0x25], p[0x24]);   // 0x466468
                glyphs(time_[r], buf, c, gRankPage == 0, setDigitGlyph);
            } else {
                for (Sprite* s : time_[r]) setColor(s, 0);
            }
        }
        for (int r = 0; r < 10; ++r) {                     // icon (page 0)
            const int e = r + gRankTop;
            Sprite* s = icon_[r];
            if (s == nullptr) continue;
            if (e < count_ && gRankPage == 0) {
                // UNVERIFIED: the table holds 2 entries; the original would read past it for icon values > 1
                const float* uv = kRankIconUv[entry(e)[0x28] & 1];
                s->u0 = uv[0] / 128.0f;
                s->v0 = uv[1] / 128.0f;
                s->u1 = uv[2] / 128.0f;
                s->v1 = uv[3] / 128.0f;
                s->color = c;
            } else {
                s->color = 0;
            }
        }
        if (gRankPage == 1) {                              // baroque name (page 1)
            for (int r = 0; r < 10; ++r) {
                const int e = r + gRankTop;
                if (e < count_ && -1 < i32(entry(e), 0x2c)) {
                    t.halfW = 8.0f;
                    t.color = kTextRgb + (alpha_ << 24);
                    t.halfH = 9.0f;
                    t.layerFlag = 1;
                    t.x = 189.0f;
                    t.y = static_cast<float>(r) * 30.0f + 112.0f;
                    t.z = 1.0f;
                    t.draw("%s\n", str(i32(entry(e), 0x2c) + 0x82));   // FUN_001fafb0, 0x466478
                }
            }
        }
        for (int r = 0; r < 10; ++r) {                     // difficulty (page 1)
            const int e = r + gRankTop;
            Sprite* s = difficulty_[r];
            if (s == nullptr) continue;
            if (e < count_ && gRankPage == 1) {
                // UNVERIFIED: 4 table entries; the original would read past them for values > 3
                const uint8_t d = static_cast<uint8_t>(entry(e)[0x30] & 3);
                const float* uv = kRankDifficultyUv[d];
                const float vdiv = (language() == 1) ? 64.0f : 128.0f;
                s->u0 = uv[0] / 128.0f;
                s->v0 = uv[1] / vdiv;
                s->u1 = uv[2] / 128.0f;
                s->v1 = uv[3] / vdiv;
                s->hw = kRankDifficultyWidth[d] * 0.5f;
                s->color = c;
            } else {
                s->color = 0;
            }
        }
        for (Sprite* s : frame_) setColor(s, c);
        for (Sprite* s : tab_) {
            if (s == nullptr) continue;
            s->color = c;
            const float* uv = kRankTabUv[gRankPage];
            s->u0 = uv[0] / 128.0f;
            s->v0 = uv[1] / 128.0f;
            s->u1 = uv[2] / 128.0f;
            s->v1 = uv[3] / 128.0f;
        }
        setColor(arrowUp_, gRankTop < 1 ? 0u : c);
        setColor(arrowDown_, count_ - 10 < gRankTop + 1 ? 0u : c);
        t.layerFlag = 1;
        t.color = kTextDefault;
    }

    int wantsNext_ = 0;              // +0x470
    int angle_ = 0;                  // +0x474
    unsigned alpha_ = 0;             // +0x478
    int count_ = 0;                  // +0x47c
    Sprite* header_[8] = {};         // +0x480
    Sprite* rank_[10][2] = {};       // +0x4a0
    Sprite* level_[10][2] = {};      // +0x4f0
    Sprite* value_[10][6] = {};      // +0x540
    Sprite* depth_[10][7] = {};      // +0x630
    Sprite* time_[10][8] = {};       // +0x748
    Sprite* icon_[10] = {};          // +0x888
    Sprite* difficulty_[10] = {};    // +0x8b0
    Sprite* frame_[0x11] = {};       // +0x8d8
    Sprite* tab_[2] = {};            // +0x91c
    Sprite* arrowUp_ = nullptr;      // +0x924
    Sprite* arrowDown_ = nullptr;    // +0x928
    int blink_ = 0;                  // +0x92c
};
}  // namespace
MenuPage* newRankingPage() { return new RankingPage(); }   // FUN_00169240 (alloc FUN_00169290 + ctor FUN_001f2480)
}  // namespace baroque::game::ui
