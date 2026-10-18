#pragma once
// Player movement input and character orientation (PLAYER.md §3.4), translated from:
//   FUN_0013cf90 movement, FUN_0013d810 idle action, FUN_00128fc0 turn toward, FUN_00128ae0 rotate about up,
//   FUN_001287a0 snap forward, FUN_002069c0 acos (table 0x46ac40), FUN_0039ded0/FUN_0039ddd0 axis-angle matrix,
//   FUN_003a0330 vector x 3x3.
// Run inside an alchemy::ps2::Ps2FloatScope (the game tick opens it).
#include "game/collision.hpp"
#include "game/rng.hpp"

#include <cstdint>
#include <functional>
#include <string>

namespace baroque::game {

// Character transform object (chr+0x20): rows of the world matrix.
struct ChrTransform {
    Vec3f pos{};                       // +0x0c
    Vec3f right{1.0f, 0.0f, 0.0f};     // +0x18
    Vec3f up{0.0f, 1.0f, 0.0f};        // +0x24
    Vec3f fwd{0.0f, 0.0f, 1.0f};       // +0x30
};

float acosTable(float x);                                             // FUN_002069c0
void snapForward(ChrTransform& t, const Vec3f& dir);                  // FUN_001287a0
void rotateAboutUp(ChrTransform& t, float rad);                       // FUN_00128ae0
void rotateAboutAxis(ChrTransform& t, float rad, const Vec3f& axis);  // FUN_00128e90
void resetAxes(ChrTransform& t);                                      // FUN_001286c0
bool turnToward(float maxDeg, ChrTransform& t, const Vec3f& dir, bool scaled);  // FUN_00128fc0

struct Chr;      // game/character/character.hpp
struct Camera;   // game/camera.hpp

// A 3-D sprite object the player controller writes into (FUN_001051d0-initialised sprite; the allocation chain
// FUN_00107170/FUN_00107130/FUN_00113c60/FUN_00114c30/FUN_00107070 and the drawing are the renderer's side).
// `live` stands for "pointer non-null".
struct ControlSprite {
    bool live = false;
    Vec3f pos{};             // sprite +0x1c/+0x20/+0x24
    Vec3f rotDeg{};          // FUN_00105240(sprite, &rot) (degrees)
    uint32_t color = 0;      // sprite +0x30 (ABGR)
    uint16_t flags3a = 0;    // sprite +0x3a (bit0 set by FUN_0013b0d0)
};

// Player-control object 0x50b620 (PLAYER.md §1.5) and the controller statics next to it.
struct PlayerControl {
    uint32_t flags = 0;       // +0x00 bit1 aim cancelled (circle), bit4 0x10 moved this tick, bit5 0x20 first-person
                              //       view, 0x10000 no input (dead/drain), 0x10000000/0x20000000 pad vibration
    Camera* camera = nullptr; // +0x04 (0x50b624) gameplay camera task
    uint32_t lock = 0;        // +0x08 nonzero = locked/strafe movement (the lock-on target word read as a flag by
                              //       FUN_0013cf90 and the camera). Port: kept in sync with `target` by setLockTarget.
    Chr* target = nullptr;    // +0x08 the same word as a pointer: lock-on target character
    uint32_t targetSerial = 0;// +0x0c target serial (chr +0x0c) when it was locked
    ControlSprite lockMarker; // +0x10 lock-on marker sprite (tex 0x129)
    int32_t attacked = 0;     // +0x14 set 1 by FUN_0013c4e0
    int32_t combo = 0;        // +0x18 ticks in an "AT" action, 0..6
    uint32_t counter = 0;     // +0x1c charge: tick counter 0..200, 0x10000000 charging (AT05), 0x20000000 release
                              //       pending, 0x80000000 released. Low 12 bits read by FUN_0013cf90 (FP + "AT").
    float aimPitch = 0.0f;    // +0x20 throw aim pitch, degrees (+-60)
    Vec3f aimDir{};           // +0x24 throw aim direction (0x50b644)
    int32_t inputLock = 0;    // +0x30 2 while the HitStagger state is current, then counts down (FUN_0013c790 gate)
    int32_t aimPhase = 0;     // +0x34 aim animation ping-pong phase
    float aimSpeed = 0.0f;    // +0x38 aim animation speed (+-0.4)
    int32_t mapShown = 0;     // +0x3c floor map shown by SELECT
    uint32_t fpCache = 0;     // +0x48 last first-person option value (FUN_0015dcb0)
    // Confusion state (statics 0x50b6c0..0x50b6e0)
    uint32_t confPadMask = 0; // 0x50b6c0
    int confTimer = 0;        // 0x50b6c8
    Vec3f confDir{};          // 0x50b6d0
    uint32_t confDpad = 0;    // 0x50b6e0
    // Other controller statics
    bool aimMarker = false;   // 0x50b600 throw-trajectory sprite pointer non-null (FUN_0013a140 = "aiming")
    float aimArcVerts[60][3] = {};   // 0x50b240 throw-trajectory sprite vertices (world space, FUN_0013a150)
    uint32_t aimArcColours[60] = {}; // 0x50b510 per-vertex colours (FUN_001051d0 a3, flag bit 0 clear)
    bool aimArcValid = false;        // PORT: the two arrays hold a FUN_0013a150 result of the current sprite
    int holeTicks = 0;        // 0x50b678 floor-hole edge counter
    int holeAssistTicks = 0;  // PORT: walkway-end assist counter
    float markerBob = 0.0f;   // 0x50b6b0 lock-on marker bob offset
    float markerSpin = 0.0f;  // 0x50b6b8 lock-on marker spin (degrees)
};

// Writes the +0x08 word in both of its port representations.
inline void setLockTarget(PlayerControl& ctl, Chr* c) {
    ctl.target = c;
    ctl.lock = c != nullptr ? 1u : 0u;
}

// Everything FUN_0013cf90 reads from other objects, with original addresses.
struct MoveInputs {
    float stickX = 0, stickY = 0, stickMag = 0;  // 0x4e62d0 / 0x4e62d4 / 0x4e62d8 (left stick)
    uint32_t held = 0;                            // 0x4e6284 (pad held bits)
    Vec3f camFwd{};                               // camera +0x70
    Vec3f camRight{};                             // camera +0x58
    bool eventCamera = false;                     // 0x53c6e8 && 0x53c720 (event camera overrides the axes)
    Vec3f eventFwd{}, eventRight{};               // FUN_001a1d90 / FUN_001a1d80
    uint32_t statusMask = 0;                      // StatusBlock +0x3c
    int hidden0 = 0;                              // *(StatusBlock +0x58) hidden stat 0 (agility)
    int hp = 0;                                   // StatusBlock +0x04
    int maxHp = 0;                                // FUN_001343f0
    const char* currentAction = nullptr;          // FUN_00143d30(act, -1): 8-char name or null
    int weaponClass = -1;                         // FUN_001d7d10(0x53d910): suffix _S1/_S2/_S3/_G1, else _H1
};

struct MoveOutputs {
    std::function<void(const std::string& name8)> requestAction;  // FUN_00139f30(actPlayer, name)
};

// orig FUN_0039ddd0 -> FUN_0039ded0: 3x3 rotation about `axis` by `rad` into m[0..2], m[4..6], m[8..10].
void axisAngle(float m[12], float rad, const Vec3f& axis);
// orig FUN_003a0330: v * M (3x3 part of a row-major 4x4).
Vec3f mulVec3(const Vec3f& v, const float m[12]);

// orig FUN_0013a4b0: action suffix by weapon class.
const char* actionSuffix(int weaponClass);
// orig FUN_0013cf90 (work/decompiled/0013cf90.c). turnOnly = param_3 == 1.
void playerMovement(PlayerControl& ctl, ChrTransform& xf, Phys& phys, const MoveInputs& in, const MoveOutputs& out,
                    Rng& rng, bool turnOnly);
// orig FUN_0013d810 (work/decompiled/0013d810.c)
void playerIdle(Phys& phys, const MoveInputs& in, const MoveOutputs& out);

}  // namespace baroque::game
