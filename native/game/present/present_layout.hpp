#pragma once
// PORT widescreen layout (PRESENTATION_PLAN.md 3.2 / 3.4, user decision 2026-10-10):
//  * the 3-D scene is Hor+ (vertical FOV of the original 4:3 picture kept, wider windows see more at the sides);
//  * the presented area is the window clamped to [4:3, max_aspect] (black pillars beyond the cap, letterbox below 4:3);
//  * the 512x448 UI raster maps onto the centred 4:3 box; the area adds `delta` raster units on each side
//    (extended raster -delta .. 512+delta). Menus, front-end pages and movies stay in the 4:3 box, unstretched;
//    flat full-screen quads (fade, pause dim, event colour quad, letterbox bars, item-screen backdrop) widen to the
//    extended raster; edge-anchored HUD elements move by hudDelta toward their edge (0 with the classic 4:3 HUD).
// The layout type lives in game/render2d.hpp (ScreenLayout) so the 2-D renderer links without this module; the host
// installs presentLayout with game::setScreenLayoutProvider.
#include "alchemy/scene_render.hpp"
#include "game/render2d.hpp"

namespace baroque::game::present {

ScreenLayout presentLayout(int screenW, int screenH);

// Projection of the 3-D pass for an aspect (FUN_00101b60 / FUN_0039e800: fovDeg is the horizontal FOV of the 4:3
// picture; its vertical FOV is kept at every aspect = Hor+).
alchemy::Mat4 presentProjection(float fovDeg, float nearZ, float farZ, float aspect);

}  // namespace baroque::game::present
