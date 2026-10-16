#pragma once
// In-game HUD (ITEMS_HUD_MENUS.md 6): status HUD object 0x5179c0 (FUN_00150ba0) on persistent sprites of the UI layer
// 0x4e68c0, item ribbon part of 0x517b60 (FUN_00152f40 / FUN_0015b2d0 / FUN_00153540), lock-on marker.
// Message log and 2D map: message_log.hpp / map2d.hpp. Integration entry points: hud_api.hpp.
#include "game/hud/sprite2d.hpp"
#include "game/items/inventory.hpp"

namespace baroque::game {
namespace ui { struct Sprite; }

// Gameplay button configuration words (0x4e65c0.., RUNTIME.md 2.4) used by the HUD glyphs
struct HudButtons {
    int slotPrev = 4;      // FUN_00153510: L1
    int slotNext = 8;      // FUN_00153500: R1
    int itemScreen = 0x10; // FUN_001534f0: triangle (uRam004e65d8)
    int talk = 0x20;       // uRam004e65c0 (talk-mode glyph); hudGameTick copies the four words from buttons() every tick
};

// Conditions the HUD ticks test (RUNTIME.md 1.6 globals).
struct HudVisibility {
    bool sceneIsGame = true;        // uRam0053ea90 == "Game...."
    bool floorGenerated = true;     // uRam0050a610 & 1
    bool systemMenuOpen = false;    // FUN_00167d70(0x51b140)
    bool eventRunning = false;      // iRam0051b620
    bool fading = false;            // iRam004e7898 (fade frames left)
    bool dialogOpen = false;        // iRam0053bf88
    bool talkActive = false;        // FUN_001a2950(0x53c6e0)
    bool paused = false;            // FUN_00102020(0x4e6640)
    bool talkSystemBusy = false;    // iRam0053c6a8
};

// Status HUD 0x5179c0. Field comments give the original offsets.
class StatusHud {
public:
    // iRam0050b144 (nullptr = no player: FUN_0014ecc0 / FUN_00151660 / FUN_00151be0 skip the status reads)
    CharacterAccess* player = nullptr;
    HudButtons buttons;

    void resetCounters();                                // orig FUN_0014ec20 (static ctor FUN_004c7c50)
    void createTextures();                               // orig FUN_0014ee70 (Game Init FUN_0011b300)
    void releaseTextures();                              // orig FUN_0014eef0 (FUN_0011bcb0)
    void tick(const HudVisibility& vis);                 // orig FUN_00150ba0
    void setHp(int hp) { hpTarget_ = hp; }               // orig FUN_00151ae0 (called by FUN_001349e0 / FUN_00134ef0 for the player)
    void setVt(int vt) { vtTarget_ = vt; }               // uRam00517b2c = vt (FUN_00134b60 for the player)
    void setDepth(int d) { depthTarget_ = d; }           // iRam00517b34 = depth (FUN_0011a9f0 / FUN_00129c60)
    void refreshBars(uint32_t which);                    // orig FUN_00151af0 (0 = all, 1 HP, 2 VT, 4 depth)
    void layout(int flag) { layoutBars(flag != 0); }     // orig FUN_00151660 (flag != 0: backgrounds only)

    // inspection (check / tests)
    bool built() const { return built_ != 0; }
    int talkMode() const { return talkMode_; }
    uint32_t fade() const { return fade_; }
    int hpEased() const { return hpEased_; }
    int vtEased() const { return vtEased_; }
    int hpMax() const { return hpMax_; }
    int vtMax() const { return vtMax_; }
    int depthEased() const { return depthEased_; }
    int levelFrame() const { return iconIndex_; }
    const ui::Sprite* hpFill() const { return hpFill_; }
    const ui::Sprite* hpBack() const { return hpBack_; }
    const ui::Sprite* vtFill() const { return vtFill_; }
    const ui::Sprite* vtBack() const { return vtBack_; }
    const ui::Sprite* hpDigit(int i) const { return hpDigits_[i]; }
    const ui::Sprite* vtDigit(int i) const { return vtDigits_[i]; }
    const ui::Sprite* depthDigit(int i) const { return depthDigits_[i]; }
    const ui::Sprite* levelDigit(int i) const { return levelDigits_[i]; }
    int texHp() const { return texHp_; }
    int texVt() const { return texVt_; }
    int texBack() const { return texBack_; }
    int spriteCount() const;                             // live sprites owned by the HUD

    void release();                                      // orig FUN_0014f8c0 (also called by the menu FUN_001642d0)

private:
    void init();                                         // orig FUN_0014ecc0
    void build();                                        // orig FUN_0014ef40
    void releaseTalkStrip();                             // orig FUN_001508f0
    void buildTalkStrip();                               // orig FUN_00150680
    void setTalkMode(int mode);                          // orig FUN_00150610
    void clearTalkMode();                                // orig FUN_00150650
    void ease();                                         // orig FUN_00151440
    void updateDigits();                                 // orig FUN_00151be0
    void layoutBars(bool backOnly);                      // orig FUN_00151660
    void setLevelFrame(int frame);                       // orig FUN_00151e00
    void uploadBar(int tex, uint32_t* px, int value, int max, int& state);   // FUN_00151960 + FUN_00106650

    int built_ = 0;                 // +0x000
    int talkMode_ = 0;              // +0x004 (0 normal, 1 / 2 talk modes)
    uint32_t fade_ = 0;             // +0x008 (0..0x40, +0x15/tick; alpha of the talk strip)
    int texBack_ = -1;              // +0x00c 4x4 bar background
    int texHp_ = -1;                // +0x010 64x4 HP bar
    int texVt_ = -1;                // +0x014 64x4 VT bar
    ui::Sprite* hpFill_ = nullptr;  // +0x018
    ui::Sprite* hpBack_ = nullptr;  // +0x01c
    ui::Sprite* vtFill_ = nullptr;  // +0x020
    ui::Sprite* vtBack_ = nullptr;  // +0x024
    ui::Sprite* hpLabel_ = nullptr; // +0x028
    ui::Sprite* vtLabel_ = nullptr; // +0x02c
    ui::Sprite* hpDigits_[7] = {};  // +0x030
    ui::Sprite* vtDigits_[7] = {};  // +0x04c
    ui::Sprite* depthDigits_[8] = {};   // +0x068
    ui::Sprite* cubitLabel_ = nullptr;// +0x088 "cubit" unit label (TEX_UI00 (0,48)-(24,58))
    ui::Sprite* frame_[13] = {};    // +0x08c
    ui::Sprite* slide_[6] = {};     // +0x0c0
    ui::Sprite* itemFrame_[7] = {}; // +0x0d8
    ui::Sprite* levelLabel_ = nullptr;   // +0x0f4
    ui::Sprite* levelDigits_[2] = {};    // +0x0f8
    ui::Sprite* levelIcon_ = nullptr;    // +0x100
    int iconIndex_ = -1;            // +0x104 (level-change animation frame 0..0x13, -1 none)
    int8_t iconCounter_ = 0;        // +0x108
    int levelCache_ = 0;            // +0x10c
    uint32_t levelDigitAlpha_ = 0;  // +0x110
    ui::Sprite* talkStrip_[13] = {};// +0x114
    ui::Sprite* talkButton_ = nullptr;   // +0x148
    ui::Sprite* talkExtra_ = nullptr;    // +0x14c
    uint32_t* pxHp_ = nullptr;      // +0x150 (FUN_00106630 buffer of +0x10)
    uint32_t* pxVt_ = nullptr;      // +0x154
    int level_ = 0;                 // +0x158
    int hpMax_ = 0;                 // +0x15c
    int hpTarget_ = 0;              // +0x160
    int hpEased_ = 0;               // +0x164
    int vtMax_ = 0;                 // +0x168
    int vtTarget_ = 0;              // +0x16c (0x517b2c)
    int vtEased_ = 0;               // +0x170
    int depthTarget_ = 0;           // +0x174 (0x517b34)
    int depthEased_ = 0;            // +0x178
    int hpBarState_ = 0;            // +0x17c
    int vtBarState_ = 0;            // +0x180
    int barsDirty_ = 0;             // +0x184
};

// Item ribbon (closed inventory view) of 0x517b60: fields +0x10, +0x18, +0x28, +0x44, +0x50, +0x5c, +0x11c..+0x1d4
class ItemRibbon {
public:
    // the ribbon half of FUN_00156de0 (screenPage = +0 of 0x517b60: -1 closed, 0 inventory, 1 bag)
    void tick(ItemSystem& sys, const HudVisibility& vis, int screenPage, bool screenOpen, Sprite2D& out);
    void draw(Sprite2D& out) const;
    void stepPrev(ItemSystem& sys);                      // orig FUN_00157410 (closed-screen path)
    void stepNext(ItemSystem& sys);                      // orig FUN_001575f0
    int cursor() const { return cursor_; }               // orig FUN_001573f0 (+0x28)
    void setCursor(int c) { cursor_ = c; }               // orig FUN_00157400
    bool visible() const { return visible_; }
    float slide() const { return slide_; }
    int hintVariant() const { return hintVariant_; }
    void requestHide();                                  // orig FUN_00153520 (+0x18 = 8 when idle)
    HudButtons buttons;
    const HudSprite& frame() const { return frame_; }
    const HudSprite& icon() const { return icon_; }
private:
    void build(ItemSystem& sys);                         // orig FUN_00152f40
    void release();                                      // orig FUN_001567e0
    void layout(ItemSystem& sys, bool menuOpen, Sprite2D& out);   // orig FUN_0015b2d0
    void updateHint(ItemSystem& sys, bool screenOpen);   // orig FUN_00153540
    void setHintVariant(ItemSystem& sys, int v);         // FUN_00152dc0 / FUN_00152e20 / FUN_00152e80 / FUN_00152ee0
    bool visible_ = false;          // +0x10
    int hideCountdown_ = -1;        // +0x18
    int cursor_ = -1;               // +0x28
    int lastCount_ = 0;             // +0x44
    float slide_ = 0;               // +0x50 (0..128)
    int hintVariant_ = 0;           // +0x5c
    HudSprite frame_;               // +0x11c
    HudSprite parts_[13];           // +0x120..
    HudSprite ticks_[27];           // +0x154..
    HudSprite icon_;                // +0x1c0
    HudSprite hintPrev_, hintNext_; // +0x1c4, +0x1c8
    HudSprite hintScreen_;          // +0x1cc
    HudSprite countDigits_[2];      // +0x1d0, +0x1d4
    struct { bool on = false; const char* text = nullptr; char buf[0x60] = {}; float x = 0, y = 0; } name_;
};

// The lock-on marker (player control +0x10, FUN_0012a4e0 -> FUN_00144560) is a 3-D pass and not part of this
// module (its texture / UVs were not recovered).

}  // namespace baroque::game
