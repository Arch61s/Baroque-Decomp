#pragma once
// Gameplay camera task (RUNTIME.md 3, CAMERA_NATIVE.md). Task object allocated by FUN_0011d7e0, ctor FUN_0013e060,
// vtable 0x4cdb70 (+0xc = FUN_0010da30 tick wrapper -> +0x14 = FUN_0013e340 update). Pointer kept at 0x50b624
// (player controller 0x50b620 + 4) and, while it is the active camera, at 0x4e7b18.
//
// Everything the original read from other objects is passed in CameraWorld (documented with its address), and
// every call into another module is a hook in CameraHooks. Plain float math (presentation only), original
// operation order and constants.
#include <cstdint>
#include <functional>

#include "game/pad.hpp"

namespace baroque::game {

// Collision sweep FUN_00143150(from, delta, &radius) + FUN_001431e0() (see CAMERA_NATIVE.md "Raycast contract").
//   from      start point (world)
//   delta     sweep vector; the end point is from + delta (the original passes a displacement, not an end point)
//   radius    value of the float the 3rd argument points at (0x44b288/0x44b290/0x44b298 = 20.0, 0x44b2a8 = 5.0)
//   distOut   on hit: the nearest hit distance in world units measured from `from` along delta (global 0x50ba38,
//             what FUN_001431e0 returns). Not written / not read on a miss.
//   returns   true on hit (FUN_00140250(query, 1) == 1).
using CameraRaycast = std::function<bool(const float from[3], const float delta[3], float radius, float* distOut)>;

// Tunables rewritten by FUN_0013e100 (globals, .data defaults = dungeon preset).
struct CameraTunables {
    float lag = 50.0f;            // 0x44b268 max horizontal target->player distance
    float height = 185.0f;        // 0x44b270 target height above player position
    float dist = 300.0f;          // 0x44b278 boom length
    float defaultPitch = -15.0f;  // 0x44b280 degrees
};
// FUN_0013e100 argument sets used by the game (FUN_0011b3e0 / FUN_0011b620 / FUN_0011b810 / FUN_0013b0d0).
inline constexpr CameraTunables kCameraPresetDungeon{50.0f, 185.0f, 300.0f, -15.0f};   // 0x42480000.. 0xc1700000
inline constexpr CameraTunables kCameraPresetHub{30.0f, 145.0f, 400.0f, -6.0f};        // 0x41f00000.. 0xc0c00000
inline constexpr CameraTunables kCameraPresetFirstPerson{1.0f, 120.0f, 10.0f, -2.0f};  // 0x3f800000.. 0xc0000000

// Engine projection fields of 0x4e6640 (RUNTIME.md 1.5). The camera only touches them through FUN_00101b60.
struct EngineProjection {
    float fovDeg = 48.0f;   // +0x8c (0x4e66cc)
    float nearZ = 10.0f;    // +0x90 (0x4e66d0)
    float farZ = 1500.0f;   // +0x94 (0x4e66d4)
};
// orig FUN_00101b60 minus the FUN_0039e800 perspective-matrix build (renderer side).
void setProjection(EngineProjection& engine, float farZ, float fovDegOrMinus1);
// FUN_00101b60(far, fov) arguments per game state (fov -1 = keep).
struct ProjectionPreset { float farZ; float fovDeg; };
inline constexpr ProjectionPreset kProjectionBoot{1500.0f, -1.0f};        // FUN_00101940
inline constexpr ProjectionPreset kProjectionDungeon{70000.0f, 48.0f};    // FUN_0011b620
inline constexpr ProjectionPreset kProjectionTown{14000.0f, 45.0f};       // FUN_0011b3e0
inline constexpr ProjectionPreset kProjectionEventA{1500.0f, 45.0f};      // FUN_0011b810, FUN_0011c100()==1
inline constexpr ProjectionPreset kProjectionEventB{1500.0f, 48.0f};      // FUN_0011b810, otherwise
inline constexpr ProjectionPreset kProjectionSceneInit{1500.0f, 48.0f};   // FUN_0011bcb0

// External state the camera reads. The caller refreshes it before inputFromPad()/tick().
struct CameraWorld {
    bool playerPresent = true;          // iRam0050b144 != 0 (player object pointer; only FUN_0013f0b0 tests it)
    float playerPos[3] = {};            // *(player+0x20) +0xc/+0x10/+0x14  (transform object, position)
    float playerForward[3] = {0, 0, 1}; // *(player+0x20) +0x30/+0x34/+0x38 (transform row "forward", unit)
    int32_t eventState = 0;             // *(int*)0x51b620 (GameMain/event manager); 1 = event owns the camera
    bool gameActive = true;             // uRam0050a610 & 1
    uint32_t controlFlags = 0;          // *(uint*)0x50b620 player controller: 0x10 = player moved by stick this tick
                                        // (set FUN_0013cf90, cleared each FUN_0013b0d0), 0x20 = first-person mode
    int32_t controlRotLock = 0;         // *(int*)(0x50b620+8): non-zero = first-person stick-turn suppressed
    bool paused = false;                // FUN_00102020(0x4e6640): (engine.flags&1) || 0 < pauseFrames < 4
    bool lightFollowsCamera = false;    // FUN_0011c0a0(0x50a2a0)==1 || iRam0050a2a8==0x5b
    uint32_t optionFlags = 0;           // uRam0051af68: bit0 camera up/down reversed, bit1 left/right reversed
    bool holdingObject = false;         // FUN_0013a140(): iRam0050b600 != 0
    // first person only (0x50b620 & 0x20)
    bool fpLookValid = false;           // chain *(*(*(player+0x2c)+4)+0x58) -> +0x14 != 0 -> +0x18 -> +0xc != 0
    float fpLookVec[3] = {};            // the float[3] that chain ends at (FUN_0013f130)
    bool fpHeadValid = false;           // player+0x18 in {0,2} && *(*(*(*(player+0x2c)+8)+0xc)+4) == 0
    float fpHeadY = 0.0f;               // y of bone "Head" (string 0x44b2b0) world translation (FUN_0013e340)
    int32_t playerModelResetCount = 0;  // *(X+8), X = *(**(player+0x2c)+0xc) (FUN_0013f560 prologue)
};

// Calls the camera makes into other modules.
struct CameraHooks {
    CameraRaycast raycast;                                  // FUN_00143150 / FUN_001431e0
    std::function<void(float rad)> rotatePlayer;            // FUN_00128ae0(rad, player); must update
                                                            // CameraWorld::playerForward before it returns
    std::function<void()> requestScreenTransition;          // FUN_00177cc0(0, 0x51b620, 2)
    std::function<void(const float dir[3])> setLightDirection; // FUN_00108b70(0x4e78e0, uRam0050a2cc, dir)
    std::function<void()> markPlayerModelOnReset;           // FUN_0013f560: *(uint*)*(X+0x10) |= 1
};

struct Camera {
    // ---- task object fields (offsets from the task object) ----
    float camWorld[4][4] = {{1, 0, 0, 0}, {0, 1, 0, 0}, {0, 0, 1, 0}, {0, 0, 0, 1}};
                                    // +0x18 rows right/up/forward/position, v' = v*M (identity from FUN_0010d8f0)
    float right[3] = {};            // +0x58
    float up[3] = {};               // +0x64
    float forward[3] = {};          // +0x70 unit, from target toward eye (after pitch)
    float eye[3] = {};              // +0x7c
    float eyeOffset[3] = {};        // +0x88 shake offset, zeroed every tick (only used in first person)
    float target[3] = {};           // +0x94
    float dir[3] = {};              // +0xa0 horizontal unit vector target -> eye
    float playerFwdAtReset[3] = {}; // +0xac (x = 10000.0 marks a manual rotation since the last reset)
    float unusedB8 = 0.0f;          // +0xb8 (written 0, never read)
    float yawDelta = 0.0f;          // +0xbc stick yaw impulse (decays /1.5 per tick)
    float pitch = 0.0f;             // +0xc0 degrees
    float fpHeadPitch = 0.0f;       // +0xc4 eased first-person look pitch (radians)
    float pitchRad = 0.0f;          // +0xc8 final pitch, clamped +-0.43633232
    float fovOverride = -1.0f;      // +0xcc passed as FUN_00101b60 fov on reset (-1 = keep)
    uint32_t flags = 0;             // +0xd0 bit0 pitch input this tick, bit1 locked (cutscene)

    // ---- camera-only globals ----
    float autoYawHist[3] = {};      // 0x50b6e8, 0x50b6ec, 0x50b6f0
    float yawHist[3] = {};          // 0x50b6f8, 0x50b6fc, 0x50b700
    float boomHist[4] = {};         // 0x50b710, 0x50b714, 0x50b718, 0x50b71c
    float autoYawVel = 0.0f;        // 0x50b720
    CameraTunables tun{};           // 0x44b268..0x44b280

    // ---- port wiring ----
    CameraWorld* world = nullptr;
    EngineProjection* engine = nullptr;
    CameraHooks hooks{};
    bool isActiveCamera = true;     // *0x4e7b18 == this
    float viewMatrix[4][4] = {{1, 0, 0, 0}, {0, 1, 0, 0}, {0, 0, 1, 0}, {0, 0, 0, 1}}; // 0x4e7b30 = inverse(+0x18)

    // orig FUN_0013e060 (ctor): world/engine must outlive the camera.
    Camera(CameraWorld& w, EngineProjection& e, CameraHooks h);

    void init();                                   // FUN_0013e130
    void applyPreset(const CameraTunables& t);     // FUN_0013e100
    void tick();                                   // FUN_0010da30 (vtable +0xc): update() + view publish
    void update();                                 // FUN_0013e340 (vtable +0x14)
    void yawInput(float d);                        // FUN_0013efd0
    void pitchInput(float d);                      // FUN_0013eff0
    void requestReset();                           // FUN_0013f0b0
    void reset();                                  // FUN_0013f560
    void updatePitch();                            // FUN_0013f130
    void inputFromPad(const PadPort& port);        // FUN_0013d970 (port 0 = 0x4e6284)

    // PORT-ONLY (not original): free mouse look. addMouseDelta takes degrees. Yaw is applied directly to the boom
    // direction in FUN_0013e340's rotateY (no 4-tap smoothing / decay); pitch goes through FUN_0013eff0's clamps
    // and, once moved by the mouse, is held (the FUN_0013f130 return-to-default is skipped) until the next
    // reset/init. The gamepad path is unchanged.
    void addMouseDelta(float dxDeg, float dyDeg);
    void applyMouseInput();
    float mouseYawPending = 0.0f;
    float mousePitchPending = 0.0f;
    float mouseYawRad = 0.0f;
    float lastMouseYawApplied = 0.0f;   // PORT: yaw consumed by the latest tick (presentation catch-up)
    float pendingMouseYawRad() const { return (mouseYawPending * 3.1415927f) / 180.0f; }
    bool mousePitchHold = false;
    // PORT (presentation, PRESENTATION_PLAN.md 2.4): ++ at every hard placement (init FUN_0013e130, reset
    // FUN_0013f560) so the presenter cuts instead of interpolating across it. Never read by logic.
    uint32_t cutSerial = 0;

private:
    void placeBehindPlayer();       // shared body of FUN_0013e130 / FUN_0013f560
    void buildBasis();              // forward = dir; right = up0 x forward; up = forward x right
    void avoidWalls(int& avoid);    // FUN_0013e340 wall-avoid probes (label-less block, kept inline order)
    void writeMatrix();             // FUN_0013e340 LAB_0013ed6c
};

// Small helpers (exposed for the check).
void rotateY(float angle, float v[3]);    // orig FUN_00206be0
void normalize3(float v[3]);              // orig FUN_00206ab0
const float* worldUp();                   // orig FUN_00206f10
bool invert4x4(float out[4][4], const float in[4][4]);   // orig FUN_0039f1d0 (status != fail)

}  // namespace baroque::game
