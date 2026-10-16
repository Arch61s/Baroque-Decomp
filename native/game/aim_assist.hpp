#pragma once
// PORT-ONLY (not original): keyboard + mouse attack direction and subtle aim assist (work/reports/CONTROLS_NATIVE.md "Aim assist").
//
// The PS2 attack start FUN_0013c4e0 (player_control.cpp playerAttackStart) attacks along the character's facing; the
// pad player steers it with the left stick during the first frames of the attack (FUN_0013b0d0: while ctl.combo < 6
// the "AT" branch calls FUN_0013cf90 turn-only, FUN_00128fc0 45 deg scaled). With the free mouse camera
// (game/free_camera.hpp) the facing has no relation to where the player looks, so a click hits "somewhere".
//
// While the free mouse camera is active (keyboard / mouse play; never with a gamepad, never in first person, never
// with an event camera or a lock-on target), at the single point where the original has just started the attack
// action:
//   1. camera-relative attacks: the attack direction becomes the free camera's forward on the ground plane;
//   2. aim assist: among living enemies (type 2, not NPCs) within the weapon's reach and inside a cone around that
//      direction, the one minimising (angle / cone + distance / reach) pulls the direction toward itself by at most a
//      small angle (subtle 20 deg melee / 12 deg gun);
//   3. the player is turned onto that direction with the original turn function FUN_00128fc0 (turnToward, 180 deg,
//      unscaled), and during that attack's original turn window the left-stick steering of FUN_0013cf90 is pointed
//      at the same direction (so WASD held during the click does not pull the hit away again).
// The ACT / attack logic then runs unchanged (the Angelic Rifle shot J030 launches along the player's forward).
// With a gamepad the hooks below are never consulted (freeCameraActive() is false), so pad play is bit-identical.
//
// The hook table is header-only so every target compiling player_control.cpp links (the PlayerControlCheck leaves it
// empty); game/aim_assist.cpp (Baroque-Native, cmake/aim_assist.cmake) installs it.
#include <cstdint>

namespace baroque::game {

struct Chr;
struct PlayerControl;

// ---- settings (controls.ini [aim]; Baroque-Native only, implemented in aim_assist.cpp) ----
enum class AimAssistLevel : int { Off = 0, Subtle = 1, Strong = 2 };
AimAssistLevel aimAssistLevel();                                  // default Subtle
void setAimAssistLevel(AimAssistLevel level, bool save = true);   // applies live; save = write controls.ini
bool cameraRelativeAttacks();                                     // default on
void setCameraRelativeAttacks(bool on, bool save = true);
const char* aimAssistLevelName(AimAssistLevel level);             // "off" / "subtle" / "strong" (controls.ini values)

// ---- hooks called by player_control.cpp ----
struct AimAssistHooks {
    // Right after FUN_0013c4e0 started the attack action (weaponClass = FUN_001d7d10: 0..2 swords, 3 gun, -1 none).
    void (*attackStart)(PlayerControl& ctl, Chr& chr, int weaponClass) = nullptr;
    // FUN_0013b0d0 attack turn window (FUN_0013cf90 turn-only): the ground direction the steering should aim at,
    // false = keep the original stick steering.
    bool (*turnWindowDir)(const PlayerControl& ctl, const Chr& chr, float dir[3]) = nullptr;
};
inline AimAssistHooks& aimAssistHooks() {
    static AimAssistHooks h;
    return h;
}

}  // namespace baroque::game
