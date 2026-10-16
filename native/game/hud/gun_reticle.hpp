#pragma once
// PORT-ONLY (not original): keyboard + mouse gun reticle (game/gun_aim.hpp). The PS2 shows no reticle: its gun aims
// along the facing, pitched only in the first-person view. Drawn at window resolution right after the 3-D pass and
// before the 2-D UI layers (hud_api.cpp hudDrawWindowOverlay), so menus and the HUD cover it.
namespace baroque::game {

// alpha = the presented time between the last two ticks (main.cpp present).
void gunReticleDraw(int screenW, int screenH, float alpha);

}  // namespace baroque::game
