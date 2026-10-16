#pragma once
// PORT-ONLY (not original): free mouse camera for keyboard + mouse play (work/reports/CAMERA_NATIVE.md §7).
//
// The PS2 had no mouse. The original gameplay camera (game/camera.*, FUN_0013e340) keeps running untouched every tick
// and stays the camera for gamepad play. While the free camera is active (controls.ini [mouse] free_camera, default
// `auto` = the last input device was the keyboard / mouse) the presentation draws this orbit camera instead of the
// original one, and the player movement FUN_0013cf90 reads this camera's ground axes instead of camera +0x70 / +0x58.
// Event and conversation cameras still take over as in the original (they are a different camera source).
//
//   pivot  = player position + (0, preset height, 0)            (FUN_0013e340: target.y = height + player.y)
//   eye    = pivot + d * (cos(elev) * back(yaw) + sin(elev) * up), d <= dist = the preset boom (FUN_0013e100)
//   back(yaw) = (sin yaw, 0, cos yaw)                            (horizontal target -> eye, same as camera +0xa0)
//   look-at = pivot - (0, dist * tan(-default pitch), 0)         (where the original's tilted view line meets the
//                                                                 player: dungeon 185 - 80, hub 145 - 42)
// At elev 0 and full boom this is exactly the original framing (eye at target height, `dist` behind, looking down by
// the preset pitch); mouse Y raises / lowers the eye on the orbit and the view keeps aiming at the look-at point.
// Wall clipping (free_camera_view.cpp): every presented frame the boom is cast from the pivot toward the eye against the
// drawn map triangles (centre + four near-plane-corner rays) and the game's collision sweep (FUN_00143150), and the eye
// eases in before the nearest hit (trailing at most kFreeCamClipSlack of the boom behind it) and eases back out when
// the obstruction clears. No logic state is touched.
//
// This header (state, mouse input, movement axes) is header-only so every target compiling player_control.cpp links.
#include <cmath>
#include <cstdint>


namespace baroque::game {

struct FreeCamera {
    bool active = false;        // set once per tick by the camera task (mode switch); read by FUN_0013cf90's adapter
    bool synced = false;        // yaw / elevation initialised from the original camera
    uint32_t syncSerial = 0;    // original camera cutSerial at the last sync (init / reset re-sync behind the player)
    float yaw = 0.0f;           // radians; back = (sin yaw, 0, cos yaw)
    float elevDeg = 0.0f;       // eye elevation above the pivot in degrees; + = above, looking down
    float shownDist = -1.0f;    // presentation: boom length drawn last frame (-1 = snap)
};

inline constexpr float kFreeCamElevMinDeg = -35.0f;   // lowest eye (looking up at the player)
inline constexpr float kFreeCamElevMaxDeg = 70.0f;    // highest eye (looking down on the player)
inline constexpr float kFreeCamSweepRadius = 15.0f;   // > the near-plane corner distance (near 10, Hor+ to ~21:9)
inline constexpr float kFreeCamMinDist = 1.0f;        // FUN_0013e340's boom floor (hit distance < 1 -> 1)
inline constexpr float kFreeCamEaseOutPerSec = 3.0f;  // PORT (was 10): ease-out when an obstruction clears
inline constexpr float kFreeCamEaseInPerSec = 4.0f;   // PORT (was 12): ease-in toward a wall
inline constexpr float kFreeCamClipSlack = 0.4f;      // PORT (was 0.2): the eye may trail this fraction of
                                                      // the boom past a hit (into the wall, thinned by the dither)
inline constexpr float kFreeCamMinFrac = 0.5f;        // PORT: shortest boom, fraction of the preset boom
inline constexpr float kFreeCamFloorClear = 20.0f;    // PORT: a low eye stays this far above the player's feet
inline constexpr float kFreeCamCutPerSec = 8.0f;      // see-through fade rate (in / out, ~0.3 s)

inline FreeCamera& freeCamera() {
    static FreeCamera c;
    return c;
}
inline bool freeCameraActive() { return freeCamera().active; }

// Mouse look in degrees: yawDegRight > 0 turns the view right, pitchDegDown > 0 looks down (eye rises).
inline void freeCameraLook(float yawDegRight, float pitchDegDown) {
    FreeCamera& c = freeCamera();
    const float kDeg = 3.14159265f / 180.0f;
    // back(yaw) rotated by +a = FUN_00206be0 rotateY(+a) = the original's right-stick-right orbit (view turns left),
    // so turning the view right decreases yaw.
    c.yaw -= yawDegRight * kDeg;
    const float twoPi = 6.28318531f;
    if (c.yaw > twoPi || c.yaw < -twoPi) c.yaw = std::fmod(c.yaw, twoPi);
    c.elevDeg += pitchDegDown;
    if (c.elevDeg < kFreeCamElevMinDeg) c.elevDeg = kFreeCamElevMinDeg;
    if (c.elevDeg > kFreeCamElevMaxDeg) c.elevDeg = kFreeCamElevMaxDeg;
}

// Start from the original camera: same horizontal direction (camera +0xa0), eye at the pivot height (elev 0) like the
// original's eye.
inline void freeCameraSync(const float dir[3], uint32_t serial) {
    FreeCamera& c = freeCamera();
    if (dir[0] != 0.0f || dir[2] != 0.0f) c.yaw = std::atan2(dir[0], dir[2]);
    c.elevDeg = 0.0f;
    c.synced = true;
    c.syncSerial = serial;
    c.shownDist = -1.0f;
}

// Ground axes for FUN_0013cf90 in the camera +0x70 / +0x58 convention: back = horizontal target -> eye,
// right = normalize(worldUp x back) (FUN_0013e130 basis).
inline void freeCameraAxes(float back[3], float right[3]) {
    const FreeCamera& c = freeCamera();
    back[0] = std::sin(c.yaw);
    back[1] = 0.0f;
    back[2] = std::cos(c.yaw);
    right[0] = back[2];    // (0,1,0) x (bx,0,bz) = (bz, 0, -bx)
    right[1] = 0.0f;
    right[2] = -back[0];
}

// The presented camera matrix (with the wall sweep) is game/free_camera_view.hpp (Baroque-Native only).

}  // namespace baroque::game
