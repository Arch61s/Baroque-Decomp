#pragma once
// Menu-driven front-end scenes (work/reports/SCENES.md 5, 7, 8), translated from the pseudocode:
//   Option   0x50a210 (vtable 0x4ccaf0; FUN_00119910 init / FUN_00119d60 exit / FUN_00119f40 update)
//   NameEntr 0x51b5d0 (vtable 0x4cdd80; FUN_001693b0 init / 0x169430 exit / FUN_00169440 update)
//   GameLoad 0x53ea00 (vtable 0x4cf2e0; FUN_00200db0 init / 0x200e30 exit / FUN_00200e40 update)
// Each hosts the shared menu object 0x51b140 (game/ui/menu.hpp) over a 2x2 background of tex 0x3f..0x42.
#include "game/scene_manager.hpp"

namespace baroque::game {

namespace ui { struct Sprite; }

// EvMovie <-> Option hand-over: DAT_004697f8 movie index (FUN_002064c0) and DAT_00469800 menu mode the Option scene
// opens after the movie (FUN_002064d0 / FUN_002064e0). Both are -1 in .data.
struct EvMovieGlobals {
    int movieIndex = -1;    // DAT_004697f8
    int optionMode = -1;    // DAT_00469800
};
EvMovieGlobals& evMovie();

// The scene Update tail used by the menu scenes: FUN_00101bd0 including its UI slice (help bar, menu object,
// brightness overlay). See menuSceneTick() in menu_scenes.cpp for the port ordering.
void menuSceneTick();

class OptionScene : public Scene {   // 0x50a210
public:
    void init() override;            // FUN_00119910
    void exit() override;            // FUN_00119d60
    void update() override;          // FUN_00119f40
private:
    ui::Sprite* tiles_[4] = {};      // +0x04..+0x10 ([x*2 + y])
    SceneName pending_ = 0;          // +0x18 (0 = none)
    int mcState_ = 0;                // +0x20: 1 check started, 3 port check running, 5 card lost
};

class NameEntrScene : public Scene { // 0x51b5d0
public:
    void init() override;            // FUN_001693b0
    void exit() override;            // 0x169430 (thunk of FUN_00163fc0)
    void update() override;          // FUN_00169440
private:
    void buildTiles();               // FUN_00169770
    void freeTiles();                // FUN_00169a20
    ui::Sprite* tiles_[4] = {};      // +0x04..+0x10
    int leaving_ = 0;                // +0x14
    int frames_ = 0;                 // +0x18
};

class GameLoadScene : public Scene { // 0x53ea00
public:
    void init() override;            // FUN_00200db0
    void exit() override;            // 0x200e30 (thunk of FUN_00163fc0)
    void update() override;          // FUN_00200e40
private:
    void buildTiles();               // FUN_00201380
    void freeTiles();                // FUN_00201610
    void cancelCheck();              // FUN_002012a0
    void leaveCheck();               // FUN_00200ec0
    ui::Sprite* tiles_[4] = {};      // +0x04..+0x10
    int leaving_ = 0;                // +0x14
    int frames_ = 0;                 // +0x18
};

// EvMovie 0x53ea68 (vtable 0x4cf300; FUN_00206260 init / FUN_00206290 exit / FUN_002062c0 update): plays movie
// DAT_004697f8 from the Option scene's EVENT LIST, then returns to Option (which opens menu mode DAT_00469800).
class EvMovieScene : public Scene {  // 0x53ea68
public:
    void init() override;            // FUN_00206260
    void exit() override;            // FUN_00206290
    void update() override;          // FUN_002062c0
private:
    void leave();                    // tail of FUN_002062c0 after FUN_002137b0
    int frames_ = 0;                 // +0x04
    bool playing_ = false;           // port: the blocking FUN_00213840 spread over host frames
};

}  // namespace baroque::game
