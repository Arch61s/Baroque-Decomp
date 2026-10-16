// PORT-ONLY (not original): keyboard + mouse gun aim through a reticle. See game/gun_aim.hpp and
// work/reports/CONTROLS_NATIVE.md "Gun reticle". Nothing here runs unless the free mouse camera is active.
#include "game/gun_aim.hpp"

#include "game/aim_assist.hpp"
#include "game/camera.hpp"
#include "game/character/character.hpp"
#include "game/character/combat.hpp"
#include "game/character/player_control.hpp"
#include "game/collision.hpp"
#include "game/event/event_manager.hpp"
#include "game/event/event_services.hpp"
#include "game/free_camera.hpp"
#include "game/objects/objects.hpp"
#include "platform/platform.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>

namespace baroque::game {

namespace {

constexpr float kPi = 3.14159265f;
constexpr float kDeg = kPi / 180.0f;
// PORT: the reticle sits above the view centre (the free camera centres the player's chest; a centred reticle would sit
// on the player and, with the view level, aim at the floor a few steps ahead). The offset is the preset's view tilt
// (-default pitch: 15 deg in the dungeon) less kReticleDipDeg, so with the eye level (elevation 0, full boom) the aim
// ray runs nearly level across the room, like the original level shot; mouse down brings it onto the floor.
constexpr float kReticleDipDeg = 4.0f;
constexpr float kReticleUpMinDeg = 2.0f, kReticleUpMaxDeg = 20.0f;
constexpr float kRange = 8000.0f;         // longest aim ray (the shot flies until it meets a wall)
constexpr float kRaySeg = 200.0f;         // the aim ray is swept in pieces of this length (radius kRayRadius)
constexpr float kRayRadius = 1.0f;
constexpr float kShotRadius = 10.0f;      // J030: wall test r 10, hit sphere r 10 (FUN_001c4430)
constexpr float kMaxYawDev = 30.0f;       // PORT: the launch never turns the shot more than this off the facing
constexpr int kSimTicks = 150;            // flight simulation length (ticks)

// Local offset (player right / up / forward) of the ACT Object spawn point of AT01_G1 (FUN_00148350: bone matrix x
// actor matrix + offset). Updated from every real launch; the default is the value measured on C00A (gun_aim.log).
Vec3f gAtLocal{-12.1f, 67.3f, 16.8f};

bool logOn() {
    static const bool v = [] {
#ifdef _MSC_VER
        char* s = nullptr;
        size_t n = 0;
        if (_dupenv_s(&s, &n, "BAROQUE_AIM_LOG") != 0 || s == nullptr) return false;
        std::free(s);
        return true;
#else
        return std::getenv("BAROQUE_AIM_LOG") != nullptr;
#endif
    }();
    return v;
}

float dot3(const Vec3f& a, const Vec3f& b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
Vec3f add(const Vec3f& a, const Vec3f& b) { return {a[0] + b[0], a[1] + b[1], a[2] + b[2]}; }
Vec3f sub(const Vec3f& a, const Vec3f& b) { return {a[0] - b[0], a[1] - b[1], a[2] - b[2]}; }
Vec3f scale(const Vec3f& a, float k) { return {a[0] * k, a[1] * k, a[2] * k}; }
Vec3f cross(const Vec3f& a, const Vec3f& b) {
    return {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]};
}
bool unit(Vec3f& v) {
    const float l = std::sqrt(dot3(v, v));
    if (!(l > 1.0e-6f)) return false;
    v = scale(v, 1.0f / l);
    return true;
}
bool flatUnit(Vec3f& v) {
    v[1] = 0.0f;
    return unit(v);
}
// orig FUN_00206be0 rotateY: x' = z*sin + x*cos, z' = z*cos - x*sin
Vec3f rotY(float rad, const Vec3f& d) {
    const float s = std::sin(rad), c = std::cos(rad);
    return {d[2] * s + d[0] * c, d[1], d[2] * c - d[0] * s};
}

// FUN_00143150 (+FUN_001431e0) without disturbing the collision module's global hit list of the tick.
bool castWorld(const Vec3f& from, const Vec3f& delta, float radius, float* dist) {
    GlobalHits saved = globalHits();
    float d = 0.0f;
    const bool hit = raycast(from.data(), delta.data(), radius, &d);
    globalHits() = saved;
    if (hit && dist != nullptr) *dist = d;
    return hit;
}
// Sweep a long ray in pieces (the collision query is built for a tick's displacement): nearest hit distance or -1.
float castRay(const Vec3f& from, const Vec3f& dir, float len, float radius) {
    float done = 0.0f;
    while (done < len) {
        const float step = (len - done) < kRaySeg ? (len - done) : kRaySeg;
        const Vec3f a = add(from, scale(dir, done));
        float d = 0.0f;
        if (castWorld(a, scale(dir, step), radius, &d)) return done + d;
        done += step;
    }
    return -1.0f;
}

bool volumeLive(const HurtVolume& v, const Chr* exclude) {
    const Chr* e = v.ent;
    if (e == nullptr || e == exclude || (e->taskFlags & 0x10000000u) != 0) return false;
    const bool act = e->obj != nullptr ? objectActive(*e) : e->active();
    return act && 0.0f < v.r;
}
// The point test of HitManager::testPoint (FUN_0014ad00) for one volume, read only.
bool volumeTouch(const HurtVolume& v, const Vec3f& p, float R) {
    const Vec3f& ep = v.ent->xf.m.pos;
    if (v.a < 0.0f) {
        const float dy = ep[1] - p[1], dx = ep[0] - p[0], dz = ep[2] - p[2];
        if (dz * dz + dx * dx + dy * dy < (R + v.r) * (R + v.r)) return true;
    }
    if (v.b < 0.0f) {
        const float dx = ep[0] - p[0], dz = ep[2] - p[2];
        if (!((R + v.r) * (R + v.r) < dz * dz + dx * dx)) return p[1] - R < v.topY && !(p[1] + R <= ep[1]);
        return false;
    }
    if (0.0f < v.b) {
        const ChrTransform& t = v.ent->xf.m;
        float f = dot3(t.fwd, p) - dot3(t.fwd, ep);
        if (f < 0.0f) f = -f;
        if (R + v.c < f) return false;
        float r = dot3(t.right, p) - dot3(t.right, ep);
        if (r < 0.0f) r = -r;
        if (R + v.b < r || v.topY <= p[1] - R) return false;
        return ep[1] < p[1] + R;
    }
    return false;
}

// [t0, t1] where o + d t lies in the slab lo < x < hi (x = dot(axis, o + d t)); false = empty.
bool slab(float o, float d, float lo, float hi, float& t0, float& t1) {
    if (std::fabs(d) < 1.0e-8f) return lo < o && o < hi;
    float a = (lo - o) / d, b = (hi - o) / d;
    if (a > b) std::swap(a, b);
    if (a > t0) t0 = a;
    if (b < t1) t1 = b;
    return t0 <= t1;
}
// Nearest entry t (>= 0, <= tMax) of the ray o + d t (d unit) into one hurt volume (radius R added), -1 = none.
float volumeEntry(const HurtVolume& v, const Vec3f& o, const Vec3f& d, float R, float tMax) {
    const Vec3f& ep = v.ent->xf.m.pos;
    float best = -1.0f;
    auto take = [&](float t) {
        if (t < 0.0f) t = 0.0f;
        if (t <= tMax && (best < 0.0f || t < best)) best = t;
    };
    if (v.a < 0.0f) {   // sphere
        const float rr = R + v.r;
        const Vec3f m = sub(o, ep);
        const float b = dot3(m, d), c = dot3(m, m) - rr * rr;
        const float disc = b * b - c;
        if (disc >= 0.0f) {
            const float s = std::sqrt(disc);
            if (-b + s >= 0.0f) take(-b - s);
        }
    }
    if (v.b < 0.0f) {   // vertical cylinder ep.y .. topY
        const float rr = R + v.r;
        float t0 = 0.0f, t1 = tMax;
        if (slab(o[1], d[1], ep[1] - R, v.topY + R, t0, t1)) {
            const float mx = o[0] - ep[0], mz = o[2] - ep[2];
            const float A = d[0] * d[0] + d[2] * d[2];
            const float B = mx * d[0] + mz * d[2], C = mx * mx + mz * mz - rr * rr;
            if (A < 1.0e-8f) {
                if (C < 0.0f) take(t0);
            } else {
                const float disc = B * B - A * C;
                if (disc >= 0.0f) {
                    const float s = std::sqrt(disc);
                    float c0 = (-B - s) / A, c1 = (-B + s) / A;
                    if (c0 > t0) t0 = c0;
                    if (c1 < t1) t1 = c1;
                    if (t0 <= t1) take(t0);
                }
            }
        }
    } else if (0.0f < v.b) {   // box along the entity's right / forward
        const ChrTransform& t = v.ent->xf.m;
        float t0 = 0.0f, t1 = tMax;
        const float of = dot3(t.fwd, o) - dot3(t.fwd, ep), df = dot3(t.fwd, d);
        const float orr = dot3(t.right, o) - dot3(t.right, ep), dr = dot3(t.right, d);
        if (slab(of, df, -(R + v.c), R + v.c, t0, t1) && slab(orr, dr, -(R + v.b), R + v.b, t0, t1) &&
            slab(o[1], d[1], ep[1] - R, v.topY + R, t0, t1))
            take(t0);
    }
    return best;
}

// Nearest volume along the ray; returns t (-1 none) and the entity.
float castVolumes(const Vec3f& o, const Vec3f& d, float tMax, const Chr* exclude, const Chr** who) {
    float best = -1.0f;
    for (const HurtVolume& v : hitManager().victims) {
        if (!volumeLive(v, exclude)) continue;
        const float t = volumeEntry(v, o, d, 0.0f, tMax);
        if (t >= 0.0f && (best < 0.0f || t < best)) {
            best = t;
            if (who != nullptr) *who = v.ent;
        }
    }
    return best;
}

// PORT aim assist for the reticle (replaces the 12 / 25 deg ground cone of aim_assist.cpp for the gun): a living
// enemy whose volume axis passes within (volume radius + margin) of the aim ray, in front of what the ray hits.
bool assistPick(const Vec3f& o, const Vec3f& d, float tMax, const Chr* exclude, Vec3f& aim) {
    const AimAssistLevel lv = aimAssistLevel();
    if (lv == AimAssistLevel::Off) return false;
    const float margin = lv == AimAssistLevel::Strong ? 45.0f : 20.0f;
    float bestT = -1.0f;
    for (const HurtVolume& v : hitManager().victims) {
        if (!volumeLive(v, exclude)) continue;
        const Chr* e = v.ent;
        if (e->type != kChrEnemy || e->status.block.hp <= 0) continue;
        const Vec3f& ep = e->xf.m.pos;
        const float top = v.a < 0.0f ? ep[1] : v.topY;
        // closest approach between the ray and the vertical segment (ep.x, ep.y..top, ep.z), sampled in height
        float bestDist = 1.0e30f, tAt = 0.0f;
        Vec3f pAt{};
        for (int k = 0; k <= 8; ++k) {
            const Vec3f q{ep[0], ep[1] + (top - ep[1]) * float(k) / 8.0f, ep[2]};
            float t = dot3(sub(q, o), d);
            if (t < 0.0f) t = 0.0f;
            const Vec3f p = add(o, scale(d, t));
            const Vec3f w = sub(q, p);
            const float dist = std::sqrt(dot3(w, w));
            if (dist < bestDist) {
                bestDist = dist;
                tAt = t;
                pAt = q;
            }
        }
        if (bestDist > v.r + margin || tAt > tMax) continue;
        if (bestT < 0.0f || tAt < bestT) {
            bestT = tAt;
            aim = pAt;
        }
    }
    return bestT >= 0.0f;
}

// Shot direction from the spawn point at the aim point through the original first-person formula's parameters:
// horizontal unit direction + pitch (vertical speed sin(pitch) * 10 against horizontal speed 10).
bool solveShot(const Vec3f& muzzle, const Vec3f& aim, const Vec3f& refFwd, Vec3f& hdir, float& pitch) {
    bool clamped = false;
    Vec3f ref = refFwd;
    if (!flatUnit(ref)) ref = {0.0f, 0.0f, 1.0f};
    const Vec3f dlt = sub(aim, muzzle);
    Vec3f h{dlt[0], 0.0f, dlt[2]};
    const float hl = std::sqrt(h[0] * h[0] + h[2] * h[2]);
    const float smax = std::sin(kGunPitchLimit);
    if (hl < 1.0f || dot3(h, ref) <= 5.0f) {   // aim point at / behind the spawn point: straight on, steepest pitch
        hdir = ref;
        pitch = dlt[1] < 0.0f ? -kGunPitchLimit : (dlt[1] > 0.0f ? kGunPitchLimit : 0.0f);
        return true;
    }
    h = scale(h, 1.0f / hl);
    float cosA = dot3(h, ref);
    if (cosA < std::cos(kMaxYawDev * kDeg)) {   // rotate ref toward h by kMaxYawDev
        const float sgn = (ref[2] * h[0] - ref[0] * h[2]) >= 0.0f ? 1.0f : -1.0f;   // rotY(+a) turns z toward x
        h = rotY(sgn * kMaxYawDev * kDeg, ref);
        clamped = true;
        cosA = std::cos(kMaxYawDev * kDeg);
    }
    // vertical slope over the horizontal distance actually travelled along h
    const float run = hl * cosA > 1.0f ? hl * cosA : 1.0f;
    float s = dlt[1] / run;
    if (s > smax) { s = smax; clamped = true; }
    if (s < -smax) { s = -smax; clamped = true; }
    hdir = h;
    pitch = std::asin(s);
    return clamped;
}

// J030 flight (FUN_001c4430 order): move, hit sphere r 10, end after a wall contact, wall test r 10, x1.2 for 10.
bool simulateShot(const Vec3f& spawn, const Vec3f& hdir, float pitch, const Chr* owner, Vec3f& impact) {
    Vec3f p = spawn;
    Vec3f v{hdir[0] * 10.0f, ps2Sin(pitch) * 10.0f, hdir[2] * 10.0f};   // FUN_00206900 as the launch
    bool wall = false;
    Vec3f wallAt{};
    for (int i = 0, n = 0; i < kSimTicks; ++i) {
        p = add(p, v);
        for (const HurtVolume& hv : hitManager().victims)
            if (volumeLive(hv, owner) && volumeTouch(hv, p, kShotRadius)) {
                impact = p;
                return true;
            }
        if (wall) {
            impact = wallAt;
            return true;
        }
        float d = 0.0f;
        if (castWorld(p, v, kShotRadius, &d)) {
            wall = true;
            Vec3f u = v;
            unit(u);
            wallAt = add(p, scale(u, d));
            // the r 10 sphere touches before its centre line meets the surface: find the surface point itself
            float d2 = 0.0f;
            const float ext = kShotRadius * 6.0f;
            if (castWorld(wallAt, scale(u, ext), kRayRadius, &d2)) wallAt = add(wallAt, scale(u, d2));
        }
        if (n < 10) {
            ++n;
            v = scale(v, 1.2f);
        }
    }
    return false;
}

Vec3f spawnFromAt(const Vec3f& at, const Vec3f& fwd) {
    // FUN_001c41e0: position = at + 12 y + rotateY(0.0873, forward) * 30
    const Vec3f d = rotY(0.08726646f, fwd);
    return {at[0] + d[0] * 30.0f, at[1] + 12.0f + d[1] * 30.0f, at[2] + d[2] * 30.0f};
}

// Attack start (after FUN_0013c4e0 started AT01_G1): the camera-relative / assist hook of aim_assist.cpp runs first,
// then the gun turns the player onto the point under the reticle with the original turn FUN_00128fc0 (180 deg,
// unscaled; a second call snaps the last ~2 deg of its acos table, as aim_assist.cpp). The AT.._G1 action takes no
// turn input afterwards (FUN_0013b0d0), so this is the facing the J030 spawn offset is built from.
void (*gInnerStart)(PlayerControl&, Chr&, int) = nullptr;
void wrappedStart(PlayerControl& ctl, Chr& chr, int weaponClass) {
    if (gInnerStart != nullptr) gInnerStart(ctl, chr, weaponClass);
    if (weaponClass != 3 || &chr != characters().player() || !gunAimActive() || ctl.target != nullptr) return;
    Vec3f aim{};
    bool onEnemy = false;
    if (!gunAimPoint(chr, chr.xf.m.pos, aim, &onEnemy)) return;
    Vec3f to = sub(aim, chr.xf.m.pos);
    if (!flatUnit(to)) return;
    if (!turnToward(180.0f, chr.xf.m, to, false)) turnToward(180.0f, chr.xf.m, to, false);
    if (logOn()) {
        char buf[160];
        std::snprintf(buf, sizeof buf, "gun aim: attack start aim (%.0f,%.0f,%.0f)%s facing (%.3f,%.3f)", double(aim[0]),
                      double(aim[1]), double(aim[2]), onEnemy ? " enemy" : "", double(chr.xf.m.fwd[0]),
                      double(chr.xf.m.fwd[2]));
        platform::log(buf);
    }
}
// Chained onto the hook table of aim_assist.cpp (header-only table; its own installer may run before or after this
// translation unit's static initialisation, so the chain is re-checked from the reticle draw and the launch too).
void ensureChained() {
    AimAssistHooks& h = aimAssistHooks();
    if (h.attackStart == &wrappedStart) return;
    gInnerStart = h.attackStart;
    h.attackStart = &wrappedStart;
}
const bool gChained = [] {
    ensureChained();
    return true;
}();

}  // namespace

void gunAimEnsureHooks() { ensureChained(); }

bool gunAimActive() {
    ensureChained();
    if (!freeCameraActive() || !cameraRelativeAttacks()) return false;
    const PlayerControl* ctl = playerCtl();
    if (ctl == nullptr || ctl->camera == nullptr || (ctl->flags & 0x20u) != 0) return false;   // first person: original
    if (ctl->target != nullptr) return false;   // lock-on: the original faces the target (FUN_0013b0d0)
    const Chr* p = characters().player();
    if (p == nullptr) return false;
    if (hooks::weaponClass() != 3) return false;                       // FUN_001d7d10: the gun
    if (hooks::eventCameraActive() || eventActive() || eventModeFlag() != 0) return false;
    return true;
}
bool gunAimHasAmmo() { return hooks::gunHasAmmo(); }

bool gunAimView(const Vec3f& pp, GunAimView& out) {
    const PlayerControl* ctl = playerCtl();
    if (ctl == nullptr || ctl->camera == nullptr) return false;
    const Camera& cam = *ctl->camera;
    const FreeCamera& c = freeCamera();
    // the free camera's presented basis (free_camera_view.cpp freeCameraMatrix, with the boom drawn last frame)
    const float height = cam.tun.height, dist = cam.tun.dist, defPitch = cam.tun.defaultPitch;
    const Vec3f pivot{pp[0], pp[1] + height, pp[2]};
    const float ce = std::cos(c.elevDeg * kDeg), se = std::sin(c.elevDeg * kDeg);
    const float sy = std::sin(c.yaw), cy = std::cos(c.yaw);
    const Vec3f boom{ce * sy, se, ce * cy};
    const float d = c.shownDist >= 0.0f ? c.shownDist : dist;
    out.eye = add(pivot, scale(boom, d));
    const float drop = defPitch < 0.0f ? dist * std::tan(-defPitch * kDeg) : 0.0f;
    const Vec3f lookAt{pivot[0], pivot[1] - drop, pivot[2]};
    out.back = sub(out.eye, lookAt);
    if (!unit(out.back)) out.back = {sy, 0.0f, cy};
    out.right = {cy, 0.0f, -sy};
    out.up = cross(out.back, out.right);
    float upDeg = -defPitch - kReticleDipDeg;
    upDeg = upDeg < kReticleUpMinDeg ? kReticleUpMinDeg : (upDeg > kReticleUpMaxDeg ? kReticleUpMaxDeg : upDeg);
    const float a = upDeg * kDeg;
    out.rayDir = add(scale(out.back, -std::cos(a)), scale(out.up, std::sin(a)));
    unit(out.rayDir);
    // start where the ray passes the player (its projection of the look-at point): nothing between the eye and the
    // player is aimed at, and the player itself is excluded from the volume cast
    float t0 = dot3(sub(lookAt, out.eye), out.rayDir);
    if (t0 < 0.0f) t0 = 0.0f;
    out.rayStart = add(out.eye, scale(out.rayDir, t0));
    const float fov = cam.engine != nullptr ? cam.engine->fovDeg : 48.0f;
    out.tanHalfY = std::tan(fov * 0.5f * kDeg) / (4.0f / 3.0f);   // present::presentProjection (Hor+)
    return true;
}

bool gunAimPoint(const Chr& player, const Vec3f& pp, Vec3f& aim, bool* onEnemy) {
    GunAimView v;
    if (!gunAimView(pp, v)) return false;
    float tw = castRay(v.rayStart, v.rayDir, kRange, kRayRadius);
    if (tw < 0.0f) tw = kRange;
    const Chr* who = nullptr;
    const float tv = castVolumes(v.rayStart, v.rayDir, tw, &player, &who);
    bool enemy = false;
    if (tv >= 0.0f) {
        aim = add(v.rayStart, scale(v.rayDir, tv));
        enemy = who != nullptr && who->type == kChrEnemy;
    } else if (assistPick(v.rayStart, v.rayDir, tw, &player, aim)) {
        enemy = true;
    } else {
        aim = add(v.rayStart, scale(v.rayDir, tw));
    }
    if (onEnemy != nullptr) *onEnemy = enemy;
    return true;
}

bool gunAimPredict(const Chr& player, const Vec3f& pp, GunAimSolution& out) {
    out = GunAimSolution{};
    if (!gunAimPoint(player, pp, out.aimPoint, &out.onEnemy)) return false;
    // the player turns toward the aim point at the attack start (aim_assist.cpp)
    Vec3f f = sub(out.aimPoint, pp);
    if (!flatUnit(f)) {
        f = player.xf.m.fwd;
        if (!flatUnit(f)) f = {0.0f, 0.0f, 1.0f};
    }
    const Vec3f r{f[2], 0.0f, -f[0]};
    const Vec3f at = add(pp, add(scale(r, gAtLocal[0]), add(Vec3f{0.0f, gAtLocal[1], 0.0f}, scale(f, gAtLocal[2]))));
    out.muzzle = spawnFromAt(at, f);
    out.clamped = solveShot(out.muzzle, out.aimPoint, f, out.hdir, out.pitch);
    out.impactValid = simulateShot(out.muzzle, out.hdir, out.pitch, &player, out.impact);
    return true;
}

void gunAimLogEnd(const Vec3f& p) {
    if (!logOn()) return;
    char buf[96];
    std::snprintf(buf, sizeof buf, "gun aim: J030 wall end at (%.0f,%.0f,%.0f)", double(p[0]), double(p[1]), double(p[2]));
    platform::log(buf);
}

bool gunAimLaunch(const Chr& owner, const Vec3f& actAt, const Vec3f& spawn, Vec3f& hdir, float& pitch) {
    if (&owner != characters().player() || !gunAimActive()) return false;
    const ChrTransform& m = owner.xf.m;
    const Vec3f rel = sub(actAt, m.pos);
    Vec3f f = m.fwd;
    if (flatUnit(f)) {   // remember the ACT spawn offset for the HUD prediction
        const Vec3f r{f[2], 0.0f, -f[0]};
        gAtLocal = {dot3(rel, r), rel[1], dot3(rel, f)};
    } else {
        f = {0.0f, 0.0f, 1.0f};
    }
    Vec3f aim{};
    bool enemy = false;
    if (!gunAimPoint(owner, m.pos, aim, &enemy)) return false;
    const bool clamped = solveShot(spawn, aim, f, hdir, pitch);
    if (logOn()) {
        char buf[260];
        std::snprintf(buf, sizeof buf,
                      "gun aim: launch at local (%.1f,%.1f,%.1f) spawn (%.0f,%.0f,%.0f) aim (%.0f,%.0f,%.0f)%s pitch %.2f deg "
                      "hdir (%.3f,%.3f)%s",
                      double(gAtLocal[0]), double(gAtLocal[1]), double(gAtLocal[2]), double(spawn[0]), double(spawn[1]),
                      double(spawn[2]), double(aim[0]), double(aim[1]), double(aim[2]), enemy ? " enemy" : "",
                      double(pitch / kDeg), double(hdir[0]), double(hdir[2]), clamped ? " clamped" : "");
        platform::log(buf);
        Vec3f imp{};
        if (simulateShot(spawn, hdir, pitch, &owner, imp)) {
            std::snprintf(buf, sizeof buf, "gun aim:   predicted end (%.0f,%.0f,%.0f)", double(imp[0]), double(imp[1]), double(imp[2]));
            platform::log(buf);
        }
    }
    return true;
}

}  // namespace baroque::game
