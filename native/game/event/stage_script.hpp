#pragma once
// Stage-script objects 0x53c690[] (count 0x53c6a0): exit / run-end objects named by a 4-char command ("PT00",
// "PT10".."PT14", "PT99"), created by FUN_00179f60 (ctor FUN_0017c5f0 -> FUN_0017a180, vtable 0x4cddf0, init
// FUN_00128690(obj, 5, cmd) -> vt+0x1c FUN_0017a2a0), ticked by FUN_0017a050 (vt+0xc FUN_0017a4d0 = call the
// pointer-to-member state at +0x28), freed by FUN_00179fe0. The end of every flow is the command handler
// FUN_0017a510 (floor request / "Title..."). EVENTS_EFFECTS_PROGRESSION.md 2.5 and 3.4, EVENTS_NATIVE.md.
#include <cstdint>

namespace baroque::game {

// pointer-to-member states (PTMF tables 0x44d7f8.., 0x44d898.., 0x44d8e8..)
enum class StageState : int {
    None = 0,          // uRam0055fdc8.. (null PTMF)
    TowerGate,         // FUN_0017ba10 (PT10)
    Exit,              // FUN_0017aad0 (PT00 / PT11 / PT12 / default)
    Warp,              // FUN_0017bb30 (PT13)
    Return,            // FUN_0017bc40 (PT14)
    WarpGlow,          // FUN_0017c0c0 (0x44d938): PT13 / PT14 warp chain, 10 ticks
    WarpFlash,         // FUN_0017c1e0 (0x44d958): 0x14 ticks
    WarpCamera,        // FUN_0017c310 (0x44d978): 0x1e ticks
    WarpFade,          // FUN_0017c3b0 (0x44d998): until the fade ends
    ExitConfirm,       // FUN_0017b020
    StairsMenu,        // FUN_0017b450
    StairsSaveWait,    // FUN_0017b550
    StairsTitle,       // FUN_0017b5c0
    RunMenu,           // FUN_0017b730
    RunSaveWait,       // FUN_0017b840
    RunTitle,          // FUN_0017b8b0
    FadeOut,           // FUN_0017b9b0
};

struct StageObject {
    int type = 5;                 // +0x18
    uint32_t cmd = 0;             // +0x1c 4-char command
    float pos[3] = {};            // *(+0x20) +0xc (transform; the placement position)
    uint32_t flags = 0;           // +0x24 bit0 player inside, 0x40000000 resume, 0x80000000 to title
    StageState state = StageState::None;   // +0x28..+0x30
    int effectA = -1;             // +0x34 (EXI 0x9a ring)
    int effectB = -1;             // +0x38 (EXI 0x9b)
    void* linked = nullptr;       // +0x3c
    int counter = 0;              // +0x44
};

StageObject* stageCreate(const char* cmd);       // orig FUN_00179f60
void stageTickAll();                             // orig FUN_0017a050
void stageClearAll();                            // orig FUN_00179fe0
void stageRunEnd(StageObject& o);                // orig FUN_0017b640
void stageDescendMenu(StageObject& o);           // orig FUN_0017b350 (player Event state PT99 branches)
void stageCommand(StageObject& o);               // orig FUN_0017a510
int stageCount();
int stageWarpPause();                            // uRam0053c6b0 (1 while the PT13 / PT14 warp chain runs)

// Calls the flows make into the Game scene (installed by game_scene.cpp).
struct StageHooks {
    void (*hideFloor)() = nullptr;               // FUN_0011ed90(0x50a350): floor scene root flags 1 | 2 cleared
    void (*requestFloor)(int floor) = nullptr;   // FUN_0011bfe0(0x50a2a0, floor, 1)
    int (*currentFloor)() = nullptr;             // iRam0050a2a8
};
StageHooks& stageHooks();

}  // namespace baroque::game
