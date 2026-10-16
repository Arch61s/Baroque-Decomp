#pragma once
// PORT-ONLY (not original): keyboard + mouse gun aim through a reticle (work/reports/CONTROLS_NATIVE.md "Gun reticle").
//
// How the original aims the gun (C00A action AT01_G1, ACT Object "J030" -> FUN_00148350 -> launch FUN_001c41e0):
//   velocity = player forward * 10 (DAT_00463978); only in the FIRST-PERSON view (option bit 0x51af68 & 4 ->
//   controller 0x50b620 |= 0x20, FUN_0013b0d0) the vertical part is replaced by sin(camera +0xc8 pitchRad) * 10 and
//   the flying effect 0x14 is pitched by -pitchRad. pitchRad is the camera pitch (+0xc0, right stick, first person
//   clamped [-25, 25] deg by FUN_0013eff0) plus the eased head pitch (+0xc4), clamped +-0.43633232 rad (FUN_0013f130).
//   In the third-person view the shot is always level (player forward has y 0), so the floor can only be shot in the
//   first-person view, looking down (at most 25 deg).
//
// The port's free mouse camera (game/free_camera.hpp) is a third-person orbit, so without this the mouse could never
// pitch the shot. While it is active (keyboard / mouse play only, never with a gamepad or in first person) and the gun
// (FUN_001d7d10 class 3) is equipped:
//   * a reticle is drawn (game/hud/gun_reticle.cpp) at a fixed point above the screen centre;
//   * the camera ray through it is cast into the world (collision FUN_00143150 + the hurt volumes of the hit manager
//     0x50c780, i.e. what the shot can strike), starting where the ray passes the player (never at the player);
//   * at the attack start the player turns toward that aim point (FUN_00128fc0; chained onto aimAssistHooks(), after
//     the camera-relative turn of aim_assist.cpp; its 12 / 25 deg ground cone is replaced by an assist on the aim ray);
//   * at the J030 launch the shot is pointed from its real spawn point at the aim point through the original
//     first-person formula: horizontal = unit direction * 10, vertical = sin(pitch) * 10, pitch clamped to the
//     original +-0.43633232 rad, effect pitch -pitch. A clamped / blocked shot shows where it will really land.
#include "game/player_move.hpp"

namespace baroque::game {

struct Chr;

inline constexpr float kGunPitchLimit = 0.43633232f;   // camera +0xc8 clamp (FUN_0013f130), = 25 deg

// The gun reticle mode is on: free mouse camera active, camera-relative attacks on (controls.ini [aim]), the gun
// equipped, the player present and not in first person, no event / talk camera.
bool gunAimActive();
bool gunAimHasAmmo();
// Re-checks that the attack-start turn is chained onto aimAssistHooks() (static initialisation order).
void gunAimEnsureHooks();

// Camera ray of the reticle for a player position (logic: the tick position; HUD: the presented one).
struct GunAimView {
    Vec3f eye{}, right{}, up{}, back{};   // presented free camera basis (back = eye - look-at, camera +0x70 convention)
    Vec3f rayDir{};                       // unit direction through the reticle
    Vec3f rayStart{};                     // where the cast starts (past the player)
    float tanHalfY = 0.0f;                // projection: tan(vertical half FOV)
};
bool gunAimView(const Vec3f& playerPos, GunAimView& out);

struct GunAimSolution {
    Vec3f aimPoint{};       // under the reticle
    bool onEnemy = false;   // the reticle is on an enemy's hurt volume (or the aim assist picked one)
    Vec3f muzzle{};         // J030 spawn point used
    Vec3f hdir{};           // horizontal unit direction of the shot
    float pitch = 0.0f;     // radians, clamped to +-kGunPitchLimit
    bool clamped = false;
    Vec3f impact{};         // simulated J030 flight: where it ends (wall / floor / character)
    bool impactValid = false;
};
// Aim point under the reticle (no muzzle needed).
bool gunAimPoint(const Chr& player, const Vec3f& playerPos, Vec3f& aim, bool* onEnemy);
// Full solution for the HUD: predicted spawn point (the player turned toward the aim point) and the flight.
bool gunAimPredict(const Chr& player, const Vec3f& playerPos, GunAimSolution& out);
// FUN_001c41e0 hook: true = use (hdir, pitch) for the shot spawned at `spawn` by `owner` (the player).
// Test trace (BAROQUE_AIM_LOG): where a J030 ended after its wall contact.
void gunAimLogEnd(const Vec3f& pos);
bool gunAimLaunch(const Chr& owner, const Vec3f& actAt, const Vec3f& spawn, Vec3f& hdir, float& pitch);

}  // namespace baroque::game
