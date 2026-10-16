#pragma once
// Item Screen (triangle) of object 0x517b60 (ITEMS_HUD_MENUS.md 7): state machine, input table FUN_00157a60,
// context actions table 0x48f580 + FUN_00158890, description pages FUN_00154db0 / FUN_00153f00, bag page.
// Drawing is the original retained-sprite model: FUN_001524b0 / FUN_00153790 / FUN_00155500 / FUN_0015a580 /
// FUN_00152a00 create persistent sprites of the UI layer 0x4e68c0 (kept in the slots below, original offsets),
// FUN_001591e0 re-colours them every tick and draws the text rows (FUN_00159bd0) through the text object 0x4e7e90;
// the description and name lines go to the text-window slots 1 / 2 of 0x5180f0 (ui::helpBar(), FUN_0015c490).
#include "game/hud/sprite2d.hpp"
#include "game/items/item_world.hpp"

namespace baroque::game {
namespace ui { struct Sprite; }

// libpad bits (RUNTIME.md 2.2): pressed / repeat words of the pad object 0x4e6280 (+8 / +0x10)
struct PadInput {
    uint32_t pressed = 0;
    uint32_t repeat = 0;
    static constexpr uint32_t kL2 = 1, kR2 = 2, kL1 = 4, kR1 = 8, kTriangle = 0x10, kCircle = 0x20, kCross = 0x40, kSquare = 0x80,
                              kStart = 0x800, kUp = 0x1000, kRight = 0x2000, kDown = 0x4000, kLeft = 0x8000,
                              kUp2 = 0x10000, kDown2 = 0x20000, kLeft2 = 0x40000, kRight2 = 0x80000;
};

// context action ids (labels 0x1a1 + action; 0x1ae "Unequip")
enum : int { kActEquip = 0, kActThrow = 1, kActPlace = 2, kActEat = 3, kActOpen = 4, kActInject = 5, kActPush = 6, kActDeport = 7,
             kActPlaceHeads = 8, kActPlaceTails = 9, kActParasite = 10, kActGive = 11, kActFuse = 12 };
extern const int kContextActions[14][4];   // 0x48f580 [V words]

struct ItemScreenEnv {
    bool sceneIsGame = true;        // uRam0053ea90
    bool systemMenuOpen = false;    // FUN_00167d70(0x51b140)
    bool eventRunning = false;      // iRam0051b620
    bool talkBusy = false;          // iRam0053c6a8
    bool talkChoice = false;        // iRam0053c6c8 == 1
    bool talkWindow = false;        // iRam0053c710
    bool paused = false;            // FUN_00102020(0x4e6640)
    bool floorGenerated = true;     // uRam0050a610 & 1
    bool playerNoInput = false;     // uRam0050b620 & 0x10000 (player control +0x00: no input, dead / drain)
    bool cursorResetOption = false; // iRam0051af34 == 1 (CURSOR POSITION option)
    int itemScreenButton = PadInput::kTriangle;   // uRam004e65d8
};

class ItemScreen {
    friend struct ItemScreenMouse;   // PORT: mouse layer (game/items/item_screen_mouse.cpp)
public:
    // orig FUN_001520d0 screen fields (Game Init); also releases sprites a previous Game scene left (port: the
    // original never re-inits while the screen is open)
    void reset();
    void requestOpen() { openRequest_ = 1; }                    // +0x52c
    void requestBag() { bagRequest_ = 1; }                      // +0x530 (talk system FUN_001a3320)
    // orig FUN_0015c070 (from FUN_0013aaa0): playerDead = the inline type 0/2 + HP 0 test on iRam0050b144,
    // eventCamera = iRam0053c6e8 != 0; logEnabled = message log +0x2b00 (uRam0051abf0)
    void bagRequestTick(ItemSystem& sys, const ItemScreenEnv& env, bool playerDead, bool eventCamera, int& logEnabled);
    void input(ItemSystem& sys, const PadInput& pad, const ItemScreenEnv& env);   // orig FUN_00157a60
    // screen half of FUN_00156de0: close countdown, then (open) FUN_00157310 screen part -> FUN_00159150 /
    // FUN_001591e0 / caption 0x2e, tick counter +0x53c, open-request clear. playerDead as for bagRequestTick;
    // hasPlayer = iRam0050b144 != 0.
    void tick(ItemSystem& sys, const ItemScreenEnv& env, bool hasPlayer, bool playerDead);
    void closeRequest(ItemSystem& sys) { requestClose(sys); }   // FUN_001529a0(0x517b60) for other modules
    bool isOpen() const { return open_ != 0; }                  // +8
    bool ready() const;                                         // orig FUN_001561b0
    int page() const { return page_; }
    int mode() const { return mode_; }
    bool contextOpen() const { return ctxOpen_ != 0; }
    int contextAction() const { return ctxAction_; }
    int cursor() const { return cursor_[0]; }
    int bagCursor() const { return cursor_[1]; }
    int targetCursor() const { return targetCursor_; }
    int listPage() const { return listPage_[0]; }
    int descPage() const { return descPage_; }
    int hintVariant() const { return hintVariant_; }
    int closeCountdown() const { return closeCountdown_; }
    uint32_t fade() const { return fade_; }
    int ribbonCursor() const { return cursor_[0]; }             // +0x28 is shared with the HUD ribbon
    void setRibbonCursor(int c) { cursor_[0] = c; }
    MenuItemAction& menuAction() { return action_; }
    // gift fields read by the talk selector FUN_001a3320
    uint32_t giftTarget() const { return placeTargetId_; }      // orig FUN_0015c2c0 (+0x534)
    Item* giftItem() const { return placeItem_; }               // orig FUN_0015c2d0 (+0x538)
    void giftClear() { placeTargetId_ = 0xffffffffu; }          // orig FUN_0015c2e0
    int lastMessageState() const { return lastState_; }
    int spriteCount() const;                                    // live sprites owned by the screen (check)
    const ui::Sprite* backdrop() const { return backdrop_; }
    const ui::Sprite* expDigit(int i) const { return expDigits_[i]; }
    const ui::Sprite* equipMark(int i) const { return equipMarks_[i]; }
    const ui::Sprite* contextBox() const { return ctxBox_; }
private:
    void openTick(ItemSystem& sys, const ItemScreenEnv& env);  // orig FUN_00157fa0
    void open(ItemSystem& sys, int page, const ItemScreenEnv& env);   // orig FUN_001524b0
    void close(ItemSystem& sys);                                // orig FUN_001561f0
    void requestClose(ItemSystem& sys);                         // orig FUN_001529a0
    void openContext(ItemSystem& sys);                          // orig FUN_00152a00
    void closeContext(ItemSystem& sys);                         // orig FUN_00152ba0 (+ FUN_00152be0)
    void cancel(ItemSystem& sys);                               // orig FUN_001580a0
    void pageLeft(ItemSystem& sys);                             // orig FUN_00158170
    void pageRight(ItemSystem& sys);                            // orig FUN_001582c0
    void cursorUp(ItemSystem& sys);                             // orig FUN_00158430
    void cursorDown(ItemSystem& sys);                           // orig FUN_00158500
    void stepPrev(ItemSystem& sys);                             // orig FUN_00157410
    void stepNext(ItemSystem& sys);                             // orig FUN_001575f0
    void stepListPrev(ItemSystem& sys, int count, int p);       // PORT: Up through the whole open list
    void stepListNext(ItemSystem& sys, int count, int p);       // PORT: Down through the whole open list
    void showListPageOf(ItemSystem& sys, int p, int idx);       // PORT: list page follows the cursor
    void targetPrev(ItemSystem& sys);                           // orig FUN_00157760
    void targetNext(ItemSystem& sys);                           // orig FUN_00157890
    void actionStep(int delta);                                 // orig FUN_0015b290
    void executeContext(ItemSystem& sys);                       // orig FUN_00158890
    void executeTwoItem(ItemSystem& sys);                       // orig FUN_001585d0
    void bagTake(ItemSystem& sys);                              // orig FUN_001586a0
    void bagGreeting(ItemSystem& sys);                          // orig FUN_0015c1a0
    bool targetExists(ItemSystem& sys) const;                   // orig FUN_001579c0
    void descriptionPage(ItemSystem& sys, int delta);           // orig FUN_00154db0
    bool descriptionPageValid(ItemSystem& sys, int delta) const;   // orig FUN_001552e0
    void updateHints(ItemSystem& sys, int cur);                 // orig FUN_00154ba0
    void buildDescription(ItemSystem& sys);                     // orig FUN_00153f00
    void gunDescription(ItemSystem& sys, const Item& it);       // orig FUN_00154ab0
    void clampCursor(ItemSystem& sys);                          // orig FUN_0015bfe0
    void keepCursorPage(ItemSystem& sys);                       // orig FUN_00152930
    int pageCount(ItemSystem& sys) const;                       // orig FUN_0015c020
    Item* itemAt(ItemSystem& sys, int idx) const;
    bool actionEnabled(ItemSystem& sys, int idx, int action);   // orig FUN_0015b170 (jump table 0x48f6a0)
    // sprite builders / releasers
    void buildFrame();                                          // orig FUN_00153790
    void releaseFrame();                                        // orig FUN_00153b20
    void setFrameVariant(int v);                                // orig FUN_00153710 / FUN_00153750
    void buildPanel(ItemSystem& sys);                           // orig FUN_00155500
    void releasePanel();                                        // orig FUN_00155a30
    void buildTabs(int n);                                      // orig FUN_00155970
    void dropSecondPage(ItemSystem& sys);                       // orig FUN_001554d0
    void refreshRows(ItemSystem& sys);                          // orig FUN_0015a350 -> FUN_0015ac90 + FUN_0015a580
    void releaseRows();                                         // orig FUN_0015ac90
    void buildRows(ItemSystem& sys);                            // orig FUN_0015a580
    void releaseContext();                                      // orig FUN_00152be0
    void releaseAll();                                          // port: every slot (reset / re-open)
    // per-tick drawing
    void updateBackdrop();                                      // orig FUN_00159150
    void update(ItemSystem& sys, const ItemScreenEnv& env);     // orig FUN_001591e0
    void updateStats(ItemSystem& sys);                          // orig FUN_0015bdb0
    void drawRows(ItemSystem& sys);                             // orig FUN_00159bd0

    int page_ = -1;             // +0x00
    int mode_ = 1;              // +0x04
    int open_ = 0;              // +0x08
    int ctxOpen_ = 0;           // +0x0c
    int closeCountdown_ = -1;   // +0x14
    int listPage_[2] = {0, 0};  // +0x1c / +0x20
    int savedPage_ = 0;         // +0x24
    int cursor_[2] = {-1, -1};  // +0x28 / +0x2c
    int pageWrapped_ = 0;       // +0x30
    int ctxAction_ = 0;         // +0x34
    int targetCursor_ = -1;     // +0x38
    int descPage_ = 0;          // +0x40
    int tabCount_ = 0;          // +0x48
    uint32_t fade_ = 0;         // +0x4c
    int hintVariant_ = 1;       // +0x54 (0 L1+R1, 1 none, 2 L1, 3 R1)
    int twoPage_ = 0;           // +0x58
    int mapWasOpen_ = 0;        // +0x524
    int openRequest_ = 0;       // +0x52c
    int bagRequest_ = 0;        // +0x530
    uint32_t placeTargetId_ = 0;// +0x534
    Item* placeItem_ = nullptr; // +0x538
    int ticks_ = 0;             // +0x53c
    int lastState_ = 0;
    MenuItemAction action_;
    // sprites (original slots)
    ui::Sprite* backdrop_ = nullptr;          // +0x60
    ui::Sprite* frame_[32] = {};              // +0x64
    ui::Sprite* frameVar_[8] = {};            // +0xe4
    ui::Sprite* frame03_[4] = {};             // +0x104
    ui::Sprite* hints_[2] = {};               // +0x114 / +0x118
    ui::Sprite* panelPieces_[0x38] = {};      // +0x1d8
    ui::Sprite* panel2Pieces_[0x12] = {};     // +0x2b8
    ui::Sprite* panel_ = nullptr;             // +0x300
    ui::Sprite* titleBar_ = nullptr;          // +0x304
    ui::Sprite* caption_ = nullptr;           // +0x308 "ITEM"
    ui::Sprite* sortCaption_ = nullptr;       // +0x30c (square "sort" hint caption)
    ui::Sprite* sortIcon_ = nullptr;          // +0x310 (square glyph)
    ui::Sprite* tabs_[5] = {};                // +0x314
    ui::Sprite* arrows_[2] = {};              // +0x328
    ui::Sprite* equipMarks_[4] = {};          // +0x330
    ui::Sprite* sealMarks_[10] = {};          // +0x340
    ui::Sprite* unusedMarks_[10] = {};        // +0x368 (released / re-coloured, never created)
    ui::Sprite* classIcons_[10] = {};         // +0x390
    ui::Sprite* numDigits_[10][3] = {};       // +0x3b8
    ui::Sprite* ctxPieces_[24] = {};          // +0x430
    ui::Sprite* ctxBox_ = nullptr;            // +0x490
    ui::Sprite* expLabel_ = nullptr;          // +0x494
    ui::Sprite* expDigits_[13] = {};          // +0x498
    ui::Sprite* atkLabel_ = nullptr;          // +0x4cc
    ui::Sprite* atkDigits_[10] = {};          // +0x4d0
    ui::Sprite* defLabel_ = nullptr;          // +0x4f8
    ui::Sprite* defDigits_[10] = {};          // +0x4fc
};

// PORT: mouse layer of the item screen (game/items/item_screen_mouse.cpp, linked into Baroque-Native only; the hooks
// stay null in the checks). `hover` runs right before ItemScreen::input (FUN_00157a60) and applies the hovered row /
// context entry like the pad's cursor step; `items` runs after ItemScreen::tick and registers this tick's rectangles.
struct ItemScreenMouseHooks {
    void (*hover)(ItemScreen& s, ItemSystem& sys, const ItemScreenEnv& env) = nullptr;
    void (*items)(ItemScreen& s, ItemSystem& sys, const ItemScreenEnv& env) = nullptr;
};
inline ItemScreenMouseHooks gItemScreenMouse;

}  // namespace baroque::game
