#pragma once
// Memory-card data file BASLUS-21714_DATA (0x1f76c bytes, RAM image 0x4e9c50) and its slot record
// (RUNTIME.md 5, work/reports/PROGRESS_NATIVE.md 4). The port stores exactly this file; every byte is placed where
// the original writer FUN_001165f0 / the card library FUN_0020ba10 put it.
//
// File: +0x00 u32 checksum (byte sum of +4..end, FUN_0020bab0; 0 = empty, mismatch = corrupt -> -1 written back by
//       FUN_0020ba60), +0x04 u32 magic 0x07102200 (file entry 0x449df0 +0xc), +0x08 u8[6] RTC of the last write
//       (sec,min,hour,day,month,year binary, FUN_0020e860), +0x0e..0x0f zero, +0x10 Slot[3] (stride 0xa7c8),
//       +0x1f768 s32 "game cleared" (flag 0x2a1 at save time, FUN_00117360).
#include <cstdint>
#include <cstddef>
namespace baroque::game {
constexpr int kSaveSlotSize = 0xa7c8;
constexpr int kSaveFileSize = 0x1f76c;
constexpr int kSaveSlotBase = 0x10;
constexpr int kSaveClearedOffset = 0x1f768;

// Slot record offsets (relative to the slot start).
namespace slot {
constexpr int kOptions = 0x0;         // u8[0x3c]  option block 0x51af30
constexpr int kRanking = 0x3c;        // u8[0xa28] ranking block 0x53deb0 (FUN_001f2720 restores)
constexpr int kName = 0xa64;          // char[16]  player name 0x51b5f8 (strcpy)
constexpr int kFlags = 0xa74;         // u32[0x7d] flag manager 0x53c3b0
constexpr int kCounters = 0xc68;      // i32[0x33] counters 0x53c5c0 (+0xc6c = counter 1, shown as the cycle count)
constexpr int kDatabase = 0xd34;      // 50 x 0x198 item records, database 0x53dc00 (FUN_001db370 / FUN_001db3f0)
constexpr int kDbStage = 0x5ce4;      // s32 FUN_001dacd0() at save time
constexpr int kDbCounter = 0x5ce8;    // s32 0x53dcc8
constexpr int kInventoryA = 0x5cec;   // 3 x 0x198 (0x53d7d0 list, FUN_001b58a0 / FUN_001b5920)
constexpr int kOuterBoxes = 0x61b4;   // 20 x 0x198 (0x53d910 +0xbc list, FUN_001daad0 / FUN_001dab50)
constexpr int kDungeonId = 0x8194;    // s32 FUN_0011c480(Game)
constexpr int kFloorId = 0x8198;      // s32 Game +8 (0x50a2a8)
constexpr int kItemRecords = 0x819c;  // 20 x 0x198 (FUN_001da6e0 / FUN_001da8d0 param 2)
constexpr int kItemWords = 0xa17c;    // u32[20] (param 3)
constexpr int kItemEquip = 0xa1cc;    // u32[4] (param 4)
constexpr int kItemBackup = 0xa1dc;   // param 5 (0x11 + 0x11 + ... words; see the items module)
constexpr int kPlayer = 0xa374;       // u8[0x448] player status (FUN_00139750 / FUN_00139b40)
constexpr int kPlayerWord1 = 0xa378;  // tested by FUN_00116a50 (0 -> start in the hub as a new character)
constexpr int kHasChar = 0xa458;      // read by FUN_00116d30 (0 = empty slot); 1 when saved without a player
constexpr int kPlayerSize = 0x448;
constexpr int kClockSec = 0xa7bc;     // u8  Game +0x24
constexpr int kClockMin = 0xa7bd;     // u8  Game +0x25
constexpr int kClockHour = 0xa7be;    // u16 Game +0x26
constexpr int kTimestamp = 0xa7c0;    // u8[6] RTC (FUN_0020e860) + 2 pad
constexpr int kItemRecordSize = 0x198;
}

// Data sources / sinks the slot writer and readers need from other modules. Null hook = not backed yet: the writer
// leaves those slot bytes as they are (so a loaded record round-trips) and the reader skips the restore.
struct SaveHooks {
    // options 0x51af30 / ranking 0x53deb0 / name 0x51b5f8 (installed by save_wiring.cpp in Baroque-Native)
    void (*writeOptions)(uint8_t* dst0x3c) = nullptr;
    void (*readOptions)(const uint8_t* src0x3c) = nullptr;
    void (*writeRanking)(uint8_t* dst0xa28) = nullptr;
    void (*readRanking)(const uint8_t* src0xa28) = nullptr;      // FUN_001f2720 (incl. its validity check)
    void (*writeName)(char* dst16) = nullptr;                    // strcpy(dst, 0x51b5f8)
    void (*readName)(const char* src16) = nullptr;               // strcpy(0x51b5f8, src)
    // item database 0x53dc00, inventories, outer-world boxes (items module)
    void (*writeDatabase)(uint8_t* dst) = nullptr;               // FUN_001db370
    void (*readDatabase)(const uint8_t* src) = nullptr;          // FUN_001db3f0
    int (*databaseCounter)() = nullptr;                          // 0x53dcc8
    void (*writeInventoryA)(uint8_t* dst) = nullptr;             // FUN_001b58a0
    void (*readInventoryA)(const uint8_t* src) = nullptr;        // FUN_001b5920
    void (*writeOuterBoxes)(uint8_t* dst) = nullptr;             // FUN_001daad0(0x53d910)
    void (*readOuterBoxes)(const uint8_t* src) = nullptr;        // FUN_001dab50(0x53d910)
    void (*writeItemSlots)(uint8_t* slotBase) = nullptr;         // FUN_001da6e0(0x53d910, +0x819c, +0xa17c, +0xa1cc, +0xa1dc)
    void (*readItemSlots)(const uint8_t* slotBase) = nullptr;    // FUN_001da8d0(0x53d910, same offsets)
    // Game scene 0x50a2a0 and the player (0x50b144)
    int (*gameFloor)() = nullptr;                                // Game +8 (0x50a2a8)
    void (*gameClock)(uint8_t* sec, uint8_t* min, uint16_t* hour) = nullptr;   // Game +0x24/+0x25/+0x26
    bool (*playerExists)() = nullptr;                            // iRam0050b144 != 0
    int (*playerBlockWord4)() = nullptr;                         // status block (status object +0xc) +4; UNVERIFIED meaning (0 -> hub reset)
    void (*playerStatusNew)() = nullptr;                         // FUN_00133a60(status)
    void (*playerBlockWord4Clear)() = nullptr;                   // *(status block + 4) = 0 after FUN_00133a60
    void (*playerHiddenStats)() = nullptr;                       // FUN_001395d0(status)
    void (*writePlayer)(uint8_t* dst0x448) = nullptr;            // FUN_00139750
    void (*readPlayer)(const uint8_t* src0x448) = nullptr;       // FUN_00139b40
    void (*rtc)(uint8_t out6[6]) = nullptr;                      // FUN_0020e860 (sec,min,hour,day,month,year)
    int newGameFlag = 0;                                         // DAT_00469700 (1 = NAME ENTRY new game; FUN_00116870 keeps the typed name)
};
SaveHooks& saveHooks();

// FUN_0020bab0: byte sum of file[4 .. size)
uint32_t saveChecksum(const uint8_t* file, size_t size);
// FUN_0020ba10: magic at +4, RTC at +8, checksum at +0 (called by the card library before writing)
void saveSealHeader(uint8_t* file, size_t size);
// FUN_0020ba60: 0 empty (sum 0), 1 valid, -1 corrupt (and *(int*)file = -1)
int saveVerify(uint8_t* file, size_t size);
// status test at the top of FUN_00116a50 / FUN_00116c90: *(int*)file 0 -> 0, -1 -> -1, else 1
int saveFileStatus(const uint8_t* file);

// FUN_001165f0: write the current game into `slotRec` (0xa7c8 bytes)
void saveWriteSlot(uint8_t* slotRec);
// FUN_00116870: restore the "system" part of a slot (options, ranking, name unless new game, flags, counters)
void saveRestoreSystem(const uint8_t* slotRec);

struct ContinueResult {
    int floor = 0;          // -> Game +8
    int prevFloor = 0;      // -> Game +0xc
    uint8_t sec = 0, min = 0;
    uint16_t hour = 0;      // -> Game +0x24..+0x27
};
// FUN_00116a50: continue a saved game (Game init, DAT_00469700 != 1): restores database, inventories, item slots,
// boxes, clock and the player; returns false when the file status is not valid.
bool saveContinue(const uint8_t* file, int slotIndex, ContinueResult* out);
// FUN_00116c90: "database keep" new game from a slot: system restore, then keep only the database flags/counters
// (tables 0x496230 / 0x496ee8) and the item database.
bool saveDatabaseKeep(const uint8_t* file, int slotIndex);

// FUN_0011c480(Game): dungeon id stored at slot +0x8194 for the current floor
int saveDungeonId(int floor);
// FUN_0011a710: floor redirect through tables 0x48c7e0 / 0x48c8d0
int floorRedirect(int floor);
}
