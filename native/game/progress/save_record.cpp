#include "game/progress/save_record.hpp"
#include "game/progress/talk_select.hpp"
#include "game/flag_manager.hpp"
#include "game/dungeon.hpp"
#include <cstring>
namespace baroque::game {
namespace {
#include "game/progress/flag_tables.inc"
SaveHooks gHooks;

inline void put32(uint8_t* p, int32_t v) { std::memcpy(p, &v, 4); }
inline int32_t get32(const uint8_t* p) { int32_t v; std::memcpy(&v, p, 4); return v; }
}
SaveHooks& saveHooks() { return gHooks; }

// orig FUN_0020bab0 (work/decompiled/0020bab0.c): sum of the bytes after the checksum word
uint32_t saveChecksum(const uint8_t* file, size_t size) {
    uint32_t sum = 0;
    if (size != 4)
        for (size_t i = 4; i < size; ++i) sum += file[i];
    return sum;
}
// orig FUN_0020ba10 (work/decompiled/0020ba10.c)
void saveSealHeader(uint8_t* file, size_t size) {
    put32(file + 4, static_cast<int32_t>(kSaveMagic));
    uint8_t t[6] = {};
    if (gHooks.rtc != nullptr) gHooks.rtc(t);                      // FUN_0020e860(file + 8)
    std::memcpy(file + 8, t, 6);
    put32(file, static_cast<int32_t>(saveChecksum(file, size)));
}
// orig FUN_0020ba60 (work/decompiled/0020ba60.c)
int saveVerify(uint8_t* file, size_t size) {
    const uint32_t sum = saveChecksum(file, size);
    if (sum == 0) return 0;
    if (static_cast<int32_t>(sum) == get32(file)) return 1;
    put32(file, -1);
    return -1;
}
// inline test at the top of FUN_00116a50 / FUN_00116c90
int saveFileStatus(const uint8_t* file) {
    const int32_t v = get32(file);
    if (v == 0) return 0;
    return v == -1 ? -1 : 1;
}

// orig FUN_0011a710 (work/decompiled/0011a710.c)
int floorRedirect(int floor) {
    if (floor == 0x8d) return 99;
    const int32_t* t = kFloorRedirect;
    int n = 0x13;
    const FlagManager& f = flags();
    if (f.test(0x1e)) {
        if (floor == 0x8c) {
            if (!f.test(0x3bd)) return 0x5b;
            if (!f.test(0x2c9)) return 99;
            if (f.test(0x3c3) && !f.test(0x3c5)) return 0x8c;
            return 99;
        }
        n = 0xb;
        t = kFloorRedirectCleared;
    }
    for (int i = 0; i < n; ++i) {
        if (floor == t[i * 3 + 1]) {
            if (f.test(static_cast<uint32_t>(t[i * 3]))) return t[i * 3 + 2];
            if (t[i * 3 + 2] != 99) return floor;
        }
    }
    return floor;
}
// orig FUN_0011c480 (work/decompiled/0011c480.c)
int saveDungeonId(int floor) {
    if (99u < static_cast<uint32_t>(floor)) {
        if (usesPt14Stairs(floor)) return 0x5b;                          // FUN_0011c1d0
        if (!isFinalFloor(floor)) {                                      // FUN_0011c210
            const int n = floorNumber(floor);                            // FUN_0011c3b0
            if (isScenarioA(floor)) return n + 0x12d;                    // FUN_0011c330
            if (isScenarioB(floor)) return n + 0x137;                    // FUN_0011c350
            if (isScenarioC(floor)) return n + 0x141;                    // FUN_0011c370
            if (!isSecondDungeon(floor)) return floorRedirect(n + 0x65); // FUN_0011c390 / FUN_0011a710
            return n + 400;
        }
    }
    return 0x5b;
}

// orig FUN_001165f0 (work/decompiled/001165f0.c)
void saveWriteSlot(uint8_t* s) {
    if (gHooks.writeOptions != nullptr) gHooks.writeOptions(s + slot::kOptions);          // memcpy(+0, 0x51af30, 0x3c)
    if (gHooks.writeRanking != nullptr) gHooks.writeRanking(s + slot::kRanking);          // memcpy(+0x3c, 0x53deb0, 0xa28)
    if (gHooks.writeName != nullptr) gHooks.writeName(reinterpret_cast<char*>(s + slot::kName));   // strcpy(+0xa64, 0x51b5f8)
    {
        uint32_t w[FlagManager::kWords];
        flags().saveTo(w);                                                                // FUN_001793c0
        std::memcpy(s + slot::kFlags, w, sizeof w);
    }
    {
        int32_t c[Counters::kCount];
        counters().saveTo(c);                                                             // FUN_00179e60
        std::memcpy(s + slot::kCounters, c, sizeof c);
    }
    if (gHooks.writeDatabase != nullptr) gHooks.writeDatabase(s + slot::kDatabase);       // FUN_001db370
    put32(s + slot::kDbStage, databaseStage(flags()));                                    // FUN_001dacd0
    if (gHooks.databaseCounter != nullptr) put32(s + slot::kDbCounter, gHooks.databaseCounter());   // uRam0053dcc8
    if (gHooks.writeInventoryA != nullptr) gHooks.writeInventoryA(s + slot::kInventoryA); // FUN_001b58a0
    if (gHooks.writeOuterBoxes != nullptr) gHooks.writeOuterBoxes(s + slot::kOuterBoxes); // FUN_001daad0
    if (gHooks.writeItemSlots != nullptr) gHooks.writeItemSlots(s);                       // FUN_001da6e0
    const int floor = gHooks.gameFloor != nullptr ? gHooks.gameFloor() : get32(s + slot::kFloorId);
    put32(s + slot::kDungeonId, saveDungeonId(floor));                                    // FUN_0011c480(0x50a2a0)
    put32(s + slot::kFloorId, floor);                                                     // uRam0050a2a8
    // iRam0050b144: no player accessor installed is treated as "no player actor" (the original's own branch)
    const bool havePlayer = gHooks.playerExists != nullptr && gHooks.playerExists();
    if (!havePlayer) {
        std::memset(s + slot::kPlayer, 0, slot::kPlayerSize);
        put32(s + slot::kHasChar, 1);
    } else {
        const int32_t fl = get32(s + slot::kFloorId);
        bool hub = isRandomDungeon(fl) || fl == 0x60 || fl == 0x5f;                      // FUN_0011c290
        bool reset;
        if (hub) reset = get32(s + slot::kDungeonId) == 0x5b;
        else reset = false;
        if (!reset) {
            const int hp = gHooks.playerBlockWord4 != nullptr ? gHooks.playerBlockWord4() : 1;
            if (hp == 0) reset = true;                                                    // LAB_00116790
        }
        if (reset) {                                                                      // LAB_001167a8
            put32(s + slot::kDungeonId, 0x5b);
            if (gHooks.playerStatusNew != nullptr) gHooks.playerStatusNew();              // FUN_00133a60
            if (gHooks.playerBlockWord4Clear != nullptr) gHooks.playerBlockWord4Clear();   // status block +4 = 0
        }
        if (gHooks.writePlayer != nullptr) gHooks.writePlayer(s + slot::kPlayer);         // FUN_00139750
    }
    uint8_t sec = s[slot::kClockSec], mn = s[slot::kClockMin];
    uint16_t hour;
    std::memcpy(&hour, s + slot::kClockHour, 2);
    if (gHooks.gameClock != nullptr) gHooks.gameClock(&sec, &mn, &hour);
    s[slot::kClockSec] = sec;                                                             // uRam0050a2c4
    s[slot::kClockMin] = mn;                                                              // uRam0050a2c5
    std::memcpy(s + slot::kClockHour, &hour, 2);                                          // uRam0050a2c6
    uint8_t t[6] = {};
    if (gHooks.rtc != nullptr) { gHooks.rtc(t); std::memcpy(s + slot::kTimestamp, t, 6); }   // FUN_0020e860(+0xa7c0)
}

// orig FUN_00116870 (work/decompiled/00116870.c)
void saveRestoreSystem(const uint8_t* s) {
    if (gHooks.readOptions != nullptr) gHooks.readOptions(s + slot::kOptions);
    if (gHooks.readRanking != nullptr) gHooks.readRanking(s + slot::kRanking);           // FUN_001f2720
    if (gHooks.newGameFlag != 1 && gHooks.readName != nullptr)
        gHooks.readName(reinterpret_cast<const char*>(s + slot::kName));
    uint32_t w[FlagManager::kWords];
    std::memcpy(w, s + slot::kFlags, sizeof w);
    flags().loadFrom(w);                                                                  // FUN_001793f0
    int32_t c[Counters::kCount];
    std::memcpy(c, s + slot::kCounters, sizeof c);
    counters().loadFrom(c);                                                               // FUN_00179e90
}

// orig FUN_00116a50 (work/decompiled/00116a50.c)
bool saveContinue(const uint8_t* file, int slotIndex, ContinueResult* out) {
    if (saveFileStatus(file) != 1) return false;
    const uint8_t* s = file + kSaveSlotBase + static_cast<size_t>(slotIndex) * kSaveSlotSize;
    if (gHooks.readDatabase != nullptr) gHooks.readDatabase(s + slot::kDatabase);         // FUN_001db3f0
    if (gHooks.readInventoryA != nullptr) gHooks.readInventoryA(s + slot::kInventoryA);   // FUN_001b5920
    if (gHooks.readItemSlots != nullptr) gHooks.readItemSlots(s);                         // FUN_001da8d0
    if (gHooks.readOuterBoxes != nullptr) gHooks.readOuterBoxes(s + slot::kOuterBoxes);   // FUN_001dab50
    out->sec = s[slot::kClockSec];
    out->min = s[slot::kClockMin];
    std::memcpy(&out->hour, s + slot::kClockHour, 2);
    const int32_t fl = get32(s + slot::kFloorId);
    const bool hubFloor = isRandomDungeon(fl) || fl == 0x60 || fl == 0x5f;               // FUN_0011c290
    if ((hubFloor && get32(s + slot::kDungeonId) == 0x5b) || get32(s + slot::kPlayerWord1) == 0) {
        out->prevFloor = -1;
        out->floor = 0x5b;
        if (gHooks.playerStatusNew != nullptr) gHooks.playerStatusNew();                  // FUN_00133a60
        if (gHooks.playerHiddenStats != nullptr) gHooks.playerHiddenStats();              // FUN_001395d0
        flags().setGuarded(0x480);                                                        // FUN_00179320(0x480)
    } else {
        out->prevFloor = fl;
        out->floor = get32(s + slot::kDungeonId);
        if (gHooks.readPlayer != nullptr) gHooks.readPlayer(s + slot::kPlayer);           // FUN_00139b40
    }
    return true;
}

// orig FUN_00116c90 (work/decompiled/00116c90.c)
bool saveDatabaseKeep(const uint8_t* file, int slotIndex) {
    const int st = saveFileStatus(file);
    if (st == 1) {
        const uint8_t* s = file + kSaveSlotBase + static_cast<size_t>(slotIndex) * kSaveSlotSize;
        saveRestoreSystem(s);                                                             // FUN_00116870
        flags().keepDatabaseOnly();                                                       // FUN_00179460
        counters().keepDatabaseOnly();                                                    // FUN_00179f00
        if (gHooks.readDatabase != nullptr) gHooks.readDatabase(s + slot::kDatabase);     // FUN_001db3f0
    }
    return st == 1;
}
}
