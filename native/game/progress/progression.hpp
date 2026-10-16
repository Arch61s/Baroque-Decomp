#pragma once
// Run progression: death / stairs exit / dungeon cleared / return to the hub, the run-end achievements and the
// ranking record, and the save branch of the Game scene init (EVENTS_EFFECTS_PROGRESSION.md 3.3-3.4,
// work/reports/PROGRESS_NATIVE.md 3). What a death keeps or resets is decided by the masks written to Game +0x1c
// (flags, FUN_00178b30) and +0x20 (counters, FUN_00179cb0), applied when the run returns to the hub (FUN_0011cd40).
#include <cstdint>
namespace baroque::game {

// Game scene object 0x50a2a0 fields used by these functions (the scene owner mirrors them in/out).
struct GameProgress {
    uint32_t flags4 = 0;            // +0x04 bit0: the run ended, Game re-entered (FUN_0011cd40 sets, init clears)
    int floor = 0;                  // +0x08
    int prevFloor = 0;              // +0x0c
    int requestedFloor = -1;        // +0x10 (FUN_0011bfe0)
    uint32_t flagResetMask = 0;     // +0x1c -> FUN_00178b30
    uint32_t counterResetMask = 0;  // +0x20 -> FUN_00179cb0 (0x80 = left by the stairs)
    uint8_t sec = 0;                // +0x24 play clock
    uint8_t min = 0;                // +0x25
    uint16_t hour = 0;              // +0x26
};

// Calls into modules that are not ported / owned here. Null = not backed yet (logged once per call site).
struct ProgressHooks {
    bool (*playerExists)() = nullptr;                    // iRam0050b144 != 0
    int (*playerStat)(int offset) = nullptr;             // i32 at the player's status block (+0xc of the status obj) + offset
    void (*playerStatusNew)() = nullptr;                 // FUN_00133a60(status)
    void (*playerHiddenStats)() = nullptr;               // FUN_001395d0(status)
    void (*playerSetAnim)(const char* name) = nullptr;   // FUN_0010a7f0(player model, sequence named `name`)
    void (*revivePlayer)() = nullptr;                    // FUN_0011a8a0 (player module)
    const char* (*playerName)() = nullptr;               // 0x51b5f8
    void (*requestScene)(const char* name) = nullptr;    // FUN_00206550(0x53ea80, name) ("Game....", "Ending..")
    void (*triggerEvent)(int n) = nullptr;               // FUN_001d1d30(n) -> event "E%02d"
    void (*postRunScript)(const char* cmd) = nullptr;    // FUN_00179f60(cmd) + FUN_0017b640(stage obj)
    uint8_t* (*rankingBlock)() = nullptr;                // 0x53deb0 (50 x 0x34 bytes, game/ui/database.hpp)
    void (*itemWorldRunEnd)() = nullptr;                 // FUN_001d98b0(0x53d910) + FUN_001d5e60(0x53d910)
    // PORT (dev options, not in the original): called once at the end of gameInitProgress (after the new-game wipe
    // FUN_00178ad0 / continue), so main.cpp's pending `--flag N` reach the floor's story-event checks (FUN_001229d0).
    void (*devAfterInit)() = nullptr;
};
ProgressHooks& progressHooks();

// Globals 0x4e5dd0 / 0x4e5dd4 / 0x4e5dd8 written by FUN_0011c5b0 and read by FUN_0011c640.
struct RunEndState {
    char kind = 0;      // 0x4e5dd0: 0 death / clear, 1 stairs
    char flagB = 0;     // 0x4e5dd4: 1 on death
    char flag5 = 0;     // 0x4e5dd8: flag 5 at that time
};
RunEndState& runEndState();

// Pending ranking record 0x53e8e8.. (FUN_001f2760 writes, FUN_001f2a00 reads).
struct RunRecord {
    int32_t stat48 = 0;      // 0x53e8e8 player status block +0x48
    int32_t stat4c = 0;      // 0x53e8f0 player status block +0x4c
    int32_t floorNumber = 0; // 0x53e8f8 FUN_0011c3b0(Game)
    int32_t area = 0;        // 0x53e900 0 hub/none, 1 tower, 2 scenario, 3 tutorial 0x397, 4..9 tutorial 0x398..0x39d, 10 floor 0x5c
    uint8_t sec = 0, min = 0;   // 0x53e908 / 0x53e909
    uint16_t hour = 0;          // 0x53e90a
    int32_t difficulty = 0;  // 0x53e910 index of 0x4f1..0x4f4 (unchanged when none is set)
};
RunRecord& runRecord();

void runEnd(char kind, char flagB, int floor);            // FUN_0011c5b0
void runEndAchievements();                                // FUN_0011c640
void nameAchievementUpdate();                             // FUN_0011cc30
void recordRun(const GameProgress& g);                    // FUN_001f2760
long rankingSubmit(int mode);                             // FUN_001f2a00 (mode 0 = stairs, 1 = otherwise)
int rankingFind(const uint8_t* block, const uint8_t* entry);   // FUN_001f2b00: insertion index, -1 = not ranked
void rankingInsert(uint8_t* block, uint8_t* entry);       // FUN_001f2c40 (also writes the rank into entry +0)
void requestFloor(GameProgress& g, int floor, int reenter);   // FUN_0011bfe0
void onPlayerDeath(GameProgress& g);                      // FUN_0011d120
void onStairsExit(GameProgress& g);                       // FUN_0011ce60
void onDungeonCleared(GameProgress& g);                   // FUN_0011cfa0
void returnToHub(GameProgress& g);                        // FUN_0011cd40

// Save / new-game / re-entry branch of the Game scene init FUN_0011a9f0 (between the scene-change test and the
// floor setup). prevSceneGame: the previous scene was "Game...." (re-entry); prevSceneEnding: it was "Ending..".
// The player creation and FUN_0011b300 stay with the scene owner (they run before this on the non-re-entry path).
void gameInitProgress(GameProgress& g, bool prevSceneGame, bool prevSceneEnding, int newGameFlag, int loadSlot,
                      const uint8_t* saveFile);
}
