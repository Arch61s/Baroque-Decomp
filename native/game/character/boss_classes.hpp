#pragma once
// Boss / special-enemy support (work/reports/BOSSES_2026-10-10.md):
//   - ACT ThrowItm (FUN_00148880): E27 boss coin/item throw ("E27T"), E15 thief stolen-item throw ("ITEM"), NPC
//     C09T / C11T / C12T random throws. Called from the default branch of actCombatCommand (combat.cpp).
//   - the steal of a hit with m3 0x40 (FUN_001392f0, E15 thief / player steal equipment), installed as
//     CombatHooks::steal on the first AI global tick when nothing else installed it; likewise the death drop
//     FUN_00136d30 / FUN_001330c0 (CombatHooks::dropItem: carried / stolen item, else the PRMCHAR drop table).
//   - PORT dev options for boss verification (not original behaviour), read from the process command line:
//       --warp-player-to TAG [TICK]  on game tick TICK (default 30) the player is placed on the first live entity with
//                                    the 4-char tag (e.g. a warp pad O70C, whose own tick then triggers the spawner)
//       --warp-offset D              with --warp-player-to: D units along the entity's forward, facing it
//       --boss-log N                 every N ticks: one log line per live enemy (id, HP, action, AI FLAG/MODE, pos)
//                                    and the player (HP, pos); boss flag changes (0x3bc..0x3c5, 0x19, 0x1e, 5) as well
//       --boss-kill-at TICK          all live enemies get HP 0 on that tick (the real death state follows)
//       --boss-flag N                (repeatable) game flag N set on the second tick of the first floor, which is then
//                                    re-entered (FUN_0011bfe0) so the floor generation sees it (main's --flag is wiped
//                                    by the new-game start of --floor)
//       --boss-hp-at TICK PCT        all live enemies get PCT % of their max HP on that tick (HP_PCT phase tests)
//       --fire-j030-at TICK          the player fires one Angelic Rifle shot (J030 created + launched as by ACT Object)
//       --player-keep-hp             the player HP max / HP are set to 999 every tick while alive (long fights)
#include "data/act.hpp"

namespace baroque::game {

struct Chr;
class ActRunner;

// Returns true when the command was handled (ThrowItm).
bool bossActCommand(Chr& owner, ActRunner& act, const data::ActCmnd& c);

// PORT dev hook, once per engine tick (called from aiGlobalTick, FUN_001ed450 position).
void bossDevTick();

}  // namespace baroque::game
