// Gameplay camera task. See camera.hpp and work/reports/CAMERA_NATIVE.md.
// Operation order follows the R5900 disassembly where Ghidra reordered multiply-accumulate chains
// (mula/madda/madd): sums are written in the order the accumulator receives the products.
#include "game/camera.hpp"

#include <bit>
#include <cmath>
#include <cstdint>
#include <utility>

namespace baroque::game {

namespace {

// Raycast radius constants passed by address in FUN_0013e340 (.data).
constexpr float kAvoidProbeRadius = 20.0f;   // 0x44b288
constexpr float kAvoidLeftRadius = 20.0f;    // 0x44b290
constexpr float kAvoidRightRadius = 20.0f;   // 0x44b298
constexpr float kYawGain = 0.0175f;          // 0x44b2a0 (0x3c8f5c29)
constexpr float kBoomRadius = 5.0f;          // 0x44b2a8

// Angle constants passed as raw float bits.
const float kAngle30 = std::bit_cast<float>(0x3f060a92u);       //  0.5235988 (30 deg)
const float kAngleMinus30 = std::bit_cast<float>(0xbf060a92u);  // -0.5235988
const float kAngleMinus90 = std::bit_cast<float>(0xbfc90fdbu);  // -1.5707964 (&UNK_bfc90fdb)
const float kPitchRadMax = std::bit_cast<float>(0x3edf66f3u);   //  0.43633232 (25 deg)
const float kPitchRadMin = std::bit_cast<float>(0xbedf66f3u);   // -0.43633232
const float kManualRotationMarker = std::bit_cast<float>(0x461c4000u);  // 10000.0

// orig FUN_0039ddd0 (work/decompiled/0039ddd0.c) -> FUN_0039ded0 (work/decompiled/0039ded0.c):
// rotation of `angle` about unit `axis`, rows m[0..2] (translation row/column = identity, unused here).
// UNVERIFIED: sin/cos come from the game's libm (FUN_002386a0 / FUN_00238400); std::sin/std::cos may differ in
// the last ulp.
void axisAngleMatrix(float angle, const float axis[3], float m[3][3]) {
    const float s = std::sin(angle);   // FUN_002386a0
    const float c = std::cos(angle);   // FUN_00238400
    const float omc = 1.0f - c;
    const float ax = axis[0];
    const float ay = axis[1];
    const float az = axis[2];
    const float zero = 0.0f;
    float t = omc * ax;
    m[0][0] = (zero + c) + t * ax;
    m[0][1] = (zero + s * az) + t * ay;
    m[0][2] = t * az - s * ay;
    t = omc * ay;
    m[1][0] = t * ax - s * az;
    m[1][1] = (zero + c) + t * ay;
    m[1][2] = (zero + s * ax) + t * az;
    t = omc * az;
    m[2][0] = (zero + s * ay) + t * ax;
    m[2][1] = t * ay - s * ax;
    m[2][2] = (zero + c) + t * az;
}

// orig FUN_003a0330 (work/decompiled/003a0330.c): out = v * M (row vector, 3x3 part). out may alias v.
void transformVector(float out[3], const float v[3], const float m[3][3]) {
    const float x = v[0];
    const float y = v[1];
    const float z = v[2];
    out[0] = y * m[1][0] + x * m[0][0] + z * m[2][0];
    out[1] = y * m[1][1] + x * m[0][1] + z * m[2][1];
    out[2] = y * m[1][2] + x * m[0][2] + z * m[2][2];
}

void copy3(float dst[3], const float src[3]) {
    dst[0] = src[0];
    dst[1] = src[1];
    dst[2] = src[2];
}

}  // namespace

// ---------------------------------------------------------------------------------------------------------------
// helpers

// orig FUN_00206be0 (work/decompiled/00206be0.c)
void rotateY(float angle, float v[3]) {
    const float s = std::sin(angle);   // FUN_002386a0
    const float c = std::cos(angle);   // FUN_00238400
    const float x = v[0];
    const float z = v[2];
    v[0] = z * s + x * c;
    v[2] = z * c + x * -s;
}

// orig FUN_00206ab0 (work/decompiled/00206ab0.c)
void normalize3(float v[3]) {
    const float sq = v[1] * v[1] + v[0] * v[0] + v[2] * v[2];
    float len = 0.0f;
    if (sq != 0.0f) {
        len = std::sqrt(sq);
    }
    if (len == 0.0f) {
        v[2] = 0.0f;
        v[1] = 0.0f;
        v[0] = 0.0f;
    } else {
        v[0] = v[0] / len;
        v[1] = v[1] / len;
        v[2] = v[2] / len;
    }
}

// orig FUN_00206f10 (work/decompiled/00206f10.c): lazily initialised static (0,1,0) at 0x53eab0.
const float* worldUp() {
    static bool initialised = false;   // cRam004e5e28
    static float up[3];                // 0x53eab0
    if (!initialised) {
        up[0] = 0.0f;
        up[1] = 1.0f;
        up[2] = 0.0f;
        initialised = true;
    }
    return up;
}

// orig FUN_0039f1d0 (work/decompiled/0039f1d0.c): out = adj(in) * (1/det); fails (out untouched) when
// |det| < 1.1754944e-38.
// UNVERIFIED: the adjugate (FUN_0039f2d0) and determinant (FUN_0039f4f0) are not translated op-for-op; this is the
// standard cofactor expansion.
bool invert4x4(float out[4][4], const float in[4][4]) {
    const float* a = &in[0][0];
    float inv[16];
    inv[0] = a[5] * a[10] * a[15] - a[5] * a[11] * a[14] - a[9] * a[6] * a[15] + a[9] * a[7] * a[14] +
             a[13] * a[6] * a[11] - a[13] * a[7] * a[10];
    inv[4] = -a[4] * a[10] * a[15] + a[4] * a[11] * a[14] + a[8] * a[6] * a[15] - a[8] * a[7] * a[14] -
             a[12] * a[6] * a[11] + a[12] * a[7] * a[10];
    inv[8] = a[4] * a[9] * a[15] - a[4] * a[11] * a[13] - a[8] * a[5] * a[15] + a[8] * a[7] * a[13] +
             a[12] * a[5] * a[11] - a[12] * a[7] * a[9];
    inv[12] = -a[4] * a[9] * a[14] + a[4] * a[10] * a[13] + a[8] * a[5] * a[14] - a[8] * a[6] * a[13] -
              a[12] * a[5] * a[10] + a[12] * a[6] * a[9];
    inv[1] = -a[1] * a[10] * a[15] + a[1] * a[11] * a[14] + a[9] * a[2] * a[15] - a[9] * a[3] * a[14] -
             a[13] * a[2] * a[11] + a[13] * a[3] * a[10];
    inv[5] = a[0] * a[10] * a[15] - a[0] * a[11] * a[14] - a[8] * a[2] * a[15] + a[8] * a[3] * a[14] +
             a[12] * a[2] * a[11] - a[12] * a[3] * a[10];
    inv[9] = -a[0] * a[9] * a[15] + a[0] * a[11] * a[13] + a[8] * a[1] * a[15] - a[8] * a[3] * a[13] -
             a[12] * a[1] * a[11] + a[12] * a[3] * a[9];
    inv[13] = a[0] * a[9] * a[14] - a[0] * a[10] * a[13] - a[8] * a[1] * a[14] + a[8] * a[2] * a[13] +
              a[12] * a[1] * a[10] - a[12] * a[2] * a[9];
    inv[2] = a[1] * a[6] * a[15] - a[1] * a[7] * a[14] - a[5] * a[2] * a[15] + a[5] * a[3] * a[14] +
             a[13] * a[2] * a[7] - a[13] * a[3] * a[6];
    inv[6] = -a[0] * a[6] * a[15] + a[0] * a[7] * a[14] + a[4] * a[2] * a[15] - a[4] * a[3] * a[14] -
             a[12] * a[2] * a[7] + a[12] * a[3] * a[6];
    inv[10] = a[0] * a[5] * a[15] - a[0] * a[7] * a[13] - a[4] * a[1] * a[15] + a[4] * a[3] * a[13] +
              a[12] * a[1] * a[7] - a[12] * a[3] * a[5];
    inv[14] = -a[0] * a[5] * a[14] + a[0] * a[6] * a[13] + a[4] * a[1] * a[14] - a[4] * a[2] * a[13] -
              a[12] * a[1] * a[6] + a[12] * a[2] * a[5];
    inv[3] = -a[1] * a[6] * a[11] + a[1] * a[7] * a[10] + a[5] * a[2] * a[11] - a[5] * a[3] * a[10] -
             a[9] * a[2] * a[7] + a[9] * a[3] * a[6];
    inv[7] = a[0] * a[6] * a[11] - a[0] * a[7] * a[10] - a[4] * a[2] * a[11] + a[4] * a[3] * a[10] +
             a[8] * a[2] * a[7] - a[8] * a[3] * a[6];
    inv[11] = -a[0] * a[5] * a[11] + a[0] * a[7] * a[9] + a[4] * a[1] * a[11] - a[4] * a[3] * a[9] -
              a[8] * a[1] * a[7] + a[8] * a[3] * a[5];
    inv[15] = a[0] * a[5] * a[10] - a[0] * a[6] * a[9] - a[4] * a[1] * a[10] + a[4] * a[2] * a[9] +
              a[8] * a[1] * a[6] - a[8] * a[2] * a[5];
    const float det = a[0] * inv[0] + a[1] * inv[4] + a[2] * inv[8] + a[3] * inv[12];
    if (std::fabs(det) < 1.1754944e-38f) {
        return false;   // *status = DAT_004afb68
    }
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            out[i][j] = (1.0f / det) * inv[i * 4 + j];
        }
    }
    return true;        // *status = DAT_004afb60
}

// orig FUN_00101b60 (work/decompiled/00101b60.c). The FUN_0039e800(fov*0.017453292, -0.017453292 (0xbc8efa35),
// 4/3 (0x3faaaaab), near, far, &engine+0xc) perspective build belongs to the renderer.
void setProjection(EngineProjection& engine, float farZ, float fovDegOrMinus1) {
    if (fovDegOrMinus1 != -1.0f) {
        engine.fovDeg = fovDegOrMinus1;
    }
    engine.nearZ = 10.0f;   // 0x41200000
    engine.farZ = farZ;
}

// ---------------------------------------------------------------------------------------------------------------
// construction / presets / reset

// orig FUN_0013e060 (work/decompiled/0013e060.c): base ctor FUN_0010d8f0 sets cam+0x18 and 0x4e7b30 to identity
// (FUN_0039d2e0), vtable 0x4cdb70, then FUN_0013e130.
Camera::Camera(CameraWorld& w, EngineProjection& e, CameraHooks h) : world(&w), engine(&e), hooks(std::move(h)) {
    init();
}

// Shared body of FUN_0013e130 / FUN_0013f560: target above the player, eye `dist` behind the player's forward.
void Camera::placeBehindPlayer() {
    const CameraWorld& w = *world;
    target[0] = w.playerPos[0];
    target[1] = tun.height + w.playerPos[1];
    target[2] = w.playerPos[2];
    const float fz = w.playerForward[2] * tun.dist;
    const float fy = w.playerForward[1] * tun.dist;
    eye[0] = (target[0] + 0.0f) - w.playerForward[0] * tun.dist;
    eye[1] = (target[1] + 0.0f) - fy;
    eye[2] = (target[2] + 0.0f) - fz;
    dir[0] = eye[0] - target[0];
    dir[2] = eye[2] - target[2];
    dir[1] = 0.0f;
    normalize3(dir);
    buildBasis();
    unusedB8 = 0.0f;
    yawDelta = 0.0f;
    pitch = tun.defaultPitch;
    mousePitchHold = false;
    mouseYawRad = 0.0f;
    fpHeadPitch = 0.0f;
    pitchRad = pitch;
}

// forward = dir; right = normalize(worldUp x forward) with y forced 0; up = normalize(forward x right).
// (inline block of FUN_0013e130 / FUN_0013f560 / FUN_0013e340)
void Camera::buildBasis() {
    forward[0] = dir[0];
    forward[1] = dir[1];
    forward[2] = dir[2];
    const float* u = worldUp();
    right[0] = u[1] * forward[2] - u[2] * forward[1];
    right[1] = u[2] * forward[0] - u[0] * forward[2];
    right[2] = u[0] * forward[1] - u[1] * forward[0];
    right[1] = 0.0f;
    normalize3(right);
    up[0] = forward[1] * right[2] - forward[2] * right[1];
    up[1] = forward[2] * right[0] - forward[0] * right[2];
    up[2] = forward[0] * right[1] - forward[1] * right[0];
    normalize3(up);
}

// orig FUN_0013e130 (work/decompiled/0013e130.c)
void Camera::init() {
    ++cutSerial;   // PORT: presentation cut marker
    placeBehindPlayer();
    fovOverride = std::bit_cast<float>(0xbf800000u);   // -1.0
    flags = 0;
}

// orig FUN_0013e100 (work/decompiled/0013e100.c): rewrites the four globals, then tail-calls FUN_0013e130
// (`j func_13E130` with a0 = camera, verified in the disassembly at 0x13e11c).
void Camera::applyPreset(const CameraTunables& t) {
    tun.lag = t.lag;
    tun.height = t.height;
    tun.dist = t.dist;
    tun.defaultPitch = t.defaultPitch;
    init();
}

// orig FUN_0013f560 (work/decompiled/0013f560.c)
void Camera::reset() {
    CameraWorld& w = *world;
    // X = *(**(player+0x2c)+0xc); if (*(X+8) != 0 && *(X+8) >= 1) *(uint*)*(X+0x10) |= 1
    // UNVERIFIED: meaning of that player-model flag; forwarded to the hook.
    if (w.playerModelResetCount != 0) {
        if (!(w.playerModelResetCount < 1)) {
            if (hooks.markPlayerModelOnReset) hooks.markPlayerModelOnReset();
        }
    }
    ++cutSerial;   // PORT: presentation cut marker
    placeBehindPlayer();
    flags = 0;
    setProjection(*engine, engine->farZ, fovOverride);   // FUN_00101b60(uRam004e66d4, cam+0xcc, 0x4e6640)
    fovOverride = std::bit_cast<float>(0xbf800000u);     // -1.0
    playerFwdAtReset[0] = w.playerForward[0];
    playerFwdAtReset[1] = w.playerForward[1];
    playerFwdAtReset[2] = w.playerForward[2];
    if (hooks.requestScreenTransition) hooks.requestScreenTransition();   // FUN_00177cc0(0, 0x51b620, 2)
}

// ---------------------------------------------------------------------------------------------------------------
// input

// orig FUN_0013efd0 (work/decompiled/0013efd0.c)
void Camera::yawInput(float d) {
    yawDelta = yawDelta + d;
    playerFwdAtReset[0] = kManualRotationMarker;
}

// orig FUN_0013eff0 (work/decompiled/0013eff0.c)
void Camera::pitchInput(float d) {
    pitch = pitch + d;
    if ((world->controlFlags & 0x20) == 0) {
        if (5.0f < pitch) {
            pitch = std::bit_cast<float>(0x40a00000u);    // 5.0
        } else if (pitch < -45.0f) {
            pitch = std::bit_cast<float>(0xc2340000u);    // -45.0
        }
    } else if (25.0f < pitch) {
        pitch = std::bit_cast<float>(0x41c80000u);        // 25.0
    } else if (!(-25.0f <= pitch)) {
        pitch = std::bit_cast<float>(0xc1c80000u);        // -25.0
    }
    flags = flags | 1;                                    // LAB_0013f090
    playerFwdAtReset[0] = kManualRotationMarker;
}

// orig FUN_0013f0b0 (work/decompiled/0013f0b0.c): R3 resets only when not locked and the player has turned (or a
// manual rotation set the 10000.0 marker) since the last reset.
void Camera::requestReset() {
    const CameraWorld& w = *world;
    if ((flags & 2) == 0 && w.playerPresent &&
        (playerFwdAtReset[0] != w.playerForward[0] || playerFwdAtReset[1] != w.playerForward[1] ||
         playerFwdAtReset[2] != w.playerForward[2])) {
        reset();
    }
}

// orig FUN_0013d970 (work/decompiled/0013d970.c), checked against the disassembly 0x13d970..0x13ddf4.
// Called by the player-controller task FUN_0013b0d0(0x50b620) with param_1 = 0x50b620 (so *param_1 & 0x20 is the
// first-person flag, param_1[1] = this camera, param_1[2] = controlRotLock). Reads pad port 0: rstick 0x4e62c4,
// lstick 0x4e62d0, pressed 0x4e6288.
void Camera::inputFromPad(const PadPort& pad) {
    CameraWorld& w = *world;
    const bool reverseLR = (w.optionFlags & 2) != 0;   // FUN_0015dc60(1)
    const bool reverseUD = (w.optionFlags & 1) != 0;   // FUN_0015dc60(0)
    if ((w.controlFlags & 0x20) == 0) {
        float ax = pad.rstick[0];
        if (ax < 0.0f) ax = -ax;
        float ay = pad.rstick[1];
        if (ay < 0.0f) ay = -ay;
        if (0.5f < ax) {
            float d = ((ax - 0.5f) / 0.5f) * (2.0f * pad.rstick[2]);
            if (!reverseLR) {
                if (pad.rstick[0] < 0.0f) d = -d;
            } else {
                if (0.0f <= pad.rstick[0]) d = -d;
            }
            yawInput(d);
        } else if (0.5f < ay) {
            float d = ((ay - 0.5f) / 0.5f) * pad.rstick[2];
            if (!reverseUD) {
                if (0.0f <= pad.rstick[1]) d = -d;
            } else {
                if (pad.rstick[1] < 0.0f) d = -d;
            }
            pitchInput(d);
        } else if ((pad.pressed & 0x400) != 0) {   // R3
            requestReset();
        }
        return;
    }
    // First person (RUNTIME.md 2.5 calls this the aim/throw mode; the flag is 0x50b620 & 0x20 = first person).
    bool leftStick = false;          // a1 (zeroed in the delay slot at 0x13d9c8)
    float sx = pad.rstick[0];        // f3
    float mag = pad.rstick[2];       // f2
    if (w.holdingObject && pad.lstick[0] != 0.0f) {   // FUN_0013a140()
        leftStick = true;
        sx = -pad.lstick[0];
        mag = pad.lstick[2];
    }
    float ax = sx;
    if (sx < 0.0f) ax = -sx;
    float ay = pad.rstick[1];
    if (ay < 0.0f) ay = -ay;
    if (0.5f < ax && w.controlRotLock == 0) {
        float d = ((ax - 0.5f) / 0.5f) * (4.0f * mag);
        if (sx < 0.0f) d = -d;
        if (hooks.rotatePlayer) {
            if (leftStick) {
                hooks.rotatePlayer((3.1415927f * d) / 180.0f);           // FUN_00128ae0
            } else if (!reverseLR) {
                hooks.rotatePlayer((3.1415927f * d) / 180.0f);
            } else {
                hooks.rotatePlayer((3.1415927f * -d) / 180.0f);
            }
        }
    }
    float t[3];
    copy3(t, forward);
    rotateY(kAngleMinus90, t);
    const float* pf = w.playerForward;   // re-read after FUN_00128ae0
    yawInput(pf[1] * t[1] + pf[0] * t[0] + pf[2] * t[2]);
    if (0.5f < ay) {
        float d = ((ay - 0.5f) / 0.5f) * pad.rstick[2];
        if (!reverseUD) {
            if (0.0f <= pad.rstick[1]) d = -d;
        } else {
            if (pad.rstick[1] < 0.0f) d = -d;
        }
        pitchInput(d);
    }
}

// PORT-ONLY: mouse look (RUNTIME.md 3 "Mouse camera for the port"). Same sign convention as the right stick
// (x right positive, y down positive): x feeds FUN_0013efd0 unchanged, y is negated like the non-reversed stick
// before FUN_0013eff0. The camera reverse options (0x51af68) are not applied to the mouse.
void Camera::addMouseDelta(float dxDeg, float dyDeg) {
    mouseYawPending += dxDeg;
    mousePitchPending += dyDeg;
}

void Camera::applyMouseInput() {
    if (mouseYawPending != 0.0f) {
        mouseYawRad += (mouseYawPending * 3.1415927f) / 180.0f;
        playerFwdAtReset[0] = kManualRotationMarker;   // as FUN_0013efd0 (enables R3 reset)
    }
    if (mousePitchPending != 0.0f) {
        pitchInput(-mousePitchPending);
        mousePitchHold = true;
    }
    mouseYawPending = 0.0f;
    mousePitchPending = 0.0f;
}

// ---------------------------------------------------------------------------------------------------------------
// per tick

// orig FUN_0010da30 (work/decompiled/0010da30.c): vtable+0xc of 0x4cdb70; calls vtable+0x14 (FUN_0013e340) and,
// when this is the active camera (*0x4e7b18), inverts cam+0x18 into the view matrix 0x4e7b30.
void Camera::tick() {
    update();
    if (isActiveCamera) {
        invert4x4(viewMatrix, camWorld);
    }
}

// Wall-avoid probes of FUN_0013e340 (only when the player moved this tick, 0x50b620 & 0x10).
void Camera::avoidWalls(int& avoid) {
    const CameraWorld& w = *world;
    dir[0] = eye[0] - target[0];
    dir[2] = eye[2] - target[2];
    dir[1] = 0.0f;
    normalize3(dir);
    float head[3] = {w.playerPos[0], w.playerPos[1], w.playerPos[2]};   // fStack_10..8
    head[1] = eye[1] + 20.0f;
    if (yawDelta < 0.01f && !(yawDelta <= -0.01f)) {
        float v[3];        // fStack_40 (normalised)
        float side[3];     // fStack_30 (raw)
        float probe[3];    // fStack_20 (raw)
        v[0] = eye[0] - head[0];
        v[1] = head[1] - head[1];   // sub.s f1, f2, f2 at 0x13e5cc: always 0
        v[2] = eye[2] - head[2];
        copy3(side, v);
        copy3(probe, v);
        normalize3(v);
        float len = 0.0f;
        const float sq = probe[1] * probe[1] + probe[0] * probe[0] + probe[2] * probe[2];
        if (sq != 0.0f) {
            len = std::sqrt(sq);
        }
        len = len / 1.5f;
        probe[0] = v[0] * len;
        probe[1] = v[1] * len;
        probe[2] = v[2] * len;
        float unused = 0.0f;
        if (hooks.raycast && hooks.raycast(head, probe, kAvoidProbeRadius, &unused)) {
            float distLeft = 0.0f;    // f22 (uninitialised in the original; only read when both probes hit)
            float distRight = 0.0f;   // f21
            copy3(probe, side);
            rotateY(kAngle30, probe);
            const bool hitLeft = hooks.raycast(head, probe, kAvoidLeftRadius, &distLeft);
            rotateY(kAngleMinus30, side);
            const bool hitRight = hooks.raycast(head, side, kAvoidRightRadius, &distRight);
            if (!hitLeft || !hitRight) {
                if (!hitLeft) {
                    if (hitRight) avoid = 1;
                } else {
                    avoid = -1;
                }
            } else {
                const float diff = std::fabs(distRight - distLeft);
                if (50.0f <= diff) {
                    if (distLeft < distRight) {
                        avoid = -1;
                    } else if (distRight < distLeft) {
                        avoid = 1;
                    }
                }
            }
        }
    }
}

// orig FUN_0013e340 (work/decompiled/0013e340.c), checked against the disassembly 0x13e340..0x13efcc.
void Camera::update() {
    CameraWorld& w = *world;
    if (w.eventState == 1) {
        return;
    }
    if ((flags & 2) != 0) {
        writeMatrix();   // goto LAB_0013ed6c
        return;
    }
    if (w.gameActive) {
        const float q = 0.25f;
        if ((w.controlFlags & 0x20) != 0) {
            eye[0] = w.playerPos[0];
            eye[2] = w.playerPos[2];
            target[0] = (eye[0] + 0.0f) - dir[0] * tun.dist;
            target[2] = (eye[2] + 0.0f) - dir[2] * tun.dist;
        }
        float distPrev = 0.0f;   // f20
        const float bz = target[2] - eye[2];
        const float dz = w.playerPos[2] - target[2];
        const float dx = w.playerPos[0] - target[0];
        const float bx = target[0] - eye[0];
        const float by = target[1] - eye[1];
        const float lagSq = (0.0f + 0.0f) + dx * dx + dz * dz;
        const float boomSq = by * by + bx * bx + bz * bz;
        if (boomSq != 0.0f) {
            distPrev = std::sqrt(boomSq);
        }
        if (tun.lag * tun.lag < lagSq) {
            const float r = std::sqrt(lagSq);
            const float excess = r - tun.lag;
            target[0] = target[0] + (dx / r) * excess;
            target[2] = target[2] + (dz / r) * excess;
        }
        target[1] = tun.height + w.playerPos[1];

        int avoid = 0;   // s0
        if ((w.controlFlags & 0x10) != 0) {
            avoidWalls(avoid);
        }
        if (avoid == 0) {
            autoYawVel = autoYawVel / 1.5f;
            if (0.0f < autoYawVel && autoYawVel < 0.075f) {
                autoYawVel = 0.0f;
            }
            if (autoYawVel < 0.0f && -0.075f < autoYawVel) {
                autoYawVel = 0.0f;
            }
        } else if (avoid < 1) {
            if (0.0f < autoYawVel) {
                autoYawVel = 0.0f;
                autoYawHist[0] = 0.0f;
                autoYawHist[1] = 0.0f;
                autoYawHist[2] = 0.0f;
            }
            if (-4.0f < autoYawVel) {
                autoYawVel = autoYawVel - 0.5f;
            }
        } else {
            if (autoYawVel < 0.0f) {
                autoYawVel = 0.0f;
                autoYawHist[0] = 0.0f;
                autoYawHist[1] = 0.0f;
                autoYawHist[2] = 0.0f;
            }
            if (autoYawVel < 4.0f) {
                autoYawVel = autoYawVel + 0.5f;
            }
        }

        if ((w.controlFlags & 0x20) == 0) {
            if (!w.paused) {   // FUN_00102020(0x4e6640) == 0
                const float autoAvg = q * autoYawHist[0] + q * autoYawVel + q * autoYawHist[1] + q * autoYawHist[2];
                const float userAvg = q * yawHist[0] + q * yawDelta + q * yawHist[1] + q * yawHist[2];
                rotateY(userAvg * kYawGain + 0.015f * autoAvg + mouseYawRad, dir);  // + port-only mouse yaw
                lastMouseYawApplied = mouseYawRad;   // PORT
                mouseYawRad = 0.0f;
                autoYawHist[2] = autoYawHist[1];
                autoYawHist[1] = autoYawHist[0];
                autoYawHist[0] = autoYawVel;
                yawHist[2] = yawHist[1];
                yawHist[1] = yawHist[0];
                yawHist[0] = yawDelta;
                yawDelta = yawDelta / 1.5f;
                if (!(yawDelta <= 0.0f)) {
                    if (yawDelta < 0.075f) {
                        yawDelta = 0.0f;   // goto LAB_0013ea94
                    }
                }
                if (yawDelta < 0.0f && !(yawDelta <= -0.075f)) {
                    yawDelta = 0.0f;
                }
            }
        } else {
            // First person: yawDelta is used directly as radians (0x13e94c). Here it is the sine-like dot product
            // fed by FUN_0013d970's first-person branch, so the view tracks the player's forward.
            rotateY(yawDelta, dir);
            yawDelta = 0.0f;
        }
        // LAB_0013eacc
        eye[0] = (0.0f + target[0]) + dir[0] * distPrev;
        eye[2] = (0.0f + target[2]) + dir[2] * distPrev;
        eye[1] = target[1];

        // collision: sweep from target toward eye, length dist, radius 5
        float v[3] = {eye[0] - target[0], eye[1] - target[1], eye[2] - target[2]};   // fStack_60
        normalize3(v);
        float sweep[3] = {v[0] * tun.dist, v[1] * tun.dist, v[2] * tun.dist};          // fStack_50
        float hitDist = 0.0f;
        const bool hit = hooks.raycast && hooks.raycast(target, sweep, kBoomRadius, &hitDist);
        boomHist[3] = boomHist[2];
        boomHist[2] = boomHist[1];
        boomHist[1] = boomHist[0];
        if (!hit) {
            boomHist[0] = tun.dist;
        } else {
            boomHist[0] = hitDist;   // FUN_001431e0()
            if (boomHist[0] < 1.0f) {
                boomHist[0] = 1.0f;
            }
        }
        float d = q * boomHist[1] + q * boomHist[0] + q * boomHist[2] + q * boomHist[3];
        if (!(d <= boomHist[0])) {
            d = boomHist[0];
        }
        eye[0] = (0.0f + target[0]) + v[0] * d;
        eye[1] = (0.0f + target[1]) + v[1] * d;
        eye[2] = (0.0f + target[2]) + v[2] * d;

        buildBasis();
        updatePitch();
    }
    writeMatrix();
}

// FUN_0013e340 LAB_0013ed6c: publish cam+0x18, clear the shake offset, optional light direction.
void Camera::writeMatrix() {
    CameraWorld& w = *world;
    camWorld[0][0] = right[0];
    camWorld[0][1] = right[1];
    camWorld[0][2] = right[2];
    camWorld[1][0] = up[0];
    camWorld[1][1] = up[1];
    camWorld[1][2] = up[2];
    camWorld[2][0] = forward[0];
    camWorld[2][1] = forward[1];
    camWorld[2][2] = forward[2];
    if ((w.controlFlags & 0x20) == 0) {
        camWorld[3][0] = eye[0];
        camWorld[3][1] = eye[1];
        camWorld[3][2] = eye[2];
    } else if (w.fpHeadValid) {
        camWorld[3][0] = eye[0];
        camWorld[3][1] = 10.0f + w.fpHeadY;   // bone "Head" (0x44b2b0) world y + 10
        camWorld[3][2] = eye[2];
    } else {
        camWorld[3][0] = eye[0] + eyeOffset[0];
        camWorld[3][1] = eye[1] + eyeOffset[1];
        camWorld[3][2] = eye[2] + eyeOffset[2];
    }
    eyeOffset[2] = 0.0f;
    eyeOffset[1] = 0.0f;
    eyeOffset[0] = 0.0f;
    if (w.lightFollowsCamera) {
        const float l[3] = {-forward[0], -forward[1], -forward[2]};
        if (hooks.setLightDirection) hooks.setLightDirection(l);   // FUN_00108b70(0x4e78e0, uRam0050a2cc, l)
    }
}

// orig FUN_0013f130 (work/decompiled/0013f130.c), easing order checked against 0x13f2a0..0x13f420.
void Camera::updatePitch() {
    const CameraWorld& w = *world;
    if (!w.paused) {   // FUN_00102020(0x4e6640) == 0
        if ((flags & 1) == 0 && !mousePitchHold) {   // mousePitchHold: port-only
            const float p = pitch;
            if (tun.defaultPitch < p) {
                pitch = p - 2.0f;
                if (pitch < tun.defaultPitch) pitch = tun.defaultPitch;
            } else if (p < tun.defaultPitch) {
                pitch = p + 2.0f;
                if (tun.defaultPitch < pitch) pitch = tun.defaultPitch;
            }
        }
        flags = flags & 0xfffffffe;
    } else {
        flags = flags & 0xfffffffe;
    }
    float m[3][3];
    if (!w.gameActive) {
        axisAngleMatrix((pitch * 3.1415927f) / 180.0f, right, m);
        transformVector(up, up, m);
        transformVector(forward, forward, m);
        return;
    }
    float look = 0.0f;   // fVar6
    if ((w.controlFlags & 0x20) != 0 && w.fpLookValid) {
        float t[3];
        copy3(t, right);
        rotateY(kAngleMinus90, t);
        const float* lv = w.fpLookVec;
        const float cur = fpHeadPitch;
        look = lv[1] * t[1] + lv[0] * t[0] + lv[2] * t[2];
        if (look != cur) {
            if (cur < look) {
                float e = look - cur;
                if (0.2f < e) {
                    e = e - 0.2f;
                    float k = 0.23f;
                    if (e <= 0.23f) k = e;
                    fpHeadPitch = fpHeadPitch + ((0.0f + 0.02f) + 0.05f * (0.23f - k));
                } else {
                    fpHeadPitch = (0.0f + cur) - 0.125f * (cur - look);
                }
            } else {
                const float e = cur - look;
                if (0.2f < e) {
                    float k = 0.23f;
                    if (e - 0.2f <= 0.23f) k = e - 0.2f;
                    fpHeadPitch = fpHeadPitch - ((0.0f + 0.02f) + 0.05f * (0.23f - k));
                } else {
                    fpHeadPitch = (0.0f + cur) - 0.125f * e;
                }
            }
            look = fpHeadPitch;
        }
    }
    const float r = (pitch * 3.1415927f) / 180.0f + look;
    pitchRad = r;
    if (0.43633232f < r) {
        pitchRad = kPitchRadMax;
    } else if (!(-0.43633232f <= r)) {
        pitchRad = kPitchRadMin;
    }
    axisAngleMatrix(pitchRad, right, m);   // LAB_0013f4b8
    transformVector(up, up, m);
    transformVector(forward, forward, m);
}

}  // namespace baroque::game
