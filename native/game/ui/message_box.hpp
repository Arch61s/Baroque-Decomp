#pragma once
// Message box object 0x53d730 (dialogs of the front end, the save menus and the stage prompts) and its framed window
// 0x51b010 (FUN_00164f40 open / FUN_00165350 close / FUN_00165560 ready / FUN_001655a0 slide animation).
//   FUN_001ab430(box, msgId, arg) open: per-id set-up (+0x40/+0x44 geometry, default cursor +0x14, progress bar,
//     close delay +0x38), window, 10-tick input delay +0x3c, Game-scene map hide + BGM duck, SE 4.
//   FUN_001ac020 close, FUN_001ac4a0 per tick -> FUN_001ac510 alpha ramp + the per-id handlers (draw + input),
//   FUN_001ac7f0 decided choice (+0x18), FUN_001ac7d0 cursor (+0x14), FUN_001ab3f0 ready (window settled, no delay).
// Cursor / result values: YES 1 / NO 2 (Circle always gives 2), stairs menu 3..5, warp list 6..12, difficulty 0xd..0x10,
// file menu 0x11..0x14. Every window starts on the value the original set-up writes (mostly NO = 2).
#include <cstdint>
namespace baroque::game::ui {
struct Sprite;
class MessageBox {
public:
    void open(int msgId, int arg);          // FUN_001ab430
    void close();                           // FUN_001ac020
    bool isOpen() const { return open_ != 0; }   // +4 (iRam0053d734)
    bool ready() const;                     // FUN_001ab3f0: window settled and the input delay +0x3c elapsed
    int currentId() const { return msgId_; }// +0xc (iRam0053d73c)
    int choice() const;                     // FUN_001ac7f0: +0x18 while open, else 0
    int highlighted() const;                // FUN_001ac7d0: +0x14 while open, else 0
    void update();                          // FUN_001ac4a0
    int lastClosedId() const { return lastId_; }       // +0x20
    int lastResult() const { return lastResult_; }     // +0x1c
    bool mouseMovedCursor() const { return mouseMoved_; }   // PORT: the mouse moved the choice cursor this tick
private:
    void frame();                           // FUN_001ac510
    void releaseSprites();                  // FUN_001ac240
    void setup(int msgId, bool& bar, bool& backdrop);   // the switch of FUN_001ab430 (FUN_001ac9a0 .. FUN_001acdd0)
    void jpAccessSprite();                  // FUN_001b0c60
    // handlers (FUN_001ac510 switch)
    void group1();                          // FUN_001acdf0 ids 1..10
    void group2();                          // FUN_001ad190 ids 0xb..0x10
    void group3();                          // FUN_001ad430 ids 0x11..0x14
    void group4();                          // FUN_001ad680 ids 0x15..0x1a
    void group5();                          // FUN_001ad980 ids 0x1b..0x27
    void group6();                          // FUN_001ae0e0 ids 0x28..0x32
    void question(int strId);               // FUN_001ae4f0 / FUN_001ae5b0 / FUN_001ae670 (0x33 / 0x34 / 0x35)
    void bagCheck();                        // FUN_001ae730 id 0x36
    void warpList();                        // FUN_001ae900 ids 0x37 / 0x38
    void stairsMenu();                      // FUN_001af100 ids 0x39..0x3c
    void databaseStart();                   // FUN_001af650 id 0x3d
    void startConfirm();                    // FUN_001af6f0 id 0x3e
    void difficulty();                      // FUN_001af950 id 0x3f
    void configQuestion();                  // FUN_001afe00 ids 0x40 / 0x41
    void fileMenu();                        // FUN_001aff60 id 0x42
    void deleteFile();                      // FUN_001b0390 ids 0x43..0x46
    void yesNo();                           // FUN_001ac810
    void inputYesNo();                      // FUN_001b0680
    void inputWarp();                       // FUN_001b07b0
    void inputStairs();                     // FUN_001b0b10
    void inputDifficulty();                 // FUN_001afc80
    void inputFileMenu();                   // FUN_001b0220
    // shared text idioms of the handlers
    void lines(float y);                    // x 78, y, z 5 (0x429c0000, y, 0x40a00000)
    void centred(int strId, float y0);      // FUN_00113cf0 + FUN_00167250 from (78, y0), y += +0x40 * +0x44, z 5
    void draw(int strId);                   // FUN_00112e80(text, FUN_00114da0(id))
    void drawArg(int strId);                // FUN_00112e80(text, FUN_00114da0(id), +0x24)
    uint32_t alphaBits() const { return alpha_ * 0x1000000u; }
    // PORT: mouse menu layer (game/ui/mouse_menu.hpp). Choices register the rectangle the handler drew them in.
    void hit(int value, float x, float y, float w);
    void mouseHover();
    void mouseItems();
    int open_ = 0;              // +4
    int mapHidden_ = 0;         // +8 (the 2-D map was hidden by the Game-scene open)
    int msgId_ = 0;             // +0xc
    int sub_ = 0;               // +0x10 index inside the handler group
    int cursor_ = 0;            // +0x14
    int result_ = 0;            // +0x18
    int lastResult_ = 0;        // +0x1c
    int lastId_ = 0;            // +0x20
    int arg_ = 0;               // +0x24
    Sprite* jpSprite_ = nullptr;// +0x28 (JP access graphic, tex 0x30)
    Sprite* bar_ = nullptr;     // +0x2c progress bar (gradient texture, flag 2)
    Sprite* barBack_ = nullptr; // +0x30 progress bar track (untextured)
    uint32_t alpha_ = 0;        // +0x34 0..0x40
    int closeDelay_ = 0;        // +0x38 window backdrop kept for this many ticks after close
    int inputDelay_ = 0;        // +0x3c
    float yOffset_ = 0.0f;      // +0x40
    float scale_ = 1.0f;        // +0x44
    int frameCount_ = 0;        // +0x48
    // PORT: mouse menu layer
    struct Hit { int value; float x, y, w; };
    Hit hits_[8] = {};
    int hitCount_ = 0;
    bool mouseMoved_ = false;
};
MessageBox& messageBox();
// Window 0x51b010 of the message box
bool messageWindowReady();   // FUN_00165560: open (iRam0051b108) and the opening slide finished (iRam0051b118 < 1)
void messageWindowTick();    // FUN_001655a0: slide / backdrop release, called at the end of FUN_00167d80 every tick
}
