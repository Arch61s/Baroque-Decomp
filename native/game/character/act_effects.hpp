#pragma once
// ACT effect commands of the characters (act_effects.cpp, game build only).
#include "data/act.hpp"
#include "game/act_runner.hpp"

namespace baroque::game {

struct Chr;

void actEffectCommand(ActRunner& r, const data::ActCmnd& c);   // Exi_a / Exi_b / ExiZofs / ExiLpOut / ExiEnd
void actEffectReset(ActRunner& r);                               // orig FUN_00144a70
void actEffectFollow(Chr& c);                                    // orig FUN_00144560
void chrActEffectsFrame();                                       // orig FUN_0012a4e0 (engine frame FUN_00101bd0)
bool actEffectsFrameHooked();                                    // the engine calls chrActEffectsFrame()

}  // namespace baroque::game
