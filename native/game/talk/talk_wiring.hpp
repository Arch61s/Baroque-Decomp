#pragma once
// Game binding of the talk manager 0x53c6e0 (Baroque-Native only): TalkWorld over the port's objects and the entry
// points other modules call (see work/reports/TALK_NATIVE.md "hook points").
namespace baroque::game {

struct Item;

// Installs the game TalkWorld, constructs the manager (FUN_001a1b70, static ctor FUN_004c7ed0) and chains the talk
// task FUN_001a1dc0(0x53c6e0) right after the event tick in the engine tick (FUN_00101bd0 calls FUN_0016d3b0 then
// FUN_001a1dc0): wraps Engine::eventFrame, so it must run after registerGameScene() installed that hook.
void talkInstallGame();

// Item-side notifications (orig FUN_001a9cc0 / FUN_001a6230 / FUN_001a5330) for callers holding an Item*.
void talkItemTaken(Item* item);                   // FUN_001a9cc0(0x53c6e0, item)
void talkItemHandOver(Item* item, bool accepted); // FUN_001a6230(0x53c6e0, item, accepted)
void talkDatabasePut(Item* item);                 // FUN_001a5330(0x53c6e0, item) from FUN_001dad80

// PORT dev option `--talk SPEAKER` (talk_dev.cpp): call with main's argv once; on the first Game tick whose floor has
// an NPC with that id, the player is placed in front of it so the approach selector starts its talk.
void talkDevOptions(int argc, char** argv);
void talkDevTick();                               // once per engine tick (main loop, after scenes().step())

// PORT test driver `--autoplay FROM` (autoplay_dev.cpp; options read by talkDevOptions, ticked from the talk hook):
// scripted pad input for the continuous playthrough. main.cpp captures the queued milestone frames
// (autoplayNextShot, nullptr = none) and ends the --shot run when autoplayFinished().
void autoplayOptions(int argc, char** argv);
void autoplayTick();
const char* autoplayNextShot();
bool autoplayFinished();

}  // namespace baroque::game
