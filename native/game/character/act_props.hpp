#pragma once
// ACT commands of the presentation / prop family (STATUS.md 2026-10-10 "ACT props"), executed from the default branch
// of actCombatCommand (combat.cpp):
//   Track FUN_001492b0, TrackEx FUN_00149320, TrackEnd FUN_00149390 (weapon trails, act_trail.*)
//   NTurnPos FUN_00149750, NTurnAct FUN_00149840, NTurnEnd FUN_00149970 (head look-at object ACT +0x64: FUN_001456f0
//     create, FUN_00145840 / FUN_001458a0 targets, FUN_001459c0 release, FUN_001459f0 tick, FUN_00145900 delete)
//   ShowBox FUN_00149b90 / HideBox FUN_00149c00 (C12A box node FUN_001b56c0 / FUN_001b5680)
//   ObjThrow FUN_001485a0, DropItem FUN_00149000, ClearItm FUN_00149210, GetItem FUN_0014a0b0, ShowItem FUN_0014a100,
//   HideItem FUN_0014a2e0 (ThrowItm FUN_00148880 stays in boss_classes.cpp)
#include "data/act.hpp"

#include <cstdint>

namespace baroque::game {

struct Chr;
class ActRunner;

// true when the command belongs to this family (handled)
bool actPropCommand(Chr& owner, ActRunner& act, const data::ActCmnd& c);
// orig FUN_001459f0: first call of the ACT tick FUN_00143fe0 (before FUN_00146120)
void actNTurnStep(Chr& c);
// The parts of the ACT reset FUN_00143bf0 that belong to this family, in the original order: FUN_00145660 (trails end
// over 4 ticks), FUN_001459c0 (look-at release); after thunk_FUN_001460d0 / FUN_00146210: the ShowItem prop leaves
// the world (ACT flag 0x10000000 -> FUN_001d3b30) and the GetItem bit (status object +0x10 bit 0) is cleared.
// Call with the ACT flags as they were before the reset clears them.
void actPropsResetTrailsLook(Chr& c);
void actPropsResetItems(Chr& c, uint32_t actFlags);
// PORT: dev options (--equip-at TICK, --trail-log N, --act-at ID NAME TICK, --spawn-at ID X Y Z TICK), run from the
// game tick
void actPropsDevTick();

}  // namespace baroque::game
