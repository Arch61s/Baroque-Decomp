// Status set FUN_00138850 and its per-status helpers FUN_00137220 / FUN_00137540 / FUN_00137d40 (see status_set.hpp).
// The duration table FUN_00137aa0 and the instant effects FUN_00138020 live with the other status code in combat.cpp.
#include "game/character/status_set.hpp"

#include "data/prm.hpp"
#include "game/character/character.hpp"
#include "game/character/combat.hpp"
#include "game/effects/effects.hpp"
#include "game/flag_manager.hpp"
#include "game/sound_stub.hpp"

namespace baroque::game {

namespace {
bool isPlayerObj(const StatusObj& s) { return s.owner != nullptr && s.owner == characters().player(); }   // +4 == iRam0050b144
}  // namespace

// orig FUN_00137220 (work/decompiled/00137220.c): FUN_00178890(0x53c3b0, flag) per status
void statusSetPlayerFlag(int i) {
    if (i == 6) flags().set(0x70);
    else if (i == 0x12) flags().set(0x6f);
    else if (i == 0x11) flags().set(0x6e);
    else if (i == 0x10) flags().set(0x6d);
    else if (i == 0xf) flags().set(0x6c);
    else if (i == 0xe) flags().set(0x6b);
    else if (i == 5) flags().set(0x6a);
    else if (i == 4) flags().set(0x69);
    else if (i == 3) flags().set(0x68);
    else if (i == 2) flags().set(0x67);
    else if (i == 1) flags().set(0x71);
    else if (i == 0) flags().set(0x65);
}

// orig FUN_001373b0 (work/decompiled/001373b0.c): FUN_00178a30(0x53c3b0, flag), the flags of FUN_00137220
void statusClearPlayerFlag(int i) {
    if (i == 6) flags().clear(0x70);
    else if (i == 0x12) flags().clear(0x6f);
    else if (i == 0x11) flags().clear(0x6e);
    else if (i == 0x10) flags().clear(0x6d);
    else if (i == 0xf) flags().clear(0x6c);
    else if (i == 0xe) flags().clear(0x6b);
    else if (i == 5) flags().clear(0x6a);
    else if (i == 4) flags().clear(0x69);
    else if (i == 3) flags().clear(0x68);
    else if (i == 2) flags().clear(0x67);
    else if (i == 1) flags().clear(0x71);
    else if (i == 0) flags().clear(0x65);
}

// orig FUN_00137cf0 (work/decompiled/00137cf0.c): timer 0; Stomachache gives back ATK / DEF (+0x14 := +0x10, +0x20 :=
// +0x1c)
void statusClearTimer(StatusObj& s, int i) {
    StatusBlock& b = s.block;
    b.timers[static_cast<size_t>(i)] = 0;
    if (i == 6) {
        b.atkMax = b.atkBase;
        b.defMax = b.defBase;
    }
}

// orig FUN_001378a0 (work/decompiled/001378a0.c): the recovery line, unless the item screen is open (iRam00517b68 ==
// 1); param_2 = the player with HP > 0
void statusRecoverMessage(int i, bool playerAlive) {
    const CombatHooks& h = combatHooks();
    if ((h.itemScreenOpen && h.itemScreenOpen()) || !playerAlive) return;
    int msg = -1;
    if (i == 6) msg = 0x2ea;
    else if (i == 0x12) msg = 0x2d1;
    else if (i == 0x11 || i == 0xf) msg = -1;
    else if (i == 0x10) msg = 0x2f0;
    else if (i == 0xe) msg = 0x2d4;
    else if (i == 5) msg = 0x2d6;
    else if (i == 4) msg = 0x2f6;
    else if (i == 3) msg = 0x2a2;
    else if (i == 2) msg = 0x2de;
    else if (i == 1) msg = 699;
    else if (i == 0) msg = 0x29a;
    if (msg != -1 && h.log) h.log(msg, 0);
}

// orig FUN_00137540 (work/decompiled/00137540.c): FUN_0015d480(0x5180f0, FUN_00114da0(msg)) unless the item screen is
// open (iRam00517b68 == 1). Statuses 0xf / 0x11 and 7..0xd have no message; 0x12 has one for the player only.
void statusSetMessage(int i, bool isPlayer) {
    const CombatHooks& h = combatHooks();
    if (h.itemScreenOpen && h.itemScreenOpen()) return;
    int msg = -1;
    if (i == 6) msg = isPlayer ? 0x2c8 : 0x2ac;
    else if (i == 0x12) msg = isPlayer ? 0x2c4 : -1;
    else if (i == 0x11) msg = -1;
    else if (i == 0x10) msg = isPlayer ? 0x2c9 : 0x2ad;
    else if (i == 0xf) msg = -1;
    else if (i == 0xe) msg = isPlayer ? 0x2c5 : 0x2a9;
    else if (i == 5) msg = isPlayer ? 0x2c6 : 0x2aa;
    else if (i == 4) msg = isPlayer ? 0x2ca : 0x2ae;
    else if (i == 3) msg = isPlayer ? 0x2c2 : 0x2a7;
    else if (i == 2) msg = isPlayer ? 0x2c7 : 0x2ab;
    else if (i == 1) msg = isPlayer ? 0x2c3 : 0x2a8;
    else if (i == 0) msg = isPlayer ? 0x2c0 : 0x2a4;
    if (msg != -1 && h.log) h.log(msg, 0);
}

// orig FUN_00137d40 (work/decompiled/00137d40.c; disassembled for the f12 tag 0 and the effect slot = status index)
void statusVisualOn(StatusObj& s, int i) {
    if (i == 0x12) {
        sound().seOverrideBegin();                                   // FUN_001161a0(0x4e9790)
    } else if (i == 5) {
        if (s.owner != nullptr) s.owner->model.anim.setScale(0.5f, 0);   // FUN_0010ae50(0.5, **(chr +0x2c), 0)
    } else if (i == 4 || i == 2) {
        // i == 3 returns without an effect; 4 / 2: non-player characters only, once (slot -1 = none)
        if (isPlayerObj(s) || s.owner == nullptr) return;
        int32_t& slot = s.block.effects[static_cast<size_t>(i)];
        if (slot != -1) return;
        const data::PrmCharFile* f = characters().prm;               // FUN_00133560(chr id)
        const data::PrmChar* p = f != nullptr ? data::findPrmChar(*f, s.owner->id) : nullptr;
        const auto& pos = s.owner->xf.m.pos;
        const float p4[4] = {pos[0], (p != nullptr ? p->iconHeight : 0.0f) + pos[1], pos[2], 1.0f};
        const float sc[4] = {100.0f, 100.0f, 100.0f, 1.0f};
        slot = effects().spawn(0.0f, i == 4 ? 0x6b : 0x69, p4, nullptr, sc);   // FUN_00112190(0, fx, pos, 0, scale)
    }
}

// orig FUN_00138850 (work/decompiled/00138850.c, checked against the disassembly)
uint32_t statusSet(StatusObj& s, uint32_t mask) {
    uint32_t applied = 0;
    uint32_t bit = 1;
    for (int i = 0; i < 0x13; ++i, bit <<= 1) {
        if ((mask & bit) == 0) continue;
        if (isPlayerObj(s)) statusSetPlayerFlag(i);                  // FUN_00137220
        statusSetDuration(s, i);                                     // FUN_00137aa0
        statusVisualOn(s, i);                                        // FUN_00137d40
        statusSetMessage(i, isPlayerObj(s));                         // FUN_00137540
        if (s.block.timers[static_cast<size_t>(i)] != 0) s.block.statusMask |= bit;
        applied |= bit;
    }
    statusInstantEffects(s, applied | (mask & 0x300000u));          // FUN_00138020
    return applied;
}

}  // namespace baroque::game
