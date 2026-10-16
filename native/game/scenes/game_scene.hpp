#pragma once
// Game scene 0x50a2a0 (SCENES.md §9): Init FUN_0011a9f0, Update FUN_0011bec0, Exit FUN_0011bad0, ctor FUN_0011a940.
// Tasks: the game task 0x50a270 (inserted by FUN_0011b300; per-tick FUN_0011d850 updates every character) and the
// camera task (FUN_0011d7e0 -> FUN_0013e060, update FUN_0010da30), in that order in the engine task list.
// Translated: entry bookkeeping, player creation, new-game status init, per-floor flags, dungeon/event floor setup
// FUN_0011b810, floor load FUN_0011f960 (game::Dungeon + FloorWorld), BGM selection FUN_0011d3f0/FUN_0011d390,
// play clock. Not yet: town/outer-world floors 0x5b (FUN_0011b3e0) and 0x60 (FUN_0011b620), save loading, HUD,
// weather/inventory 0x53d910, events.
#include "game/scene_manager.hpp"
#include "game/progress/progression.hpp"

namespace baroque::game {

struct GameEntry {               // values handed to the Game scene by other scenes (FUN_0011bfe0 / Title / NameEntr)
    int requestedFloor = -1;     // Game +0x10
    int flag = 0;
};
GameEntry& gameEntry();
void gameRequestFloor(int floor, int flag);   // orig FUN_0011bfe0(Game, floor, flag): +0x10 = floor, scene "Game...."

void registerGameScene();                     // replaces the "Game...." placeholder
GameProgress& gameProgress();                 // the progression fields of the Game object 0x50a2a0
// Presentation of the 3-D pass (FUN_001075a0 subset) for the host; false when the Game scene is not active.
bool gameRender3D(int screenW, int screenH, float alpha);
bool gameActive();                                  // Game scene between Init and Exit
void gameWorldToggle(int on);                       // orig FUN_001b8430: "Toggle" switches child 1 (on) / child 0
void gameMouseLook(float dxDeg, float dyDeg);       // port-only free mouse look (Camera::addMouseDelta)

}  // namespace baroque::game
