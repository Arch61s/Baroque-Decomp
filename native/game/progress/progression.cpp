#include "game/progress/progression.hpp"
#include "game/progress/save_record.hpp"
#include "game/flag_manager.hpp"
#include "game/dungeon.hpp"
#include "platform/platform.hpp"
#include <cstring>
namespace baroque::game {
namespace {
#include "game/progress/flag_tables.inc"
ProgressHooks gHooks;
RunEndState gRunEnd;
RunRecord gRecord;

inline void put32(uint8_t* p, int32_t v) { std::memcpy(p, &v, 4); }
inline int32_t get32(const uint8_t* p) { int32_t v; std::memcpy(&v, p, 4); return v; }
inline uint16_t get16(const uint8_t* p) { uint16_t v; std::memcpy(&v, p, 2); return v; }
void missing(const char* what) { platform::log(std::string("progress: hook not installed: ") + what); }
int playerStat(int offset) { return gHooks.playerStat != nullptr ? gHooks.playerStat(offset) : 0; }
bool playerExists() { return gHooks.playerExists != nullptr && gHooks.playerExists(); }
void requestScene(const char* name) {
    if (gHooks.requestScene != nullptr) gHooks.requestScene(name);
    else missing("requestScene (FUN_00206550)");
}
void triggerEvent(int n) {
    if (gHooks.triggerEvent != nullptr) gHooks.triggerEvent(n);
    else missing("triggerEvent (FUN_001d1d30)");
}
}
ProgressHooks& progressHooks() { return gHooks; }
RunEndState& runEndState() { return gRunEnd; }
RunRecord& runRecord() { return gRecord; }

// orig FUN_0011c5b0 (work/decompiled/0011c5b0.c)
void runEnd(char kind, char flagB, int floor) {
    (void)floor;
    gRunEnd.kind = kind;
    gRunEnd.flagB = flagB;
    gRunEnd.flag5 = flags().test(5) ? 1 : 0;
    // player status block +0x14 (level) > 0x31; the original dereferences the player unconditionally
    if (gRunEnd.kind == 0 && flags().test(0x4f6) && 0x31 < playerStat(0x14)) flags().setGuarded(0x44d);
}

// orig FUN_0011c640 (work/decompiled/0011c640.c)
void runEndAchievements() {
    FlagManager& f = flags();
    if (gRunEnd.kind == 1) {
        f.clearAchievement(0x44f);
        f.clearAchievement(0x45f);
        f.clearAchievement(0x45a);
        f.clearAchievement(0x467);
        if (f.test(0x4f6) && f.test(0x3ed) && f.test(0x1c)) f.setGuarded(0x465);
        if (f.test(0x6d)) f.setGuarded(0x474);
    } else {
        if (gRunEnd.flagB != 0 && f.test(0x4f6) && gRunEnd.flag5 == 1 && f.test(0x68) && f.test(0x6f))
            f.setGuarded(0x478);
        if (f.test(0x4f6) && gRunEnd.flag5 == 1) {
            if (counters().values[4] == 0) f.setGuarded(0x457);   // iRam0053c5d0 = counter 4
            if (f.test(0x330)) f.setGuarded(0x468);
            if (f.test(0x71)) f.setGuarded(0x477);
            if (f.test(0x69)) f.setGuarded(0x479);
        }
    }
    for (int i = 0; i < 12; ++i)                                   // table 0x48ca50 {achievement, flag}
        if (f.test(static_cast<uint32_t>(kHubAchievementPairs[i * 2 + 1]))) f.setGuarded(kHubAchievementPairs[i * 2]);
    if (f.test(0x4f6) && f.test(0x3be) && f.test(0x3bf) && f.test(0x3c2) && f.test(0x3c4)) f.setGuarded(0x453);
    if (counters().atMax(0x1a)) f.setGuarded(0x454);               // FUN_00179810(0x53c5c0, 0x1a)
    if (f.test(0x4dd)) f.setGuarded(0x455);
    if (f.test(0x47)) f.setGuarded(0x456);
    if (f.test(0x32c)) f.setGuarded(0x458);
    bool all = true;
    for (uint32_t id = 0x267; id < 0x289; ++id)
        if (!f.test(id)) { all = false; break; }
    if (all) f.setGuarded(0x461);
    if (f.test(0x68e) && f.test(0x64f) && f.test(0x660) &&
        (f.test(0x6fe) || f.test(0x702) || f.test(0x714) || f.test(0x72a) || f.test(0x72b)))
        f.setGuarded(0x47a);
    if (f.test(0x33)) f.setGuarded(0x47b);
    if (f.test(0xbc4) && f.test(0xbcb) && (f.test(0x881) || f.test(0xbd4))) f.setGuarded(0x47e);
    if (f.test(0x4fc) || f.test(0x50b)) f.setGuarded(0x47f);
    f.setGuarded(0x482);
    f.set(0x4c3);
}

// orig FUN_0011cc30 (work/decompiled/0011cc30.c; the FUN_00179570 delta is a2 = 1, disasm 0x11ccc8)
void nameAchievementUpdate() {
    FlagManager& f = flags();
    const int n = f.nameAchievement();                             // FUN_001792c0
    f.clearNameFlags();                                            // FUN_00179250
    if (n == 0) {
        f.set(0x4e6);
    } else {
        f.clear(0x4e6);
        f.set(static_cast<uint32_t>(n));
        f.set(static_cast<uint32_t>(n + 0x40));
    }
    bool all = true;
    counters().values[0x12] = 0;                                   // uRam0053c608
    for (uint32_t id = 0x483; id < 0x4c3; ++id) {
        if (f.test(id)) counters().add(0x12, 1);
        else all = false;
    }
    if (all) f.set(0x4c4);
}

// orig FUN_001f2760 (work/decompiled/001f2760.c)
void recordRun(const GameProgress& g) {
    if (!playerExists()) return;
    const FlagManager& f = flags();
    gRecord.stat48 = playerStat(0x48);
    gRecord.stat4c = playerStat(0x4c);
    gRecord.floorNumber = floorNumber(g.floor);                    // FUN_0011c3b0(0x50a2a0)
    if (f.test(0x397)) gRecord.area = 3;
    else if (g.floor == 0x5b) gRecord.area = 0;
    else if (isRandomDungeon(g.floor)) gRecord.area = 1;           // FUN_0011c290 (a1 = Game+8, disasm 0x1f27d8)
    else if (g.floor == 0x5c) gRecord.area = 10;
    else if (isScenarioFloor(g.floor)) gRecord.area = 2;           // FUN_0011c2c0
    else if (f.test(0x398)) gRecord.area = 4;
    else if (f.test(0x399)) gRecord.area = 5;
    else if (f.test(0x39a)) gRecord.area = 6;
    else if (f.test(0x39b)) gRecord.area = 7;
    else if (f.test(0x39c)) gRecord.area = 8;
    else if (f.test(0x39d)) gRecord.area = 9;
    else gRecord.area = 0;
    if (f.test(0x4f1)) gRecord.difficulty = 0;
    else if (f.test(0x4f2)) gRecord.difficulty = 1;
    else if (f.test(0x4f3)) gRecord.difficulty = 2;
    else if (f.test(0x4f4)) gRecord.difficulty = 3;
    gRecord.hour = g.hour;
    gRecord.min = g.min;
    gRecord.sec = g.sec;
}

// orig FUN_001f2b00 (work/decompiled/001f2b00.c): rows ordered by +0x18 (desc, signed), +0x1c (desc, unsigned),
// +0x28 mode (1 beats 0), play time hour*3600 + min*60 + sec (asc); an empty row (+0 == 0) ends the table.
int rankingFind(const uint8_t* block, const uint8_t* e) {
    const int32_t e18 = get32(e + 0x18);
    const uint32_t e1c = static_cast<uint32_t>(get32(e + 0x1c));
    const int32_t emode = get32(e + 0x28);
    const long etime = static_cast<long>(get16(e + 0x26)) * 0xe10 + static_cast<long>(e[0x25]) * 0x3c + e[0x24];
    for (int i = 0; i < 0x32; ++i) {
        const uint8_t* r = block + i * 0x34;
        if (get32(r) == 0) return i;
        if (get32(r + 0x18) < e18) return i;
        if (e18 == get32(r + 0x18)) {
            if (static_cast<uint32_t>(get32(r + 0x1c)) < e1c) return i;
            if (e1c == static_cast<uint32_t>(get32(r + 0x1c))) {
                if (get32(r + 0x28) == 0 && emode == 1) return i;
                const long rtime = static_cast<long>(get16(r + 0x26)) * 0xe10 + static_cast<long>(r[0x25]) * 0x3c + r[0x24];
                if (emode == get32(r + 0x28) && etime < rtime) return i;
            }
        }
    }
    return -1;
}
// orig FUN_001f2c40 (work/decompiled/001f2c40.c): shift rows down from the insertion point (renumbering non-empty
// rows), then store the entry with rank = index + 1. Bytes +0x31..+0x33 of a row are never copied.
void rankingInsert(uint8_t* block, uint8_t* e) {
    const int at = rankingFind(block, e);
    if (at == -1) return;
    for (int i = 0x31; at < i; --i) {
        uint8_t* d = block + i * 0x34;
        const uint8_t* s = d - 0x34;
        std::memcpy(d, s, 0x30);                  // rank, name, +0x14..+0x2c (word/half/byte copies in the original)
        d[0x30] = s[0x30];
        if (get32(d) != 0) put32(d, i + 1);
    }
    put32(e, at + 1);
    uint8_t* d = block + at * 0x34;
    std::memcpy(d, e, 0x30);
    d[0x30] = e[0x30];
}

// orig FUN_001f2a00 (work/decompiled/001f2a00.c): ranking entry (0x34 bytes, the layout of 0x53deb0 rows):
// +0 rank, +4 name[16], +0x14 stat48, +0x18 stat4c, +0x1c floor number, +0x20 area, +0x24 sec, +0x25 min,
// +0x26 hour, +0x28 mode, +0x2c name index (FUN_001792c0 - 0x443), +0x30 difficulty byte.
long rankingSubmit(int mode) {
    uint8_t e[0x34] = {};   // UNVERIFIED: +0..+3 and the padding are uninitialised stack in the original
    const char* name = gHooks.playerName != nullptr ? gHooks.playerName() : "";
    std::memcpy(e + 4, name, strnlen(name, 15));                   // strcpy(auStack_3c, 0x51b5f8)
    put32(e + 0x14, gRecord.stat48);
    put32(e + 0x18, gRecord.stat4c);
    put32(e + 0x1c, gRecord.floorNumber);
    put32(e + 0x20, gRecord.area);
    e[0x24] = gRecord.sec;
    e[0x25] = gRecord.min;
    std::memcpy(e + 0x26, &gRecord.hour, 2);
    put32(e + 0x28, mode);
    e[0x30] = static_cast<uint8_t>(gRecord.difficulty);
    uint8_t* block = gHooks.rankingBlock != nullptr ? gHooks.rankingBlock() : nullptr;
    if (block == nullptr) { missing("rankingBlock (0x53deb0)"); return -1; }
    const int at = rankingFind(block, e);                          // FUN_001f2b00
    if (at == 0 && flags().test(0x1e)) flags().setGuarded(0x460);  // new first place after the game was cleared
    put32(e + 0x2c, flags().nameAchievement() - 0x443);
    rankingInsert(block, e);                                       // FUN_001f2c40
    return at;
}

// orig FUN_0011bfe0 (work/decompiled/0011bfe0.c)
void requestFloor(GameProgress& g, int floor, int reenter) {
    g.requestedFloor = floor;
    if (reenter == 1) requestScene("Game....");                    // s_Game_____00449f80
}

// orig FUN_0011cd40 (work/decompiled/0011cd40.c)
void returnToHub(GameProgress& g) {
    if (gHooks.postRunScript != nullptr) gHooks.postRunScript(kStagePT99);   // FUN_00179f60("PT99") + FUN_0017b640
    else missing("postRunScript (FUN_00179f60 / FUN_0017b640)");
    counters().add(1, 1);                                          // FUN_00179570(0x53c5c0, 1, 1)
    g.flags4 |= 1;
    g.sec = 0;
    g.min = 0;
    g.hour = 0;
    runEndAchievements();                                          // FUN_0011c640
    rankingSubmit(g.counterResetMask == 0x80 ? 0 : 1);             // FUN_001f2a00
    nameAchievementUpdate();                                       // FUN_0011cc30
    if (gHooks.itemWorldRunEnd != nullptr) gHooks.itemWorldRunEnd();   // FUN_001d98b0 + FUN_001d5e60 (0x53d910)
    else missing("itemWorldRunEnd (FUN_001d98b0 / FUN_001d5e60)");
    flags().clearAchievement(0x44e);                               // FUN_001793b0(0x44e)
    flags().runReset(g.flagResetMask);                             // FUN_00178b30(+0x1c)
    counters().runReset(g.counterResetMask);                       // FUN_00179cb0(+0x20)
    g.flagResetMask = 0;
    g.counterResetMask = 0;
}

// orig FUN_0011d120 (work/decompiled/0011d120.c; FUN_0011bfe0 third argument 1, disasm 0x11d260)
void onPlayerDeath(GameProgress& g) {
    runEnd(0, 1, g.floor);
    recordRun(g);                                                  // FUN_001f2760
    g.flagResetMask = 1;
    g.counterResetMask = 1;
    flags().clearRunFlags();                                       // FUN_001791f0
    if (flags().test(0x1e)) requestScene("Ending..");
    else if (flags().test(0x19)) triggerEvent(0xf);
    else if (flags().test(0x16)) triggerEvent(0xb);
    else if (flags().test(5)) requestFloor(g, 0x60, 1);
    else returnToHub(g);
}

// orig FUN_0011ce60 (work/decompiled/0011ce60.c)
void onStairsExit(GameProgress& g) {
    runEnd(1, 0, g.floor);
    if (flags().test(5)) flags().set(0x56);
    if (g.floor == 0x5b) g.flagResetMask = 2;
    else if (isRandomDungeon(g.floor)) g.flagResetMask = 4;        // FUN_0011c290 (a1 = Game+8, disasm 0x11cea8)
    else if (isScenarioFloor(g.floor)) g.flagResetMask = 8;        // FUN_0011c2c0
    else if (isSecondDungeon(g.floor)) g.flagResetMask = 0x10;     // FUN_0011c390
    g.flagResetMask |= 0x81;
    g.counterResetMask = 0x80;
    recordRun(g);                                                  // FUN_001f2760
    if (flags().test(0x21)) {
        if (gHooks.revivePlayer != nullptr) gHooks.revivePlayer(); // FUN_0011a8a0
        else missing("revivePlayer (FUN_0011a8a0)");
        requestFloor(g, 0x5f, 1);
    } else {
        returnToHub(g);
    }
}

// orig FUN_0011cfa0 (work/decompiled/0011cfa0.c)
void onDungeonCleared(GameProgress& g) {
    runEnd(0, 0, g.floor);
    recordRun(g);
    g.flagResetMask = 1;
    g.counterResetMask = 1;
    flags().clearRunFlags();
    FlagManager& f = flags();
    if (f.test(0x1c)) {
        triggerEvent(10);
    } else if (f.test(0x16)) {
        f.set(0x1a);
        f.set(0x5e);
        triggerEvent(10);
    } else if (f.test(5)) {
        if (f.test(0x58)) {
            f.set(0x15);
            f.set(0x5d);
            triggerEvent(10);
        } else {
            triggerEvent(0x53);
        }
    } else {
        f.set(2);
        f.set(0xc0c);
        f.set(0xc1d);
        f.set(0xc1e);
        requestFloor(g, 0x5f, 1);
    }
}

// orig FUN_0011a9f0 (work/decompiled/0011a9f0.c), the branch between the scene test and LAB_0011ae0c
void gameInitProgress(GameProgress& g, bool prevSceneGame, bool prevSceneEnding, int newGameFlag, int loadSlot,
                      const uint8_t* saveFile) {
    g.prevFloor = g.floor;
    g.floor = g.requestedFloor;
    g.requestedFloor = -1;
    if (prevSceneGame) {
        if ((g.flags4 & 1) != 0) {                                 // back in the hub after a run (rebirth)
            if (gHooks.revivePlayer != nullptr) gHooks.revivePlayer();          // FUN_0011a8a0
            else missing("revivePlayer (FUN_0011a8a0)");
            if (gHooks.playerHiddenStats != nullptr) gHooks.playerHiddenStats(); // FUN_001395d0
            if (gHooks.playerSetAnim != nullptr) gHooks.playerSetAnim("M005");   // 0x449fa8, FUN_0010a7f0
            flags().setGuarded(0x480);
        }
    } else if (prevSceneEnding) {
        g.prevFloor = -1;
        g.floor = 0x5b;
        if (gHooks.playerStatusNew != nullptr) gHooks.playerStatusNew();         // FUN_00133a60
        if (gHooks.playerHiddenStats != nullptr) gHooks.playerHiddenStats();     // FUN_001395d0
        flags().setGuarded(0x480);
    } else if (newGameFlag == 1) {
        uint32_t diff = 0;
        for (uint32_t id = 0x4f1; id < 0x4f5; ++id)
            if (flags().test(id)) { diff = id; break; }
        if (loadSlot == -1) {
            flags().newGameWipe();                                 // FUN_00178ad0
            counters().clearAll();                                 // FUN_00179540
        } else {
            if (saveFile != nullptr) saveDatabaseKeep(saveFile, loadSlot);   // FUN_00116c90(0x5093d0, slot)
            flags().set(0x58);
            flags().set(0xc0b);
        }
        flags().set(diff);                                         // flag 0 when no difficulty flag is set
        if (gHooks.playerStatusNew != nullptr) gHooks.playerStatusNew();
        if (gHooks.playerHiddenStats != nullptr) gHooks.playerHiddenStats();
    } else if (saveFile != nullptr) {
        ContinueResult c;
        if (saveContinue(saveFile, loadSlot, &c)) {                // FUN_00116a50(0x5093d0, slot, +8, +0xc, +0x24..)
            g.floor = c.floor;
            g.prevFloor = c.prevFloor;
            g.sec = c.sec;
            g.min = c.min;
            g.hour = c.hour;
        }
    }
    g.flags4 &= ~1u;                                               // LAB_0011ae0c
    if (gHooks.devAfterInit != nullptr) gHooks.devAfterInit();     // PORT: pending dev flags (main.cpp --flag)
}
}
