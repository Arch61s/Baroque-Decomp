// Player status record and rebirth (see player_progress.hpp).
#include "game/progress/player_progress.hpp"

#include "game/character/character.hpp"
#include "game/character/status_set.hpp"
#include "game/flag_manager.hpp"
#include "game/hud/hud_api.hpp"
#include "game/items/chr_adapter.hpp"
#include "game/items/inventory.hpp"
#include "game/items/item.hpp"

#include <cstring>

namespace baroque::game {

namespace {
void put(uint8_t* d, int word, int32_t v) { std::memcpy(d + word * 4, &v, 4); }
int32_t get(const uint8_t* s, int word) { int32_t v; std::memcpy(&v, s + word * 4, 4); return v; }
int32_t clamp99(int32_t v) {
    if (v < -99) return -99;   // 0xffffff9d
    if (99 < v) return 99;
    return v;
}
}  // namespace

// orig FUN_00139750 (work/decompiled/00139750.c)
void playerRecordWrite(const StatusObj& s, uint8_t* d) {
    const StatusBlock& b = s.block;
    const int32_t head[12] = {b.hpMax, b.hp, b.vtMax, b.vt, b.atkBase, b.atkMax, b.atkLevel, b.defBase, b.defMax,
                              b.defLevel, b.critical, b.expValue};
    for (int i = 0; i < 12; ++i) put(d, i, head[i]);
    for (int i = 0; i < 3; ++i) put(d, 0xc + i, b.element[static_cast<size_t>(i)]);
    for (int i = 0; i < 3; ++i) put(d, 0xf + i, b.directional[static_cast<size_t>(i)]);
    for (int i = 0; i < 0x13; ++i) put(d, 0x12 + i, b.resist[static_cast<size_t>(i)]);
    put(d, 0x25, static_cast<int32_t>(b.statusMask));
    for (int i = 0; i < 0x13; ++i) put(d, 0x26 + i, b.timers[static_cast<size_t>(i)]);
    put(d, 0x39, b.level);
    put(d, 0x3a, b.exp);
    put(d, 0x3b, b.expNext);
    put(d, 0x3c, static_cast<int32_t>(b.brand));
    for (int i = 0; i < 5; ++i) put(d, 0x3d + i, b.hidden[static_cast<size_t>(i)]);
    ItemSaveRecord rec;
    writeItemRecord(static_cast<const Item*>(b.parasite), rec);   // FUN_001d5a50 / FUN_00116560
    std::memcpy(d + 0x42 * 4, rec.w, sizeof rec.w);
    writeItemRecord(static_cast<const Item*>(b.carried), rec);
    std::memcpy(d + 0xa8 * 4, rec.w, sizeof rec.w);
    put(d, 0x10e, statusHpMaxWithItems(s));                      // FUN_001343f0
    put(d, 0x10f, statusVtMaxTotal(s));                          // FUN_00134470
    put(d, 0x110, clamp99(inventory().atkTotal(itemSystem())));  // FUN_001d83d0(0x53d910)
    put(d, 0x111, clamp99(inventory().defTotal(itemSystem())));  // FUN_001d85b0(0x53d910)
}

// orig FUN_00139b40 (work/decompiled/00139b40.c)
void playerRecordRead(StatusObj& s, const uint8_t* src) {
    StatusBlock& b = s.block;
    for (int i = 0; i < 3; ++i) b.element[static_cast<size_t>(i)] = get(src, 0xc + i);
    for (int i = 0; i < 3; ++i) b.directional[static_cast<size_t>(i)] = get(src, 0xf + i);
    for (int i = 0; i < 0x13; ++i) b.resist[static_cast<size_t>(i)] = get(src, 0x12 + i);
    // FUN_00138850(status, mask): re-applies each saved status (flag, duration, visual, message, instant effects); the
    // mask bit is kept only where the duration table gives a non-zero timer. The saved timers follow.
    statusSet(s, static_cast<uint32_t>(get(src, 0x25)));
    for (int i = 0; i < 0x13; ++i) b.timers[static_cast<size_t>(i)] = get(src, 0x26 + i);
    b.hpMax = get(src, 0);
    b.hp = get(src, 1);
    b.vtMax = get(src, 2);
    b.vt = get(src, 3);
    b.atkBase = get(src, 4);
    b.atkMax = get(src, 5);
    b.atkLevel = get(src, 6);
    b.defBase = get(src, 7);
    b.defMax = get(src, 8);
    b.defLevel = get(src, 9);
    b.critical = get(src, 10);
    b.expValue = get(src, 11);
    b.level = get(src, 0x39);
    b.exp = get(src, 0x3a);
    b.expNext = get(src, 0x3b);
    b.brand = static_cast<uint32_t>(get(src, 0x3c));
    for (int i = 0; i < 5; ++i) b.hidden[static_cast<size_t>(i)] = get(src, 0x3d + i);
    ItemSaveRecord rec;
    std::memcpy(rec.w, src + 0x42 * 4, sizeof rec.w);
    b.parasite = readItemRecord(rec, itemSystem());              // FUN_001d5bc0
    std::memcpy(rec.w, src + 0xa8 * 4, sizeof rec.w);
    b.carried = readItemRecord(rec, itemSystem());
}

// orig FUN_00133aa0 (work/decompiled/00133aa0.c)
void statusRevive(StatusObj& s) {
    StatusBlock& b = s.block;
    b.hp = 0;
    gameAdapter().player().cureAll();                            // FUN_00138ac0 (status tables: chr_adapter)
    if (b.brand != 0) {                                          // FUN_00139240
        b.brand = 0;
        flags().clear(0x3cc);
    }
    if (b.parasite != nullptr) {                                 // FUN_001392a0 (FUN_0012be90 registry unlink)
        b.parasite = nullptr;
        flags().clear(0x3cb);
    }
    if (b.carried != nullptr) b.carried = nullptr;               // FUN_00139550
    statusCopyPrm(s);                                            // FUN_00133840
    // FUN_00133b10: statusPlayerNew = FUN_00133a60 = hidden stats zeroed + FUN_00133b10; the hidden stats are kept
    const auto hidden = b.hidden;
    statusPlayerNew(s);
    b.hidden = hidden;
    // uRam0050b1d0 / 0x50b1d8 / 0x50b1e0 = 0: UNVERIFIED (player status-effect globals, not modelled)
}

// inline idiom of FUN_0011a8a0 / FUN_0011bad0 on the player's state machine (comp +0xc)
void playerStateReset() {
    Chr* p = characters().player();
    if (p == nullptr) return;
    StateMachine& sm = p->sm;
    bool reset;
    if (sm.curFactory == StateId::None) {
        if (sm.next != StateId::None || sm.popRequested) reset = !(2 < sm.priority);
        else reset = true;
    } else {
        reset = !(2 < sm.priority);
    }
    if (reset) {
        sm.next = StateId::None;                                 // +0x18 = -1 (pop request)
        sm.popRequested = true;
        sm.arg = 0;
        sm.priority = 2;
        sm.priorityCopy = 2;
    }
    if (p->actLoaded) p->act.stop();                             // FUN_00143bc0
}

// orig FUN_0011a8a0 (work/decompiled/0011a8a0.c)
void playerRevive() {
    Chr* p = characters().player();
    if (p == nullptr) return;
    statusRevive(p->status);                                     // FUN_00133aa0
    playerStateReset();
    chrUpdate(*p);                                               // vt+0xc
}

}  // namespace baroque::game
