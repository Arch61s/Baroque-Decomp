#pragma once
// Shared menu object 0x51b140 and its pages (SCENES.md 11). FUN_001677c0(menu, mode) open, FUN_00167910 close,
// FUN_00167d70 is-open, FUN_00167d80 per-frame, FUN_00168010 brightness overlay.
// Page vtable: +0xc open, +0x10 close, +0x14 is-active, +0x18 update/draw, +0x1c input, +0x20 wants-next, +0x24 next page.
// Page base object (FUN_00163be0): +4 frame-made, +8 / +0xc / +0x10 sub-window A / B / C open, +0x14 game-scene tiles[4],
// +0x24 title bar sprite, +0x28 underline sprite, +0x2c title y, +0x30 title settled, +0x34 letter sprites[64],
// +0x134 frame sprites[22] (tex 0x31), +0x18c frame2 sprites[61] (tex 0x2e), +0x280 help-bar background (tex 0x25),
// +0x284 sub-window A pieces[45] (+0x338 [2]), +0x340 sub-window B pieces[43] (+0x3ec [2]), +0x3f4 sub-window C
// pieces[29] (+0x468), +0x46c is-open.
// Fields from +0x470 on belong to the concrete page.
#include <cstdint>
namespace baroque::game::ui {
struct Sprite;
class MenuPage {
public:
    virtual ~MenuPage() = default;
    virtual void open() = 0;                       // +0xc
    virtual void close() = 0;                      // +0x10
    virtual bool isActive() const { return isOpen != 0; }   // +0x14 (every page returns +0x46c)
    virtual void update() = 0;                     // +0x18
    virtual void input() = 0;                      // +0x1c
    virtual bool wantsNext() const { return false; }        // +0x20
    virtual MenuPage* nextPage() { return nullptr; }        // +0x24
    virtual const char* debugName() const = 0;
    // PORT: mouse menu layer (game/ui/mouse_menu.hpp), not in the original vtable. MenuObject::update() calls
    // mouseInput() right before input() (applies the hovered item to the page cursor) and mouseItems() after the
    // page update (registers the selectable rectangles for the next pad sample).
    virtual void mouseInput() {}
    virtual void mouseItems() {}
    int isOpen = 0;                                // +0x46c
protected:
    void makeFrame();                              // FUN_001642d0
    void makeGameBackdrop();                       // FUN_00164910 (Game / Ending scene pages)
    void makeTitle(const char* title);             // FUN_00164a30 (+ FUN_00164d00 letter UVs)
    void makeFrame2(int variant);                  // FUN_001664b0
    void animateTitle();                           // FUN_00164dd0 (FUN_00164f30, called right after it, is empty)
    void freeFrame();                              // FUN_00164460
    void freeFrame2();                             // FUN_00166640
    void openSubWindowA(float y);                  // FUN_00165a10
    void closeSubWindowA();                        // FUN_00165b80
    void openSubWindowB();                         // FUN_00165d90
    void closeSubWindowB();                        // FUN_00165ef0
    void openSubWindowC();                         // FUN_00166100
    void closeSubWindowC();                        // FUN_00166240
    void colorSubWindowC(uint32_t alphaBits);      // FUN_00166420
    int frame2WideRole(int i, const float* p, int& anchor) const;   // PORT: widescreen role of a FUN_001664b0 piece
    int frameMade = 0;                             // +4
    int subWindowA = 0;                            // +8
    int subWindowB = 0;                            // +0xc
    int subWindowC = 0;                            // +0x10
    Sprite* backdropTiles[4] = {};                 // +0x14 (FUN_00164910, freed by FUN_00164460)
    Sprite* titleBar = nullptr;                    // +0x24
    Sprite* underline = nullptr;                   // +0x28
    float titleY = -80.0f;                         // +0x2c
    int titleSettled = 0;                          // +0x30
    Sprite* letters[64] = {};                      // +0x34
    Sprite* frame[22] = {};                        // +0x134
    Sprite* frame2[61] = {};                       // +0x18c
    Sprite* helpBg = nullptr;                      // +0x280
    Sprite* subA[45] = {};                         // +0x284
    Sprite* subA2[2] = {};                         // +0x338
    Sprite* subB[43] = {};                         // +0x340
    Sprite* subB2[2] = {};                         // +0x3ec
    Sprite* subC[29] = {};                         // +0x3f4
    Sprite* subC2 = nullptr;                       // +0x468
    // PORT widescreen (presentation only): makeFrame / makeFrame2 move the left-edge frame pieces to the area's left
    // edge. The CHARACTER LIST clears it before makeFrame(): its list column is joined to the left pipe column.
    int portWideLeft = 1;
};
class MenuObject {
public:
    void open(int mode);                           // FUN_001677c0
    void close();                                  // FUN_00167910
    bool isOpen() const { return isOpen_ != 0; }   // FUN_00167d70
    void update();                                 // FUN_00167d80
    void updateBrightness(int8_t brightness);      // FUN_00168010
    MenuPage* page() const { return page_; }
    unsigned counter() const { return counter_; }
    static MenuPage* createPage(int mode);         // the switch in FUN_001677c0
private:
    void loadPageTextures();                       // FUN_00167390
    void releasePageTextures();                    // the release half of FUN_00167910
    void selectOpensConfig();                      // FUN_00167eb0
    int isOpen_ = 0;                               // +0x46c
    unsigned counter_ = 0;                         // +0x470
    MenuPage* page_ = nullptr;                     // +0x474
    char brightnessCache_ = 0;                     // +0x478
    Sprite* brightnessQuad_ = nullptr;             // +0x47c
};
MenuObject& menu();
// The UI slice of the engine tick FUN_00101bd0: FUN_0015c380(0x5180f0) help/text windows, FUN_00167d80(0x51b140)
// menu pages, FUN_00168010(0x51b140, brightness option). In the original these run inside the tick after the
// message box (FUN_001ac4a0); see uiTick() in menu.cpp for where the port calls it.
void uiTick();
// page factories (ctors): FUN_00163b10 OPTION main, FUN_00163a20 GAME CONFIG, FUN_00169010 NAME ENTRY,
// FUN_00168fc0 FILE_SELECT, FUN_00163ac0 KEY ASSIGN, FUN_00163a70 BRIGHTNESS, FUN_00169240..FUN_00169060 database pages
MenuPage* newOptionPage();
MenuPage* newGameConfigPage();
MenuPage* newNameEntryPage();
MenuPage* newFileSelectPage();
MenuPage* newKeyAssignPage();
MenuPage* newBrightnessPage();
MenuPage* newDatabasePage(int mode);   // modes 0..6: RANKING, EVENT LIST, ITEM LIST, VOICE LIST, BAROQUE LIST, CHARACTER LIST, SOUND TEST
// PORT: PC SETTINGS page (page_pc_settings.cpp, menu mode 11; not in the original). from: 0 = OPTION main page
// (Circle returns there), 1 = GAME CONFIG item 10 (Circle returns to GAME CONFIG).
MenuPage* newPcSettingsPage(int from);
// FILE_SELECT globals (DAT_00469700 / DAT_00469708 / uRam0053ea48 live in front() of game/scenes/front_scenes.hpp)
struct FileSelectGlobals {
    int flow = 0;            // uRam0053ea20: 1 title flow (GameLoad scene), 2 in-game save flow
    int state = 1;           // uRam0053ea28
    int saved = 0;           // uRam0053ea30 (set after a successful save)
    int cardChanged = 0;     // uRam0053ea38 (FUN_00203440 result)
    int formatTimer = 0;     // iRam0053ea50
    signed char cursor = -1; // DAT_004e5cb8 (.data initial 0xff)
};
FileSelectGlobals& fileSelect();
struct NameEntryGlobals {
    char name[16] = {};      // 0x51b5f8 (5 full-width SJIS chars + NUL)
    signed char pos = 0;     // cRam004e5e00
    int canCancel = 0;       // uRam0051b5f0 (1 while the name is still the default, the cursor at 0 and no dialog)
};
NameEntryGlobals& nameEntry();
void nameEntryReset();                 // FUN_0016ace0: strcpy(0x51b5f8, default name); position = 0
void nameEntrySet(const char* name);   // FUN_0016ac80
bool nameEntryChanged();               // FUN_0016ad10: name != default
// orig FUN_00166eb0: button glyph sprite for a pad mask (face buttons tex 0x28, shoulders tex 0x27; nullptr otherwise)
Sprite* buttonIcon(float x, float y, float z, float w, float h, uint32_t mask, int flag);
// orig FUN_00166a50: set a sprite to a digit / punctuation glyph of TEX_UI00 (uv and half size)
void setDigitGlyph(Sprite* s, char c);
// orig FUN_00206940: sine of an angle in degrees through the 1024-entry table 0x469830
float sinDeg(float deg);
// the float -> int conversion with the 2^31 wrap the compiler emitted for (int)f on values >= 2^31
inline int ftoiWrap(float f) { return (2.1474836e+09f <= f) ? static_cast<int>(f - 2.1474836e+09f) : static_cast<int>(f); }
// menu cursors that persist across pages (reset by the CURSOR POSITION option)
extern signed char gOptionCursor;     // cRam004e5dfc
extern signed char gConfigCursor;     // cRam004e5df8
extern int gOptionsChanged;           // uRam0051af20
}
