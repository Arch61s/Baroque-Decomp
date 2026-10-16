#include "game/flag_manager.hpp"
#include "platform/platform.hpp"
#include <cstdint>
namespace baroque::game {
namespace {
#include "game/progress/flag_tables.inc"
FlagManager gFlags;
Counters gCounters;
FlagHooks gFlagHooks;
CounterResetHooks gCounterResetHooks;

int currentFloor() { return gFlagHooks.floorId != nullptr ? gFlagHooks.floorId() : -1; }
// orig FUN_0011c290 (work/decompiled/0011c290.c): 99 < id < 300 || id == 99
bool floorIsRandomDungeon(int id) { const auto u = static_cast<uint32_t>(id); return (99 < u && u < 300) || u == 99; }
// orig FUN_0011c0a0 (work/decompiled/0011c0a0.c): not town (0x5b/0x5c, FUN_0011c110), not event (0x5d..0x60,
// FUN_0011c150), not 99 (FUN_0011c100)
bool floorIsGenerated(int id) {
    if (id == 0x5b || id == 0x5c) return false;
    if (id == 0x5d || id == 0x5e || id == 0x5f || id == 0x60) return false;
    return id != 99;
}
}
FlagManager& flags() { return gFlags; }
Counters& counters() { return gCounters; }
FlagHooks& flagHooks() { return gFlagHooks; }
CounterResetHooks& counterResetHooks() { return gCounterResetHooks; }

// orig FUN_00178840 (work/decompiled/00178840.c). UNVERIFIED: ids >= 4000 would write past the 0x7d words into
// the gap before the counter block in the original; they are ignored here (no caller uses one).
void FlagManager::setRaw(uint32_t id) {
    const uint32_t w = id >> 5;
    if (w < static_cast<uint32_t>(kWords)) words[w] |= (1u << (id & 0x1f));
}
// orig FUN_00178a80 (work/decompiled/00178a80.c)
bool FlagManager::test(uint32_t id) const {
    const uint32_t w = id >> 5;
    return w < static_cast<uint32_t>(kWords) && (words[w] & (1u << (id & 0x1f))) != 0;
}
// orig FUN_00178a30 (work/decompiled/00178a30.c)
void FlagManager::clear(uint32_t id) {
    const uint32_t w = id >> 5;
    if (w < static_cast<uint32_t>(kWords)) words[w] &= ~(1u << (id & 0x1f));
}
// orig FUN_00178760 (work/decompiled/00178760.c): 0x78 words in the unrolled loop + 5 = 0x7d
void FlagManager::clearAll() { for (uint32_t& w : words) w = 0; }

// orig FUN_00178890 (work/decompiled/00178890.c)
void FlagManager::set(uint32_t id) {
    setRaw(id);                                                   // FUN_00178840
    if (id == 0x810 || id == 0x80f || id == 0x80d) {
        const uint32_t bit = id == 0x810 ? 0x10u : (id == 0x80f ? 0x20u : 4u);
        if (gFlagHooks.playerStatusBit != nullptr) gFlagHooks.playerStatusBit(bit);   // FUN_00138850(player, bit)
        else {
            static bool logged = false;   // UNVERIFIED until the player hook is installed: status bit not applied
            if (!logged) platform::log("flags: FUN_00138850 player status hook not installed");
            logged = true;
        }
    } else if ((id == 0x3c6 || id == 6) && test(6) && test(0x3c6)) {
        set(0x3c7);
    }
    if (0xa54 < id && id < 0xb83) gCounters.add(0x11, 1);         // FUN_00179570(0x53c5c0, 0x11, 1)
    if (test(5) && test(0x4f6) && test(0x32d) && test(0x2f8) && test(0x32e)) setGuarded(0x45d);
}
// orig FUN_00179320 (work/decompiled/00179320.c): ids of table 0x496200 whose (id + 0x40) flag is set are skipped,
// otherwise the flag (id + 0xaf9) is set raw.
void FlagManager::setGuarded(int id) {
    for (int i = 0; i < 10; ++i)
        if (id == kGuardedAchievements[i] && test(static_cast<uint32_t>(kGuardedAchievements[i] + 0x40))) return;
    setRaw(static_cast<uint32_t>(id + 0xaf9));
}
// orig FUN_001793b0 (work/decompiled/001793b0.c)
void FlagManager::clearAchievement(int id) { clear(static_cast<uint32_t>(id + 0xaf9)); }
// orig FUN_00178ad0 (work/decompiled/00178ad0.c)
void FlagManager::newGameWipe() {
    clearAll();
    for (int32_t f : kNewGameFlags) set(static_cast<uint32_t>(f));
}
// orig FUN_001793c0 / FUN_001793f0
void FlagManager::saveTo(uint32_t* dst) const { for (int i = 0; i < kWords; ++i) dst[i] = words[i]; }
void FlagManager::loadFrom(const uint32_t* src) { for (int i = 0; i < kWords; ++i) words[i] = src[i]; }

// orig FUN_00179420 (work/decompiled/00179420.c)
bool isDatabaseKeepFlag(uint32_t id) {
    for (uint16_t k : kDatabaseKeepFlags)
        if (id == k) return true;
    return false;
}
// orig FUN_00179460 (work/decompiled/00179460.c)
void FlagManager::keepDatabaseOnly() {
    for (uint32_t id = 0; id < 4000; ++id)
        if (!isDatabaseKeepFlag(id)) clear(id);
}
// orig FUN_001791f0 (work/decompiled/001791f0.c)
void FlagManager::clearRunFlags() { for (int32_t f : kDeathClearFlags) clear(static_cast<uint32_t>(f)); }
// orig FUN_00179250 (work/decompiled/00179250.c)
void FlagManager::clearNameFlags() {
    for (uint32_t f = 0x443; f < 0x483; ++f) clear(f);
    for (uint32_t f = 0xf3c; f < 0xf7c; ++f) clear(f);
}
// orig FUN_001792c0 (work/decompiled/001792c0.c)
int FlagManager::nameAchievement() const {
    for (uint32_t f = 0xf3c; f < 0xf7c; ++f)
        if (test(f)) return static_cast<int>(f) - 0xaf9;
    return 0;
}

// orig FUN_00178b30 (work/decompiled/00178b30.c; the lost argument of the 0x495b40 test is the table word, disasm
// 0x178e30..0x178e3c)
void FlagManager::runReset(uint32_t mask) {
    const bool t397 = test(0x397);
    const bool t398 = test(0x398);
    const bool t399 = test(0x399);
    const bool t39a = test(0x39a);
    const bool t39b = test(0x39b);
    const bool t39c = test(0x39c);
    const bool t39d = test(0x39d);
    if ((mask & 1) != 0) {
        clear(0x407);
        if (test(0x724)) set(0x407);
        clear(0x408);
        if (test(0x86b)) set(0x408);
        if (test(7) || test(0x3c7)) set(8);
        if (!test(10) && test(0xef)) {
            set(0xc);
        } else if (!test(10) && test(0xb) && test(0xee)) {
            set(0xef);
        } else if (!test(10) && test(0xb)) {
            set(0xee);
        }
        if (test(0xd)) set(0xe);
        if (test(0xd2)) set(0x28a);
        else if (test(0xd1)) set(0x289);
        if (test(0xf4)) set(0xf5);
        if (test(0xfd)) set(0xfe);
        for (int32_t f : kRunResetA) clear(static_cast<uint32_t>(f));
        for (int i = 0; i < 9; ++i) {
            const int32_t t = kRunResetPairs[i * 2];
            if (t == 0 || test(static_cast<uint32_t>(t))) {
                set(static_cast<uint32_t>(kRunResetPairs[i * 2 + 1]));
                break;
            }
        }
        if (test(0x1e)) {
            set(0x4e7); set(0x4e8); set(0x4e9); set(0x4ea); set(0x4eb); set(0x4ec); set(0x4f5);
        }
        for (int32_t f : kRunResetB) clear(static_cast<uint32_t>(f));
        for (uint32_t f = 0xe74; f < 0xeb1; ++f) clear(f);
    }
    if ((mask & 0x10001) != 0) for (int32_t f : kRunReset10001) clear(static_cast<uint32_t>(f));
    if ((mask & 0x7001) != 0) for (int32_t f : kRunReset7001) clear(static_cast<uint32_t>(f));
    if ((mask & 0x3001) != 0) for (int32_t f : kRunReset3001) clear(static_cast<uint32_t>(f));
    if ((mask & 0x5001) != 0) for (int32_t f : kRunReset5001) clear(static_cast<uint32_t>(f));
    if ((mask & 0x80) != 0) clearRunFlags();                      // FUN_001791f0
    if ((mask & 2) != 0 && test(0x56)) set(0x2b);
    if ((mask & 8) != 0) set(0x30);
    if ((mask & 0x10) != 0) {
        if (t397) set(0x3a);
        else if (!t398 && !t399 && !t39a && !t39b) {
            if (t39c) set(0x3f3);
            else if (t39d) set(0x3f6);
        }
    }
    if ((mask & 0x7002) != 0) for (int32_t f : kRunReset7002) clear(static_cast<uint32_t>(f));
    if ((mask & 0x5002) != 0) for (int32_t f : kRunReset5002) clear(static_cast<uint32_t>(f));
    if ((mask & 0x100a) != 0) clear(0x59);
    if ((mask & 0x5000) != 0) clear(0x3ee);
    if ((mask & 0x7000) != 0) { clear(799); clear(800); }
}

// orig FUN_001a6420 (work/decompiled/001a6420.c)
bool talkTutorialGate() {
    if (currentFloor() == 0x5b) return false;
    const FlagManager& f = gFlags;
    if (f.test(0x2f5)) return true;
    if (!f.test(0x2f6) && !f.test(0x2f7) && !f.test(0x397) && !f.test(0x398) && !f.test(0x399) && !f.test(0x39a) &&
        !f.test(0x39b) && !f.test(0x39c) && !f.test(0x39d))
        return false;
    return true;
}

namespace {
// Counter side effects called from FUN_00179570 (each receives the counter block).
// orig FUN_00179850 (work/decompiled/00179850.c)
void ctrSide1(const Counters& c) {
    for (int i = 0; i < 5; ++i)
        if (c.values[1] == kCtr1Flags[i * 2]) { gFlags.set(static_cast<uint32_t>(kCtr1Flags[i * 2 + 1])); return; }
}
// orig FUN_001798c0 (work/decompiled/001798c0.c)
void ctrSide2(const Counters& c) { if (!gFlags.test(5) && c.values[2] == 100) gFlags.set(0xe0); }
// orig FUN_00179920 (work/decompiled/00179920.c)
void ctrSide4() { if (gFlags.test(5)) gFlags.set(0x3d5); }
// orig FUN_00179960 (work/decompiled/00179960.c)
void ctrSide6(const Counters& c) { if (2u < static_cast<uint32_t>(c.values[6])) gFlags.setGuarded(0x47d); }
// orig FUN_001799a0 (work/decompiled/001799a0.c)
void ctrSide16(const Counters& c) {
    const uint32_t v = static_cast<uint32_t>(c.values[0x10]);
    for (int i = 0; i < 15; ++i)
        if (static_cast<int32_t>(v) == kCtr16Exact[i * 2]) gFlags.set(static_cast<uint32_t>(kCtr16Exact[i * 2 + 1]));
    for (int i = 0; i < 20; ++i) gFlags.clear(static_cast<uint32_t>(kCtr16Range[i * 3 + 2]));
    for (int i = 0; i < 20; ++i)
        if (static_cast<uint32_t>(kCtr16Range[i * 3]) <= v && v <= static_cast<uint32_t>(kCtr16Range[i * 3 + 1]))
            gFlags.set(static_cast<uint32_t>(kCtr16Range[i * 3 + 2]));
    for (int i = 0; i < 8; ++i) gFlags.clear(static_cast<uint32_t>(kCtr16Pairs[i * 2 + 1]));
    for (int i = 0; i < 8; ++i)
        if (gFlags.test(static_cast<uint32_t>(kCtr16Pairs[i * 2]))) {
            gFlags.set(static_cast<uint32_t>(kCtr16Pairs[i * 2 + 1]));
            return;
        }
}
// orig FUN_00179b20 (work/decompiled/00179b20.c)
void ctrSide17(const Counters& c) {
    for (int i = 0; i < 13; ++i)
        if (c.values[0x11] == kCtr17Flags[i * 2]) { gFlags.set(static_cast<uint32_t>(kCtr17Flags[i * 2 + 1])); return; }
}
// orig FUN_00179b90 (work/decompiled/00179b90.c)
void ctrSide19(const Counters& c) { if (gFlags.test(5) && c.values[0x13] == 5) gFlags.set(0x3ee); }
// orig FUN_00179bf0 (work/decompiled/00179bf0.c)
void ctrSide20(const Counters& c) { if (c.values[0x14] == 0x14) gFlags.set(0x2ec); }
// orig FUN_00179c30 (work/decompiled/00179c30.c)
void ctrSide21(const Counters& c) { if (c.values[0x15] == 4) gFlags.set(0x126); }
// orig FUN_00179c70 (work/decompiled/00179c70.c) -> FUN_001a9be0 (work/decompiled/001a9be0.c)
void ctrSide22(const Counters& c) {
    if (static_cast<uint32_t>(c.values[0x16]) < 9) gFlags.set(static_cast<uint32_t>(c.values[0x16]) + 0x315);
    if (talkTutorialGate()) gFlags.set(0xe51);                    // FUN_001a9be0(0x53c6e0)
}
}

// orig FUN_00179570 (work/decompiled/00179570.c; the FUN_00179320 argument is the table's flagOnMax, disasm
// 0x1796b8..0x1796c8)
void Counters::add(int id, int delta) {
    if (id < 0 || id >= kDefined) {
        platform::log("counters: id outside table 0x496b50 ignored");   // UNVERIFIED: original reads past the table
        return;
    }
    const CounterDef& d = kCounterDefs[id];
    const int fl = currentFloor();
    const bool gate = d.type == 0 || (d.type == 1 && gFlags.test(5)) || (d.type == 2 && floorIsRandomDungeon(fl)) ||
                      (d.type == 3 && floorIsGenerated(fl)) ||
                      (d.type == 4 && gFlags.test(5) && floorIsRandomDungeon(fl));
    if (!gate) return;
    const uint32_t old = static_cast<uint32_t>(values[id]);
    const uint32_t now = old + static_cast<uint32_t>(delta);
    values[id] = static_cast<int>(now);
    if (d.max <= now) {
        values[id] = static_cast<int>(d.max);
        if (d.flagOnMax != 0) gFlags.setGuarded(d.flagOnMax);
    }
    switch (id) {
    case 0x16: ctrSide22(*this); break;
    case 0x15: ctrSide21(*this); break;
    case 0x14: ctrSide20(*this); break;
    case 0x13: ctrSide19(*this); break;
    case 0x11: ctrSide17(*this); break;
    case 0x10: ctrSide16(*this); break;
    case 6: ctrSide6(*this); break;
    case 4: ctrSide4(); break;
    case 2: ctrSide2(*this); break;
    case 1: ctrSide1(*this); break;
    default: break;
    }
}
// orig FUN_00179540 (work/decompiled/00179540.c)
void Counters::clearAll() { for (int i = 0; i < kCount; ++i) values[i] = 0; }
// orig FUN_00179810 (work/decompiled/00179810.c)
bool Counters::atMax(int id) const {
    if (id < 0 || id >= kDefined) return false;
    return static_cast<uint32_t>(values[id]) == kCounterDefs[id].max;
}
// orig FUN_00179cb0 (work/decompiled/00179cb0.c)
void Counters::runReset(uint32_t mask) {
    if ((mask & 0x7000) != 0) {
        values[2] = 0; values[3] = 0; values[4] = 0; values[5] = 0;
        values[10] = 0; values[11] = 0; values[12] = 0; values[13] = 0;
        values[15] = 0; values[20] = 0; values[23] = 0; values[27] = 0;
    }
    if ((mask & 0x17000) != 0) values[6] = 0;
    if ((mask & 0x7080) != 0) { values[7] = 0; values[8] = 0; values[9] = 0; values[14] = 0; }
    if ((mask & 0x5000) != 0) values[19] = 0;
    if ((mask & 0x7081) != 0) values[21] = 0;
    if ((mask & 0x10081) != 0) {
        values[22] = 0;
        values[24] = 0;
        // FUN_0012c210/FUN_0012c220(0x50b130): any `O**A` object actor whose +0x2c bit0 is clear -> achievement
        // 0x45a lost
        int count = 0;
        bool anyUnidentified = false;
        if (gCounterResetHooks.scanObjects != nullptr) gCounterResetHooks.scanObjects(&count, &anyUnidentified);
        if (count != 0 && anyUnidentified) gFlags.clearAchievement(0x45a);
    }
    if ((mask & 0x81) != 0) values[25] = 0;
}
// orig FUN_00179e60 / FUN_00179e90
void Counters::saveTo(int32_t* dst) const { for (int i = 0; i < kCount; ++i) dst[i] = values[i]; }
void Counters::loadFrom(const int32_t* src) { for (int i = 0; i < kCount; ++i) values[i] = src[i]; }
// orig FUN_00179ec0 (work/decompiled/00179ec0.c)
bool isDatabaseKeepCounter(int id) {
    for (uint16_t k : kDatabaseKeepCounters)
        if (static_cast<uint32_t>(id) == k) return true;
    return false;
}
// orig FUN_00179f00 (work/decompiled/00179f00.c)
void Counters::keepDatabaseOnly() {
    for (int i = 0; i < kCount; ++i)
        if (!isDatabaseKeepCounter(i)) values[i] = 0;
}
}
