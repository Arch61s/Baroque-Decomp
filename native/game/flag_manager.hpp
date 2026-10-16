#pragma once
// Story/event flag manager 0x53c3b0 and counter block 0x53c5c0 (EVENTS_EFFECTS_PROGRESSION.md 3.1/3.2,
// work/reports/PROGRESS_NATIVE.md).
// Flags: 0x7d u32 words = 4000 bit flags (FUN_00178760 clears 0x78+5 words; FUN_001793c0/FUN_001793f0 copy 0x7d words
// to/from the save slot +0xa74). Bit = id >> 5 / 1 << (id & 0x1f) (FUN_001786c0).
// Counters: 0x33 i32 (FUN_00179540 clears 0x33, FUN_00179e60/FUN_00179e90 copy 0x33 words to/from slot +0xc68);
// only ids 0..0x1b have a definition in table 0x496b50 (FUN_00179570).
#include <cstdint>
namespace baroque::game {

// Accessors the flag / counter side effects need from other modules. Null = not backed yet (see the .cpp for the
// fallback of each). Installed by the Game scene owner (hook points in PROGRESS_NATIVE.md).
struct FlagHooks {
    int (*floorId)() = nullptr;                   // Game scene +8 (0x50a2a8); null -> -1 (no floor)
    void (*playerStatusBit)(uint32_t bit) = nullptr;  // FUN_00138850(player status object, bit) from FUN_00178890
};
FlagHooks& flagHooks();

struct FlagManager {
    static constexpr int kWords = 0x7d;      // 4000 bits (0x53c3b0 .. 0x53c5a4)
    static constexpr uint32_t kCount = 4000;
    uint32_t words[kWords] = {};
    void setRaw(uint32_t id);                // FUN_00178840
    void set(uint32_t id);                   // FUN_00178890 (with side effects)
    bool test(uint32_t id) const;            // FUN_00178a80
    void clear(uint32_t id);                 // FUN_00178a30
    void clearAll();                         // FUN_00178760
    void setGuarded(int id);                 // FUN_00179320: achievement n -> flag n + 0xaf9 unless guarded
    void clearAchievement(int id);           // FUN_001793b0: clear flag n + 0xaf9
    void newGameWipe();                      // FUN_00178ad0
    void saveTo(uint32_t* dst) const;        // FUN_001793c0 (0x7d words)
    void loadFrom(const uint32_t* src);      // FUN_001793f0 (0x7d words)
    void keepDatabaseOnly();                 // FUN_00179460: clear every flag < 4000 not in table 0x496230
    void clearRunFlags();                    // FUN_001791f0: clear table 0x4961e0 {91,92,93,100,94,95}
    void clearNameFlags();                   // FUN_00179250: clear 0x443..0x482 and 0xf3c..0xf7b
    int nameAchievement() const;             // FUN_001792c0: first set flag of 0xf3c..0xf7b minus 0xaf9, else 0
    void runReset(uint32_t mask);            // FUN_00178b30 (new-run reset by Game +0x1c mask)
};
bool isDatabaseKeepFlag(uint32_t id);        // FUN_00179420 (table 0x496230, 0x489 entries)
bool talkTutorialGate();                     // FUN_001a6420: not floor 0x5b and any of 0x2f5..0x2f7 / 0x397..0x39d

struct Counters {
    static constexpr int kCount = 0x33;      // saved words
    static constexpr int kDefined = 28;      // entries of table 0x496b50
    // padded to 0x40 so existing callers' `id < 0x40` guards stay in bounds; only 0..0x32 exist in the original
    int values[0x40] = {};
    void add(int id, int delta);             // FUN_00179570 (type gate, clamp to max, flagOnMax, side effects)
    void clearAll();                         // FUN_00179540
    bool atMax(int id) const;                // FUN_00179810
    void runReset(uint32_t mask);            // FUN_00179cb0 (new-run reset by Game +0x20 mask)
    void saveTo(int32_t* dst) const;         // FUN_00179e60 (0x33 words)
    void loadFrom(const int32_t* src);       // FUN_00179e90 (0x33 words)
    void keepDatabaseOnly();                 // FUN_00179f00: zero every counter not in table 0x496ee8 {16,17,18}
};
bool isDatabaseKeepCounter(int id);          // FUN_00179ec0

// Hook for the actor-registry scan in FUN_00179cb0 (mask 0x10081): number of `O**A` objects and whether one of them
// is unidentified (+0x2c & 1 == 0). Null -> no objects (achievement 0x45a untouched).
struct CounterResetHooks {
    void (*scanObjects)(int* count, bool* anyUnidentified) = nullptr;
};
CounterResetHooks& counterResetHooks();

FlagManager& flags();
Counters& counters();
}
