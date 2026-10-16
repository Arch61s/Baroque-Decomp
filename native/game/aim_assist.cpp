// PORT-ONLY (not original): keyboard + mouse camera-relative attacks and subtle aim assist. See game/aim_assist.hpp
// and work/reports/CONTROLS_NATIVE.md "Aim assist". Nothing here runs unless the free mouse camera is active.
#include "game/aim_assist.hpp"

#include "game/character/character.hpp"
#include "game/character/player_control.hpp"
#include "game/free_camera.hpp"
#include "game/player_move.hpp"
#include "platform/controls.hpp"
#include "platform/platform.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace baroque::game {

namespace {

constexpr float kPi = 3.14159265f;
constexpr float kDeg = kPi / 180.0f;

// Tuning per level (index = AimAssistLevel). coneDeg: half-angle around the attack direction that a target must be
// in; maxTurnDeg: the most the direction is pulled toward it; reachScale: multiplies the weapon reach below.
struct Tuning {
    float coneDeg, maxTurnDeg, reachScale;
};
constexpr Tuning kMelee[3] = {{0.0f, 0.0f, 0.0f}, {30.0f, 20.0f, 1.0f}, {45.0f, 40.0f, 1.25f}};
constexpr Tuning kGun[3] = {{0.0f, 0.0f, 0.0f}, {12.0f, 12.0f, 1.0f}, {25.0f, 25.0f, 1.25f}};

// Reach as chrDistance (FUN_0012c270: centre distance minus both collision radii). From C00A.ACT: bare hands hit with
// Hit_b spheres r 15..22 on the hand / foot joints (forearm + sphere ~ 80) and the attacks step forward; the swords
// use Hit_c r 20 along "Right/Left_WEAPON00_Attach" with blade lengths 80 (_S1), 100 (_S2), 60 (_S3). The gun shot
// J030 flies until it meets a wall (speed 10 x 1.2^10 per tick), so its reach is a view distance.
float weaponReach(int weaponClass) {
    switch (weaponClass) {
    case 0: return 190.0f;
    case 1: return 210.0f;
    case 2: return 170.0f;
    case 3: return 1200.0f;
    default: return 110.0f;
    }
}
constexpr float kMaxHeightDiff = 250.0f;   // ignore enemies on another level of the room (stairs, platforms)

// Test overrides (no main.cpp option needed): BAROQUE_AIM_ASSIST=off|subtle|strong, BAROQUE_CAMERA_ATTACKS=0|1,
// BAROQUE_AIM_LOG=1 logs every assisted attack start.
std::string envStr(const char* name) {   // empty = unset
#ifdef _MSC_VER
    char* v = nullptr;
    size_t n = 0;
    if (_dupenv_s(&v, &n, name) != 0 || v == nullptr) return {};
    std::string s(v);
    std::free(v);
    return s;
#else
    const char* v = std::getenv(name);
    return v != nullptr ? v : "";
#endif
}
int envLevel() {
    static const int v = [] {
        const std::string e = envStr("BAROQUE_AIM_ASSIST");
        if (e.empty()) return -1;
        if (e == "off" || e == "0") return 0;
        if (e == "strong" || e == "2") return 2;
        return 1;
    }();
    return v;
}
int envCameraAttacks() {
    static const int v = [] {
        const std::string e = envStr("BAROQUE_CAMERA_ATTACKS");
        if (e.empty()) return -1;
        return (e == "0" || e == "off") ? 0 : 1;
    }();
    return v;
}
bool logOn() {
    static const bool v = !envStr("BAROQUE_AIM_LOG").empty();
    return v;
}

// The direction chosen at the last attack start, steering that attack's turn window.
struct Steer {
    const Chr* chr = nullptr;
    uint32_t serial = 0;
    Vec3f dir{};
    bool live = false;
};
Steer gSteer;

bool portActive(const PlayerControl& ctl) {
    // Free mouse camera only (keyboard / mouse play; it is never active in first person or with a gamepad), no lock-on
    // target (the original faces it), no event / talk camera (the movement axes are the event camera's then).
    return freeCameraActive() && (ctl.flags & 0x20u) == 0 && ctl.target == nullptr && !hooks::eventCameraActive();
}

bool flatUnit(Vec3f& v) {
    v[1] = 0.0f;
    const float l = std::sqrt(v[0] * v[0] + v[2] * v[2]);
    if (!(l > 1.0e-4f)) return false;
    v[0] /= l;
    v[2] /= l;
    return true;
}

// The living enemy that best matches `base`: inside the cone, within reach, minimal angle/cone + distance/reach.
const Chr* pickTarget(const Chr& self, const Vec3f& base, float coneDeg, float reach, float* angleDeg, Vec3f* toDir) {
    const Chr* best = nullptr;
    float bestScore = 0.0f;
    for (const Chr* c : characters().list()) {
        if (c == nullptr || c == &self || c->type != kChrEnemy) continue;           // enemies only, not NPCs
        if ((c->taskFlags & 0x10000000u) != 0 || c->status.block.hp <= 0) continue;  // removed / dead
        const float dy = c->xf.m.pos[1] - self.xf.m.pos[1];
        if (std::fabs(dy) > kMaxHeightDiff) continue;
        const float dist = chrDistance(self, *c);   // FUN_0012c270
        if (dist > reach) continue;
        Vec3f to{c->xf.m.pos[0] - self.xf.m.pos[0], 0.0f, c->xf.m.pos[2] - self.xf.m.pos[2]};
        if (!flatUnit(to)) continue;
        float d = to[0] * base[0] + to[2] * base[2];
        d = d > 1.0f ? 1.0f : (d < -1.0f ? -1.0f : d);
        const float ang = std::acos(d) / kDeg;
        if (ang > coneDeg) continue;
        const float score = ang / coneDeg + dist / reach;
        if (best == nullptr || score < bestScore) {
            best = c;
            bestScore = score;
            *angleDeg = ang;
            *toDir = to;
        }
    }
    return best;
}

void onAttackStart(PlayerControl& ctl, Chr& chr, int weaponClass) {
    gSteer.live = false;
    if (!portActive(ctl)) return;
    const bool camRel = cameraRelativeAttacks();
    const AimAssistLevel level = aimAssistLevel();
    if (!camRel && level == AimAssistLevel::Off) return;

    Vec3f base{};
    if (camRel) {
        float back[3], right[3];
        freeCameraAxes(back, right);   // back = horizontal target -> eye; forward = -back
        base = {-back[0], 0.0f, -back[2]};
    } else {
        base = chr.xf.m.fwd;
    }
    if (!flatUnit(base)) return;
    Vec3f dir = base;

    const Chr* target = nullptr;
    float ang = 0.0f, turn = 0.0f;
    if (level != AimAssistLevel::Off) {
        const Tuning& t = (weaponClass == 3 ? kGun : kMelee)[int(level)];
        const float reach = weaponReach(weaponClass) * t.reachScale;
        Vec3f to{};
        target = pickTarget(chr, base, t.coneDeg, reach, &ang, &to);
        if (target != nullptr) {
            turn = ang < t.maxTurnDeg ? ang : t.maxTurnDeg;
            if (ang > 0.01f) {   // slerp base -> to by turn / ang (both unit, on the ground plane)
                const float om = ang * kDeg, k = turn / ang, s = std::sin(om);
                const float a = std::sin((1.0f - k) * om) / s, b = std::sin(k * om) / s;
                dir = {base[0] * a + to[0] * b, 0.0f, base[2] * a + to[2] * b};
                if (!flatUnit(dir)) dir = base;
            } else {
                dir = to;
            }
        }
    }

    if (logOn()) {
        char buf[200];
        const float f0 = chr.xf.m.fwd[0], f2 = chr.xf.m.fwd[2];
        std::snprintf(buf, sizeof buf,
                      "aim: attack start w=%d camRel=%d level=%d fwd=(%.3f,%.3f) base=(%.3f,%.3f) target=%s ang=%.1f "
                      "turn=%.1f dir=(%.3f,%.3f)",
                      weaponClass, camRel ? 1 : 0, int(level), double(f0), double(f2), double(base[0]),
                      double(base[2]), target != nullptr ? "enemy" : "none", double(ang), double(turn), double(dir[0]),
                      double(dir[2]));
        std::string line = buf;
        if (target != nullptr) {
            char id[5] = {char(target->id & 0xff), char((target->id >> 8) & 0xff), char((target->id >> 16) & 0xff),
                          char((target->id >> 24) & 0xff), 0};
            line += std::string(" id=") + id + " dist=" + std::to_string(int(chrDistance(chr, *target)));
        }
        platform::log(line);
        if (envStr("BAROQUE_AIM_LOG") == "2") {   // every enemy relative to the player (test setup aid)
            for (const Chr* c : characters().list()) {
                if (c == nullptr || c->type != kChrEnemy || (c->taskFlags & 0x10000000u) != 0) continue;
                Vec3f to{c->xf.m.pos[0] - chr.xf.m.pos[0], 0.0f, c->xf.m.pos[2] - chr.xf.m.pos[2]};
                const float bearing = std::atan2(to[0], to[2]) / kDeg;
                std::snprintf(buf, sizeof buf, "aim:   enemy hp=%d pos=(%.0f,%.0f,%.0f) dist=%.0f bearing=%.1f",
                              int(c->status.block.hp), double(c->xf.m.pos[0]), double(c->xf.m.pos[1]),
                              double(c->xf.m.pos[2]), double(chrDistance(chr, *c)), double(bearing));
                platform::log(buf);
            }
            std::snprintf(buf, sizeof buf, "aim:   player pos=(%.0f,%.0f,%.0f) fwd bearing=%.1f base bearing=%.1f",
                          double(chr.xf.m.pos[0]), double(chr.xf.m.pos[1]), double(chr.xf.m.pos[2]),
                          double(std::atan2(chr.xf.m.fwd[0], chr.xf.m.fwd[2]) / kDeg),
                          double(std::atan2(base[0], base[2]) / kDeg));
            platform::log(buf);
        }
    }

    // The original turn function: FUN_00128fc0(180 deg, chr, dir, unscaled) turns by the whole angle; its acos table
    // (FUN_002069c0) leaves up to ~2 deg, so a second call (dot > 0.999 -> FUN_001287a0 snap) lands exactly on dir.
    if (!turnToward(180.0f, chr.xf.m, dir, false)) turnToward(180.0f, chr.xf.m, dir, false);
    if (logOn()) {
        char buf[96];
        std::snprintf(buf, sizeof buf, "aim:   turned: fwd=(%.3f,%.3f) bearing=%.1f", double(chr.xf.m.fwd[0]),
                      double(chr.xf.m.fwd[2]), double(std::atan2(chr.xf.m.fwd[0], chr.xf.m.fwd[2]) / kDeg));
        platform::log(buf);
    }
    if (camRel) gSteer = {&chr, chr.serial, dir, true};
}

bool onTurnWindow(const PlayerControl& ctl, const Chr& chr, float dir[3]) {
    if (!gSteer.live || gSteer.chr != &chr || gSteer.serial != chr.serial || !portActive(ctl)) return false;
    if (!cameraRelativeAttacks()) return false;
    dir[0] = gSteer.dir[0];
    dir[1] = 0.0f;
    dir[2] = gSteer.dir[2];
    return true;
}

const bool gInstalled = [] {
    AimAssistHooks& h = aimAssistHooks();
    h.attackStart = &onAttackStart;
    h.turnWindowDir = &onTurnWindow;
    return true;
}();

}  // namespace

AimAssistLevel aimAssistLevel() {
    const int e = envLevel();
    int v = e >= 0 ? e : platform::controls().aimAssist;
    if (v < 0 || v > 2) v = 1;
    return AimAssistLevel(v);
}
void setAimAssistLevel(AimAssistLevel level, bool save) {
    platform::Controls c = platform::controls();
    c.aimAssist = int(level);
    platform::setControls(c);
    if (save) platform::saveControls(c);
}
bool cameraRelativeAttacks() {
    const int e = envCameraAttacks();
    return e >= 0 ? e != 0 : platform::controls().cameraRelativeAttacks;
}
void setCameraRelativeAttacks(bool on, bool save) {
    platform::Controls c = platform::controls();
    c.cameraRelativeAttacks = on;
    platform::setControls(c);
    if (save) platform::saveControls(c);
}
const char* aimAssistLevelName(AimAssistLevel level) {
    switch (level) {
    case AimAssistLevel::Off: return "off";
    case AimAssistLevel::Strong: return "strong";
    default: return "subtle";
    }
}

}  // namespace baroque::game
