// Player movement and orientation, translated (see player_move.hpp).
#include "game/player_move.hpp"

#include <bit>
#include <cmath>
#include <cstring>

#pragma fenv_access(on)

namespace baroque::game {

namespace {

constexpr uint32_t kAcosTable[512] = {
#include "game/acos_table.inc"
};

float dot3(const Vec3f& a, const Vec3f& b) { return a[2] * b[2] + a[0] * b[0] + a[1] * b[1]; }

// Action names read from the ELF (4 chars each).
constexpr const char* kMV02 = "MV02";  // DAT_0044b220 walk forward
constexpr const char* kMV03 = "MV03";  // DAT_0044b228 walk back
constexpr const char* kMV04 = "MV04";  // DAT_0044b230 strafe (right side negative)
constexpr const char* kMV05 = "MV05";  // DAT_0044b238 strafe
constexpr const char* kMV01 = "MV01";  // DAT_0044b240 run
constexpr const char* kWT03 = "WT03";  // DAT_0044b248 idle with status
constexpr const char* kWT02 = "WT02";  // DAT_0044b250 idle, HP < 30 %
constexpr const char* kWT01 = "WT01";  // DAT_0044b258 idle
// DAT_0044b200 / DAT_0044b210: confusion D-pad picks, indexed by rand(2) in [0,2]
constexpr uint32_t kConfDpadA[3] = {0x0, 0x1000, 0x4000};
constexpr uint32_t kConfDpadB[3] = {0x0, 0x8000, 0x2000};

bool currentIsAttack(const MoveInputs& in) {
    // *(u16*)FUN_00143d30(act,-1) == "AT" (DAT_0044b1b8)
    return in.currentAction != nullptr && in.currentAction[0] == 'A' && in.currentAction[1] == 'T';
}

}  // namespace

// orig FUN_0039ded0 (+FUN_0039ddd0 wrapper): 3x3 rotation about `axis` by `rad` (row-vector matrix).
void axisAngle(float m[12], float rad, const Vec3f& axis) {
    const float s = std::sin(rad);  // FUN_002386a0  UNVERIFIED: PS2 libm vs host sinf (ulp)
    const float c = std::cos(rad);  // FUN_00238400
    const float omc = 1.0f - c;
    const float x = axis[0], y = axis[1], z = axis[2];
    float k = omc * x;
    m[0] = k * x + c + 0.0f;
    m[1] = k * axis[1] + s * z + 0.0f;
    m[2] = k * axis[2] - s * y;
    k = omc * axis[1];
    m[4] = k * axis[0] - s * z;
    m[5] = k * axis[1] + c + 0.0f;
    m[6] = k * axis[2] + s * x + 0.0f;
    const float kz = omc * axis[2];
    m[8] = kz * axis[0] + s * y + 0.0f;
    m[9] = kz * axis[1] - s * x;
    m[10] = kz * axis[2] + c + 0.0f;
}

// orig FUN_003a0330: out = v * M (3x3 part)
Vec3f mulVec3(const Vec3f& v, const float m[12]) {
    return {v[2] * m[8] + v[0] * m[0] + v[1] * m[4], v[2] * m[9] + v[0] * m[1] + v[1] * m[5],
            v[2] * m[10] + v[0] * m[2] + v[1] * m[6]};
}

// orig FUN_002069c0 (work/decompiled/002069c0.c)
float acosTable(float x) {
    if (x <= -1.0f) return std::bit_cast<float>(0x40490fdbu);
    if (1.0f <= x) return 0.0f;
    return std::bit_cast<float>(kAcosTable[size_t(int((x + 1.0f) * 256.0f) & 0x1ff)]);
}

// orig FUN_001287a0 (work/decompiled/001287a0.c)
void snapForward(ChrTransform& t, const Vec3f& dir) {
    t.fwd = dir;
    t.right[0] = t.up[1] * t.fwd[2] - t.up[2] * t.fwd[1];
    t.right[1] = t.up[2] * t.fwd[0] - t.up[0] * t.fwd[2];
    t.right[2] = t.up[0] * t.fwd[1] - t.up[1] * t.fwd[0];
    normalize(t.right);
    t.up[0] = t.fwd[1] * t.right[2] - t.fwd[2] * t.right[1];
    t.up[1] = t.fwd[2] * t.right[0] - t.fwd[0] * t.right[2];
    t.up[2] = t.fwd[0] * t.right[1] - t.fwd[1] * t.right[0];
    normalize(t.up);
}

// orig FUN_00128ae0 (work/decompiled/00128ae0.c): rotate right about up, rebuild fwd = right x up, right = up x fwd.
void rotateAboutUp(ChrTransform& t, float rad) {
    float m[12] = {};
    axisAngle(m, rad, t.up);
    t.right = mulVec3(t.right, m);
    normalize(t.right);
    t.fwd[0] = t.right[1] * t.up[2] - t.right[2] * t.up[1];
    t.fwd[1] = t.right[2] * t.up[0] - t.right[0] * t.up[2];
    t.fwd[2] = t.right[0] * t.up[1] - t.right[1] * t.up[0];
    normalize(t.fwd);
    t.right[0] = t.up[1] * t.fwd[2] - t.up[2] * t.fwd[1];
    t.right[1] = t.up[2] * t.fwd[0] - t.up[0] * t.fwd[2];
    t.right[2] = t.up[0] * t.fwd[1] - t.up[1] * t.fwd[0];
    normalize(t.right);
}

// orig FUN_00128e90 (work/decompiled/00128e90.c): rotate up and forward about `axis`, rebuild right = up x fwd,
// up = fwd x right.
void rotateAboutAxis(ChrTransform& t, float rad, const Vec3f& axis) {
    float m[12] = {};
    axisAngle(m, rad, axis);
    t.up = mulVec3(t.up, m);
    t.fwd = mulVec3(t.fwd, m);
    normalize(t.up);
    normalize(t.fwd);
    t.right[0] = t.up[1] * t.fwd[2] - t.up[2] * t.fwd[1];
    t.right[1] = t.up[2] * t.fwd[0] - t.up[0] * t.fwd[2];
    t.right[2] = t.up[0] * t.fwd[1] - t.up[1] * t.fwd[0];
    normalize(t.right);
    t.up[0] = t.fwd[1] * t.right[2] - t.fwd[2] * t.right[1];
    t.up[1] = t.fwd[2] * t.right[0] - t.fwd[0] * t.right[2];
    t.up[2] = t.fwd[0] * t.right[1] - t.fwd[1] * t.right[0];
    normalize(t.up);
}

// orig FUN_001286c0: axes = FUN_00206f90 (1,0,0), FUN_00206f10 (0,1,0), FUN_00207010 (0,0,1)
void resetAxes(ChrTransform& t) {
    t.right = {1.0f, 0.0f, 0.0f};
    t.up = {0.0f, 1.0f, 0.0f};
    t.fwd = {0.0f, 0.0f, 1.0f};
}

// orig FUN_00128fc0 (work/decompiled/00128fc0.c). Returns true when snapped onto `dir`.
bool turnToward(float maxDeg, ChrTransform& t, const Vec3f& dir, bool scaled) {
    const float d = dir[2] * t.fwd[2] + dir[0] * t.fwd[0] + dir[1] * t.fwd[1];
    float a = acosTable(d);
    if (0.999f < d) {
        snapForward(t, dir);
        return true;
    }
    a = (a * 180.0f) / 3.1415927f;
    if (scaled) maxDeg = maxDeg * (a / 180.0f);
    if (!(a < maxDeg)) a = maxDeg;
    if (dir[2] * t.right[2] + dir[0] * t.right[0] + dir[1] * t.right[1] < 0.0f) a = -a;
    rotateAboutUp(t, (a * 3.1415927f) / 180.0f);
    return false;
}

const char* actionSuffix(int weaponClass) {
    switch (weaponClass) {  // orig FUN_0013a4b0: DAT_0044b120.. "_S1","_S2","_S3","_G1", else "_H1"
    case 0: return "_S1";
    case 1: return "_S2";
    case 2: return "_S3";
    case 3: return "_G1";
    default: return "_H1";
    }
}

namespace {
void requestWithSuffix(const MoveOutputs& out, const char* base, const MoveInputs& in) {
    if (out.requestAction) out.requestAction(std::string(base) + actionSuffix(in.weaponClass));
}
}  // namespace

// orig FUN_0013d810 (work/decompiled/0013d810.c)
void playerIdle(Phys& phys, const MoveInputs& in, const MoveOutputs& out) {
    const char* name;
    if ((in.statusMask & 0x3f) == 0) {
        if (float(in.hp) < float(in.maxHp) * 0.3f) name = kWT02;
        else name = kWT01;
    } else {
        name = kWT03;
    }
    requestWithSuffix(out, name, in);
    phys.moveVel = {0.0f, 0.0f, 0.0f};
}

// orig FUN_0013cf90 (work/decompiled/0013cf90.c)
void playerMovement(PlayerControl& ctl, ChrTransform& xf, Phys& phys, const MoveInputs& in, const MoveOutputs& out,
                    Rng& rng, bool turnOnly) {
    float sx = in.stickX, sy = in.stickY, mag = in.stickMag;
    const uint32_t held = in.held;
    uint32_t dpad = held;
    if ((in.statusMask & 2) != 0) {  // Confusion
        if ((held & 0xff000) != ctl.confPadMask) {
            ctl.confTimer = 0;
            ctl.confPadMask = held & 0xff000;
        }
        if (ctl.confTimer == 0) {
            ctl.confTimer = 10;
            const uint32_t deg = rng.rand(0x167);
            const float fdeg = float(deg);
            Vec3f r{};
            static const Vec3f kFwd{0.0f, 0.0f, 1.0f};  // FUN_00207010
            rotateY((fdeg * 3.1415927f) / 180.0f, r, kFwd);
            ctl.confDir = {r[0], r[2], in.stickMag};
            ctl.confDpad = kConfDpadA[rng.rand(2)];
            ctl.confDpad = ctl.confDpad | kConfDpadB[rng.rand(2)];
        } else {
            --ctl.confTimer;
        }
        if (0.0f < in.stickMag) {
            sx = ctl.confDir[0];
            sy = ctl.confDir[1];
            mag = ctl.confDir[2];
        } else if ((held & 0xf000) != 0) {
            dpad = ctl.confDpad;
        }
    }
    Vec3f fwd = in.camFwd;
    Vec3f right = in.camRight;
    if (in.eventCamera) {
        fwd = in.eventFwd;
        right = in.eventRight;
    }
    fwd[1] = 0.0f;
    normalize(fwd);
    Vec3f mv{0.0f, 0.0f, 0.0f};
    int moving = 0;
    if (0.0f < mag) {
        mv[0] = fwd[0] * sy + right[0] * sx + 0.0f;
        mv[2] = fwd[2] * sy + right[2] * sx + 0.0f;
        mv[1] = 0.0f;
        normalize(mv);
        if ((ctl.flags & 0x20) == 0) {
            if (ctl.lock == 0) {
                moving = 1;
                turnToward(45.0f, xf, mv, true);  // 0x42340000
            } else {
                moving = -1;
            }
            ctl.flags |= 0x10;
        } else {
            moving = ctl.lock == 0 ? 1 : -1;
        }
    } else if ((dpad & 0xf000) != 0) {
        float fx = 0.0f, fy = 0.0f, fz = 0.0f;
        if ((dpad & 0x1000) != 0) {
            fx = -fwd[0];
            fy = -fwd[1];
            fz = -fwd[2];
        } else if ((dpad & 0x4000) != 0) {
            fx = fwd[0];
            fy = fwd[1];
            fz = fwd[2];
        }
        // UP (0x1000) takes -camFwd, DOWN (0x4000) +camFwd: camera +0x70 points from the target toward the eye
        // (consistent with the stick path, where pushing up gives a negative stick y).
        float rx = 0.0f, ry = 0.0f, rz = 0.0f;
        if ((dpad & 0x8000) != 0) {
            rx = -right[0];
            ry = -right[1];
            rz = -right[2];
        } else if ((dpad & 0x2000) != 0) {
            rx = right[0];
            ry = right[1];
            rz = right[2];
        }
        mv[0] = fx + rx;
        mv[1] = fy + ry;
        mv[2] = fz + rz;
        normalize(mv);
        moving = -1;
    }
    if (turnOnly) return;
    if (moving == 0 || (in.statusMask & 1) != 0) {  // not moving, or Paralysis
        if ((ctl.flags & 0x20) == 0) {
            phys.moveVel = {0.0f, 0.0f, 0.0f};
        } else if (currentIsAttack(in)) {
            phys.moveVel = {0.0f, 0.0f, 0.0f};
            return;
        }
        playerIdle(phys, in, out);
        return;
    }
    float statusMul = 1.0f;
    float base;
    const char* name;
    if (moving < 0) {
        const float d = xf.fwd[2] * mv[2] + xf.fwd[0] * mv[0] + xf.fwd[1] * mv[1];
        if (0.8f < d) name = kMV02;
        else if (d < -0.8f) name = kMV03;
        else if (xf.right[2] * mv[2] + xf.right[0] * mv[0] + xf.right[1] * mv[1] + 0.0f + 0.0f < 0.0f) name = kMV04;
        else name = kMV05;
        base = 6.0f;
    } else if (mag < 0.9f) {
        base = 3.0f;
        name = kMV02;
    } else {
        base = 12.0f;
        name = kMV01;
    }
    bool request = true;
    if ((ctl.flags & 0x20) != 0 && currentIsAttack(in)) {
        request = false;
        statusMul = (ctl.counter & 0xfff) < 0xb ? 0.75f : 0.5f;
    }
    if (request) requestWithSuffix(out, name, in);
    const float speed = base * ((float(in.hidden0) / 255.0f) * 0.5f + 1.0f) * statusMul;
    phys.moveVel = {mv[0] * speed, mv[1] * speed, mv[2] * speed};
}

}  // namespace baroque::game
