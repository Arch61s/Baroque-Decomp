#include "game/progress/talk_select.hpp"
#include "game/progress/talk_predicates.hpp"
#include "data/tlk.hpp"
#include <cstdio>
#include <cstring>
namespace baroque::game {
namespace {
#include "game/progress/talk_tables.inc"
#include "game/progress/flag_tables.inc"
TalkSelectHooks gHooks;

inline uint32_t lo8(uint32_t id) { return id & 0xff; }
inline uint32_t b1(uint32_t id) { return (id >> 8) & 0xff; }
// joined_r0x001a4030: second byte 'T'
inline int secondIsT(uint32_t id) { return b1(id) == 0x54 ? 1 : 0; }
// joined_r0x001a484c: "MT"
inline int isMT(uint32_t id) { return lo8(id) == 0x4d ? secondIsT(id) : 0; }
inline int isHT(uint32_t id) { return lo8(id) == 0x48 ? secondIsT(id) : 0; }
inline int is2(uint32_t id, uint32_t c0, uint32_t c1) { return (lo8(id) == c0 && b1(id) == c1) ? 1 : 0; }

// 4-char id -> C string the way FUN_0023edb8(buf, "%c%c%c%c", ...) writes it (a NUL byte ends the string early).
void idString(uint32_t id, char out[8]) {
    out[0] = static_cast<char>(id & 0xff);
    out[1] = static_cast<char>((id >> 8) & 0xff);
    out[2] = static_cast<char>((id >> 16) & 0xff);
    out[3] = static_cast<char>(id >> 24);
    out[4] = 0;
}
// atoi used by FUN_001a69e0 on the start string; the table strings are plain decimals (generator stored the value).
}
TalkSelectHooks& talkSelectHooks() { return gHooks; }

TalkCondEntry talkCondEntry(int idx) {
    if (idx < 0 || idx >= data::kTalkTableCount) return TalkCondEntry{0, 0, 0, 0};
    const TalkCond& c = kTalkCond[idx];
    return TalkCondEntry{c.kind, c.c1, c.c2, c.c3};
}

// orig FUN_001dacd0 (work/decompiled/001dacd0.c)
int databaseStage(const FlagManager& f) {
    for (int i = 0; i < 8; ++i)
        if (f.test(static_cast<uint32_t>(kDatabaseStage[i * 2]))) return kDatabaseStage[i * 2 + 1];
    return 5;
}

// orig FUN_001a3cc0 (work/decompiled/001a3cc0.c; label structure joined_r0x001a4030 / 0x001a484c / 0x001a4f40)
int talkItemMatch(const TalkSelectState& mgr, const TalkActorRef& item, long cat, const FlagManager& f) {
    const uint32_t id = item.id;
    const uint32_t f70 = item.flags70;
    switch (cat) {
    case 0: return id == kId_4622b8 ? 1 : 0;                                          // IS01
    case 1:
        if (id == kId_4622b8) return 0;
        if (lo8(id) == 0x49) return 0;
        if (b1(id) == 0x53) return 0;
        return 1;
    case 2:
        if ((f70 & 4) == 0) return 0;
        if (lo8(id) == 0x48 && b1(id) == 0x54) return 1;
        return isMT(id);
    case 3: return id == kId_462450 ? 1 : 0;                                          // ST06
    case 6: return id == kId_4622d0 ? 1 : 0;                                          // IS04
    case 7: return 1;
    case 8: return id == kId_4622a8 ? 1 : 0;                                          // GN00
    case 9: return id == kId_462458 ? 1 : 0;                                          // LQ22
    case 0xc:
        if (lo8(id) == 0x48 && b1(id) == 0x54) return 1;
        return isMT(id);
    case 0xd: return is2(id, 0x53, 0x44);                                             // SD
    case 0xe: return lo8(id) == 0x43 ? secondIsT(id) : 0;                             // CT
    case 0xf: return is2(id, 0x57, 0x47);                                             // WG
    case 0x10: return is2(id, 0x42, 0x58);                                            // BX
    case 0x11: return is2(id, 0x4c, 0x51);                                            // LQ
    case 0x12: return lo8(id) == 0x53 ? secondIsT(id) : 0;                            // ST
    case 0x13: return is2(id, 0x54, 0x4c);                                            // TL
    case 0x14: return is2(id, 0x54, 0x50);                                            // TP
    case 0x15: return is2(id, 0x42, 0x4e);                                            // BN
    case 0x16: return is2(id, 0x57, 0x4d);                                            // WM
    case 0x17: return is2(id, 0x49, 0x53);                                            // IS
    case 0x19: return id == kId_462460 ? 1 : 0;                                       // BN11
    case 0x1b: return id == kId_462468 ? 1 : 0;                                       // BN14
    case 0x1c: return id == kId_462470 ? 1 : 0;                                       // WM20
    case 0x1d: return id == kId_4622b0 ? 1 : 0;                                       // IS00
    case 0x1f: return id == kId_4622c0 ? 1 : 0;                                       // IS02
    case 0x20: return id == kId_4622c8 ? 1 : 0;                                       // IS03
    case 0x21: return id == kId_4622d8 ? 1 : 0;                                       // IS05
    case 0x22: return id == kId_4622e0 ? 1 : 0;                                       // IS06
    case 0x23:
        if (lo8(id) != 0x49) return 0;
        if (b1(id) != 0x53) return 0;
        if (id == kId_4622b0 || id == kId_4622b8 || id == kId_4622c0 || id == kId_4622c8 || id == kId_4622d0 ||
            id == kId_4622d8 || id == kId_4622e0 || id == kId_462478 || id == kId_462480 || id == kId_462488 ||
            id == kId_462490)
            return 0;                                                                  // IS00..IS10
        return 1;
    case 0x24: return id == kId_462498 ? 1 : 0;                                       // ST05
    case 0x25: if ((f70 & 4) != 0) return 0; return isMT(id);
    case 0x26: if ((f70 & 4) == 0) return 0; return isMT(id);
    case 0x27: if ((f70 & 4) != 0) return 0; return isHT(id);
    case 0x28: if ((f70 & 4) == 0) return 0; return isHT(id);
    case 0x29: return id == kId_4624a0 ? 1 : 0;                                       // WM21
    case 0x2b: return id == kId_4624a8 ? 1 : 0;                                       // LQ12
    case 0x2c: return id == kId_4624b0 ? 1 : 0;                                       // LQ21
    case 0x2d: return id == kId_4624b8 ? 1 : 0;                                       // HT00
    case 0x2e: return id == kId_4624c0 ? 1 : 0;                                       // HT01
    case 0x2f: return isMT(id);
    case 0x30: return isHT(id);
    case 0x32:
        if ((f70 & 4) != 0) return 1;
        if (item.f24 != 0 || -1 < item.f60) {
            if (item.f24 != 1) return 0;
            if (-1 < item.f64) return 0;
        }
        return 1;
    case 0x33: return id == kId_4624c8 ? 1 : 0;                                       // BN19
    case 0x34: return id == kId_4624d0 ? 1 : 0;                                       // BN20
    case 0x35:
        if (id != kId_4624d8 && lo8(id) == 0x48 && b1(id) == 0x54) return 1;          // HT** except HT02
        if (id == kId_4624e0) return 0;                                                // MT03
        return isMT(id);
    case 0x36: return id == kId_4624e8 ? 1 : 0;                                       // TL26
    case 0x37: return id == kId_4624f0 ? 1 : 0;                                       // LQ06
    case 0x38: {
        if (lo8(id) != 0x49) return 0;
        if (b1(id) != 0x53) return 0;
        bool ok = false;
        int found = -1;
        for (int i = 0; i < static_cast<int>(sizeof(kItemIdList) / sizeof(kItemIdList[0])); ++i)
            if (kItemIdList[i] == id) { found = i; break; }
        if (found >= 0) {
            ok = true;
            for (uint32_t fl = kItemIdFlagRange[found][0]; fl <= kItemIdFlagRange[found][1]; ++fl) {
                uint32_t t = fl;
                if (fl == 0xa5c) t = 0xa5e;
                if (t == 0xa66) t = 0xa68;
                if (!f.test(t)) { ok = false; break; }
            }
        }
        return ok ? 1 : 0;
    }
    case 0x39: return (f70 & 0x10000) != 0 ? 1 : 0;
    case 0x3a:
        if (lo8(id) != 0x49) return 0;
        if (b1(id) != 0x53) return 0;
        return f.test(0x4e0) ? 1 : 0;
    case 0x3b: {
        int sum = 0;
        bool low;
        if (lo8(id) == 0x53 && b1(id) == 0x44) {
            sum = item.f54 + item.f60;
            low = sum < 0x1e;
        } else if (lo8(id) != 0x43) {
            low = true;
        } else {
            if (b1(id) == 0x54) sum = item.f58 + item.f64;
            low = sum < 0x1e;
        }
        return low ? 0 : 1;
    }
    case 0x3c: return mgr.fc0 != 0 ? 1 : 0;
    case 0x3d: return id == kId_4624f8 ? 1 : 0;                                       // BX21
    case 0x3e:
    case 0x3f: {
        if (mgr.databaseStageSeen != (cat == 0x3e ? 0 : 1)) return 0;
        // FUN_001dad50(0x53dc00): database +200 == FUN_001dacd0()
        const int count = gHooks.databaseCount != nullptr ? gHooks.databaseCount() : 0;
        return count == databaseStage(f) ? 1 : 0;
    }
    case 0x5b:
        if (id == kId_4622b0) return 1;                                                // IS00
        if (id != kId_4622c0 && id != kId_4622c8 && id != kId_4622d8 && id != kId_4622e0) return 0;
        return 1;
    case 0x5c:
        if (id != kId_4622a8) return 0;                                                // GN00
        return mgr.fb0 != 0 ? 0 : 1;
    default:
        break;
    }
    if (0x3f < cat && cat < 0x5b) {
        char buf[16];
        std::snprintf(buf, sizeof buf, kFmt_462500, static_cast<int>(cat) - 0x35);    // "IS%d"
        const uint32_t want = static_cast<uint32_t>(static_cast<uint8_t>(buf[0])) |
                              (static_cast<uint32_t>(static_cast<uint8_t>(buf[1])) << 8) |
                              (static_cast<uint32_t>(static_cast<uint8_t>(buf[2])) << 16) |
                              (static_cast<uint32_t>(static_cast<uint8_t>(buf[3])) << 24);
        return want == id ? 1 : 0;
    }
    return 0;
}

// orig FUN_001a6d00 (work/decompiled/001a6d00.c; the jump table 0x498980 cases were read from the disassembly
// 0x1a6d70..0x1a718c). Every accepted case ends in the predicate FUN_001a0e60(idx).
int talkCondition(const TalkSelectState& mgr, int idx, uint32_t kind, int a, int b, const FlagManager& f) {
    const TalkCond& e = kTalkCond[idx];
    if (kind != static_cast<uint32_t>(e.kind) && kind != 5) return 0;
    if (kind >= 0x10) return talkPredicate(f, idx);
    switch (kind) {
    case 1: {   // 0x1a6d70: attack reaction (a = 0/1/2 slot, b = hit sub-type)
        if (a == 0) {
            if (e.c1 != 0 && e.c1 != 3 && e.c1 != 4) return 0;
        } else if (a == 1) {
            if (e.c1 != 1 && e.c1 != 3 && e.c1 != 4) return 0;
        } else if (a == 2) {
            if (e.c1 != 2 && e.c1 != 4) return 0;
        }
        if (b == 0) {
            if (e.c2 != 0 && e.c2 != -1) return 0;
        } else if (b >= 1 && b <= 3) {
            if (e.c2 != b && e.c2 != 8 && e.c2 != -1) return 0;
        } else if (b >= 4 && b <= 7) {
            if (e.c2 != b && e.c2 != 8 && e.c2 != 9 && e.c2 != -1) return 0;
        }
        return talkPredicate(f, idx);
    }
    case 2: {   // 0x1a7048: item shown / given
        const bool speaker = gHooks.npcExists != nullptr && gHooks.npcExists(mgr.itemTalkActor);   // FUN_001a9810
        if (!speaker) return 0;
        if (mgr.giftItem != nullptr && talkItemMatch(mgr, *mgr.giftItem, e.c1, f) == 0) return 0;
        return talkPredicate(f, idx);
    }
    case 3:     // 0x1a7090: map arrival; c1 == -1 jumps straight to the c3 test (0x1a70a4 -> 0x1a70dc)
        if (e.c1 != -1) {
            if (a != e.c1) return 0;
            if (e.c2 != -1 && b != e.c2) return 0;
        }
        if (e.c3 != -1) return 0;
        return talkPredicate(f, idx);
    case 4:     // 0x1a7100: floor
    case 0xf:   // 0x1a7158: idle
        if (e.c1 != -1 && a != e.c1) return 0;
        return talkPredicate(f, idx);
    case 0xc:   // 0x1a712c: special item (+0xfac)
        if (mgr.shownItem == nullptr) return 0;   // UNVERIFIED: the original passes the pointer unchecked
        if (talkItemMatch(mgr, *mgr.shownItem, e.c1, f) == 0) return 0;
        return talkPredicate(f, idx);
    default:    // 0, 5..11, 13, 14 -> 0x1a7184
        return talkPredicate(f, idx);
    }
}

int talkSpeakerStart(uint32_t actorId) {
    char buf[8];
    if (actorId == kId_454228 || actorId == kId_462288 || actorId == kId_462290 || actorId == kId_462298)
        std::memcpy(buf, "C23A", 5);                                    // FUN_0023edb8(buf, 0x454228 "C23A")
    else
        idString(actorId, buf);                                          // "%c%c%c%c"
    for (const TalkSpeakerStart& s : kTalkSpeakerStart)
        if (std::strcmp(buf, s.id) == 0) return s.start;
    return 0;
}

// orig FUN_001a69e0 (work/decompiled/001a69e0.c)
int talkSearch(const TalkSelectState& mgr, const TalkActorRef* actor, int kind, int a, int b, bool anySpeaker,
               const FlagManager& f) {
    if (!anySpeaker && actor == nullptr) return -1;
    if (actor != nullptr && mgr.excludedSpeaker == actor->id) return -1;
    char buf[8] = {};
    if (!anySpeaker) {
        const uint32_t id = actor->id;
        if (id == kId_454228 || id == kId_462288 || id == kId_462290 || id == kId_462298) std::memcpy(buf, "C23A", 5);
        else idString(id, buf);
    }
    int idx = 0;
    if (!anySpeaker) {
        for (const TalkSpeakerStart& s : kTalkSpeakerStart)
            if (std::strcmp(buf, s.id) == 0) { idx = s.start; break; }
    }
    const data::TalkEntry* t = data::talkTable();
    for (; idx < data::kTalkTableCount; ++idx) {
        if (!anySpeaker && std::strcmp(t[idx].speaker, buf) != 0) return -1;
        if (talkCondition(mgr, idx, static_cast<uint32_t>(kind), a, b, f) != 0) return idx;
    }
    return -1;
}
}
