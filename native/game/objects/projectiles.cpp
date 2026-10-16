// Projectile classes of the factory table 0x44aa10 (J030, J050..J074; wrappers 0x12f340..0x12fc70, ctor / launch
// (vt+0x28) / tick (vt+0xc) in 0x1c40a0..0x1cc200), created by the ACT command Object (FUN_00148350) through
// FUN_0012ce70 and launched with (owner, spawn position, owner bone). vt+0x1c (init) is an empty function for every
// class, vt+0x20 is FUN_0012a5d0 (always active), vt+0x24 FUN_00129140.
// Common object fields: +0x20 transform (Chr::xf), +0x24 owner, +0x28 owner serial (+0xc), +0x2c owner id (+0x1c,
// the weapon tag of FUN_0014ad00), +0x30 effect handle or model holder, +0x34..+0x3c velocity.
// Every FUN_0014ad00 call passes the owner (+0x24) as the excluded character (stack argument, disassembly).
#include "game/objects/objects.hpp"
#include "game/objects/objects_internal.hpp"

#include "game/character/combat.hpp"
#include "game/collision.hpp"
#include "game/effects/effects.hpp"
#include "game/game_world.hpp"
#include "game/gun_aim.hpp"   // PORT: keyboard + mouse gun reticle
#include "game/hud/hud_api.hpp"
#include "game/items/inventory.hpp"
#include "game/items/item.hpp"
#include "game/flag_manager.hpp"
#include "game/camera.hpp"
#include "game/player_move.hpp"
#include "platform/platform.hpp"

#include <cmath>
#include <cstdio>

#pragma fenv_access(on)

namespace baroque::game {

namespace {

ObjData& od(Chr& c) { return *c.obj; }
Vec3f& pos(Chr& c) { return c.xf.m.pos; }
uint32_t tagOf(const char* s) { return objTag(s); }

// orig FUN_001287a0(obj, f): forward = f, right = normalise(up x f), up = normalise(f x right)
void setForward(ChrTransform& t, const Vec3f& f) {
    t.fwd = f;
    t.right = {t.up[1] * f[2] - t.up[2] * f[1], t.up[2] * f[0] - t.up[0] * f[2], t.up[0] * f[1] - t.up[1] * f[0]};
    normalize(t.right);
    t.up = {f[1] * t.right[2] - f[2] * t.right[1], f[2] * t.right[0] - f[0] * t.right[2], f[0] * t.right[1] - f[1] * t.right[0]};
    normalize(t.up);
}

Vec3f vel(const ObjData& o) { return {o.F(0x34), o.F(0x38), o.F(0x3c)}; }
void setVel(ObjData& o, const Vec3f& v) {
    o.F(0x34) = v[0];
    o.F(0x38) = v[1];
    o.F(0x3c) = v[2];
}

// +0x24 owner dropped when its serial (+0xc) changed (the record was reused)
Chr* ownerCheck(Chr& c) {
    ObjData& o = od(c);
    Chr* w = o.P(0x24);
    if (w != nullptr && static_cast<uint32_t>(o.I(0x28)) != w->serial) o.P(0x24) = nullptr;
    return o.P(0x24);
}

// launch head shared by the classes: owner, its serial and id, velocity = owner forward * speed, position
void launchHead(Chr& c, Chr& owner, const Vec3f& at, float speed, bool keepId = true) {
    ObjData& o = od(c);
    o.P(0x24) = &owner;
    o.I(0x28) = static_cast<int32_t>(owner.serial);
    if (keepId) o.I(0x2c) = static_cast<int32_t>(owner.id);
    const Vec3f& f = owner.xf.m.fwd;
    setVel(o, {f[0] * speed, f[1] * speed, f[2] * speed});
    pos(c) = at;
}

// orig FUN_00206d20(out, dir): out[1] = atan2(x, z) in degrees (yaw), out[0] the pitch, out[2] 0
float yawDeg(const Vec3f& d) { return (std::atan2(d[0], d[2]) * 180.0f) / 3.1415927f; }

// FUN_00112190(0, fx, pos, rot, (s, s, s, 1))
int fxAt(int fx, const Vec3f& p, const Vec3f* rotDeg, const Vec3f& scale) {
    return effects().spawn(0.0f, fx, p.data(), rotDeg != nullptr ? rotDeg->data() : nullptr, scale.data());
}
constexpr Vec3f k100{100.0f, 100.0f, 100.0f};

// FUN_00143150(pos, vel, &r) (+ FUN_001431e0 distance)
bool wallTest(Chr& c, float r, float* dist) {
    const Vec3f v = vel(od(c));
    float d = 0.0f;
    const bool hit = raycast(pos(c).data(), v.data(), r, &d);
    if (dist != nullptr) *dist = d;
    return hit;
}
// pos += normalise(vel) * (distance + 9): onto the wall
void snapToWall(Chr& c, float dist) {
    Vec3f d = vel(od(c));
    normalize(d);   // FUN_00206ab0
    const float k = dist + 9.0f;
    pos(c) = {pos(c)[0] + d[0] * k, pos(c)[1] + d[1] * k, pos(c)[2] + d[2] * k};
}
// FUN_00143200(i): face of hit record i (null for character / object hits)
const CollFace* hitFace(int i) {
    const GlobalHits& g = globalHits();
    if (i < 0 || i >= g.count || static_cast<size_t>(i) >= g.list.size()) return nullptr;
    const GlobalHit& h = g.list[size_t(i)];
    if (h.mesh == nullptr || h.face < 0 || static_cast<size_t>(h.face) >= h.mesh->faces.size()) return nullptr;
    return &h.mesh->faces[size_t(h.face)];
}
// the hit list holds a wall ('W') face or a non-face hit (the "cVar6" scan of the classes)
bool hitWallOrBody() {
    const int n = globalHits().count;   // FUN_001431f0
    if (n == 0) return true;
    for (int i = 0; i < n; ++i) {
        const CollFace* f = hitFace(i);
        if (f == nullptr || f->type == 'W') return true;
    }
    return false;
}
// Impact decal on the first hit face: FUN_00112190(0, fx, 0, 0, 0) then FUN_00112770 with the basis
// right = normalise(n x ref), fwd = normalise(right x n), ref = up for 'W' faces else +z (FUN_00206ca0(0, ..)), the
// normal's y 0 -> 0.001, rows * 100 (scale constants of the classes), position.
void wallDecal(int fx, const Vec3f& p, bool fixY = true) {
    const CollFace* f = hitFace(0);
    if (f == nullptr) return;
    const int h = effects().spawn(0.0f, fx, nullptr, nullptr, nullptr);
    const Vec3f ref = f->type == 'W' ? Vec3f{0.0f, 1.0f, 0.0f} : Vec3f{0.0f, 0.0f, 1.0f};
    const Vec3f& n = f->normal;
    Vec3f r{n[1] * ref[2] - n[2] * ref[1], n[2] * ref[0] - n[0] * ref[2], n[0] * ref[1] - n[1] * ref[0]};
    normalize(r);
    Vec3f fw{r[1] * n[2] - r[2] * n[1], r[2] * n[0] - r[0] * n[2], r[0] * n[1] - r[1] * n[0]};
    normalize(fw);
    Vec3f nn = n;
    if (fixY && nn[1] == 0.0f) nn[1] = 0.001f;   // (J072 FUN_001cb6b0 uses the face normal as is)
    ExiMat m;
    for (int i = 0; i < 16; ++i) m.m[size_t(i)] = (i % 5 == 0) ? 1.0f : 0.0f;
    for (int k = 0; k < 3; ++k) {
        m.m[size_t(k)] = r[size_t(k)] * 100.0f;
        m.m[size_t(4 + k)] = nn[size_t(k)] * 100.0f;
        m.m[size_t(8 + k)] = fw[size_t(k)] * 100.0f;
        m.m[size_t(12 + k)] = p[size_t(k)];
    }
    effects().setMatrix(h, m);
}
// FUN_0014ad00(R, power, 0x50c780, obj, 0, pos, m1, m2, m3, owner id, owner)
bool hitTest(Chr& c, float r, float power, uint32_t m1, uint32_t m2, uint32_t m3) {
    ObjData& o = od(c);
    return hitManager().testPoint(r, power, &c, 0, pos(c), m1, m2, m3, static_cast<uint32_t>(o.I(0x2c)), o.P(0x24));
}
void moveByVel(Chr& c) {
    const Vec3f v = vel(od(c));
    pos(c) = {pos(c)[0] + v[0], pos(c)[1] + v[1], pos(c)[2] + v[2]};
}
void fxFollow(Chr& c, uint32_t off) { effects().setPosition(od(c).I(off), pos(c).data()); }   // FUN_001122d0
void endKill(Chr& c) {   // FUN_00112530(+0x30) + FUN_00207350
    fxKill(od(c).I(0x30));
    entityRemove(c);
}
void endStop(Chr& c) {   // FUN_00112560(+0x30) + FUN_00207350
    fxStop(od(c).I(0x30));
    entityRemove(c);
}

// ---------------------------------------------------------------------------------------------------------------
// The straight "bullet" classes J050 / J057 / J058 / J059 / J061 share one tick body (FUN_001c4800, FUN_001c6980,
// FUN_001c7cc0, FUN_001c7020, FUN_001c76d0) with different constants:
//   wall (FUN_00143150, r 10): snap onto the wall, wall SE, impact decal on the face, end;
//   else hit sphere FUN_0014ad00: end (+ hit SE / effect), J061 keeps flying (no end on a hit);
//   else move, x / z velocity * 1.2 for the first 10 ticks (+0x40), the effect follows (FUN_001122d0).
struct BulletSpec {
    const char* tag;
    float speed;            // launch: owner forward * speed (DAT_00463990 / 0x463a58 / 0x463ad0 / 0x463a80 / 0x463aa8)
    int fxFly;
    Vec3f fxScale;
    bool fxYaw;             // effect rotation (0, yaw(owner forward), 0) (FUN_00206d20)
    int seWall;             // -1 none
    int fxWall;             // impact decal
    float hitR, hitPower;
    uint32_t m1, m2, m3;
    int seHit, fxHit;       // -1 none (effect at the position, scale 100)
    bool endOnHit;
    bool killFx;            // end: FUN_00112530 (kill) or FUN_00112560 (stop)
};
constexpr BulletSpec kBullets[] = {
    {"J050", 5.0f, 0x1b, k100, true, 0xca, 0xf, 20.0f, 1.0f, 0, 0, 0x2000000, -1, -1, true, true},
    {"J057", -10.0f, 0x3f, k100, true, 0x33, 0xf, 10.0f, 1.0f, 0, 0, 0, 0x33, 0x18, true, true},
    {"J058", 5.0f, 0x47, k100, false, 0x7f, 0x48, 20.0f, 1.0f, 4, 0, 0, -1, -1, true, true},
    {"J059", 10.0f, 0xd6, k100, true, 0x33, 0xf, 10.0f, 0.2f, 0, 0, 0x1000, 0x33, 0x16, true, true},
    {"J061", 10.0f, 0x4c, Vec3f{200.0f, 200.0f, 400.0f}, true, -1, 0xf, 20.0f, 1.0f, 0, 0, 0x4000000, -1, -1, false, true},
    {"J069", 5.0f, 0x5f, k100, true, -1, 0xf, 20.0f, 1.0f, 0, 0, 0x4000000, -1, -1, true, true},   // FUN_001ca150 / FUN_001ca250
};
const BulletSpec* bulletOf(uint32_t tag) {
    for (const BulletSpec& b : kBullets)
        if (tagOf(b.tag) == tag) return &b;
    // J060 / J062 share J058's class (factory table)
    if (tag == tagOf("J060") || tag == tagOf("J062")) return &kBullets[2];
    return nullptr;
}

void bulletLaunch(Chr& c, Chr& owner, const Vec3f& at, const BulletSpec& b) {
    launchHead(c, owner, at, b.speed);
    ObjData& o = od(c);
    Vec3f rot{0.0f, yawDeg(owner.xf.m.fwd), 0.0f};
    o.I(0x30) = fxAt(b.fxFly, at, b.fxYaw ? &rot : nullptr, b.fxScale);
    o.I(0x40) = 0;
}

void bulletTick(Chr& c, const BulletSpec& b) {
    ObjData& o = od(c);
    ownerCheck(c);
    float dist = 0.0f;
    if (wallTest(c, 10.0f, &dist)) {
        snapToWall(c, dist);
        if (b.seWall >= 0) objSe3d(b.seWall, pos(c));
        wallDecal(b.fxWall, pos(c));
        if (b.killFx) endKill(c); else endStop(c);
        return;
    }
    if (hitTest(c, b.hitR, b.hitPower, b.m1, b.m2, b.m3)) {
        if (!b.endOnHit) return;
        if (b.seHit >= 0) objSe3d(b.seHit, pos(c));
        if (b.fxHit >= 0) fxAt(b.fxHit, pos(c), nullptr, k100);
        if (b.killFx) endKill(c); else endStop(c);
        return;
    }
    moveByVel(c);
    if (o.I(0x40) < 10) {
        o.I(0x40) = o.I(0x40) + 1;
        o.F(0x34) = o.F(0x34) * 1.2f;
        o.F(0x3c) = o.F(0x3c) * 1.2f;
    }
    fxFollow(c, 0x30);
}

// ---------------------------------------------------------------------------------------------------------------
// J030: the player's gun shot (C00A AT01_G1). ctor FUN_001c40a0: flags 0x53 / 0x54 set (FUN_00178890), one round of
// the equipped gun (+0x6c) used unless floor 99 (FUN_0011c100) with flag 0x21, +0x40 = 0.
// launch FUN_001c41e0: velocity forward * 10 (first person: y = sin(camera pitch) * 10), position + 12 y + forward
// rotated by 0.0873 rad * 30, axes = owner forward, effect 0x14 (yaw, first person: pitch), +0x30 = 0.
// tick FUN_001c4430: move, hit sphere 10 power 1000 (m 0/0/0, weapon 0) -> end; after a wall contact (+0x40) the
// spark 0x11 + SE 0xb3 and end; wall test r 10 sets +0x40; velocity * 1.2 for 10 ticks; the effect follows.
// The tick's end paths only mark the record removed (FUN_00207350); the flying effect 0x14 (+0x2c) is stopped by the
// class destructor FUN_001c4150 (vtable 0x4ce690 +8: FUN_00112560(+0x2c), base FUN_0012aea0) when the record is
// deleted -> projDestroy. (The other projectile dtors 0x1c4680..0x1cbee0 stop no effect: their ticks stop / kill
// it before the removal; J051 / J063 / J066 only release the model holder, FUN_0012d940.)
// ---------------------------------------------------------------------------------------------------------------
void j030Ctor(Chr& c) {
    flags().set(0x53);
    flags().set(0x54);
    if (!(floorIdNow() == 99 && flags().test(0x21))) {
        if (Item* g = inventory().equip[0]) g->count = g->count - 1;   // *(iRam0053d9b4 + 0x6c) -= 1
    }
    od(c).I(0x40) = 0;
}
void j030Launch(Chr& c, Chr& owner, const Vec3f& at) {
    ObjData& o = od(c);
    o.P(0x24) = &owner;
    o.I(0x28) = static_cast<int32_t>(owner.serial);
    const Vec3f& f = owner.xf.m.fwd;
    setVel(o, {f[0] * 10.0f, f[1] * 10.0f, f[2] * 10.0f});   // DAT_00463978
    PlayerControl* ctl = playerCtl();
    const bool fp = ctl != nullptr && (ctl->flags & 0x20u) != 0;
    if (fp && ctl->camera != nullptr) o.F(0x38) = ps2Sin(ctl->camera->pitchRad) * 10.0f;
    float d[3] = {f[0], f[1], f[2]};
    rotateY(0.08726646f, d);   // FUN_00206be0(0x3db2b8c3)
    pos(c) = at;
    pos(c)[1] = pos(c)[1] + 12.0f;
    pos(c) = {pos(c)[0] + d[0] * 30.0f, pos(c)[1] + d[1] * 30.0f, pos(c)[2] + d[2] * 30.0f};   // DAT_00463980
    setForward(c.xf.m, owner.xf.m.fwd);   // FUN_001287a0
    Vec3f rot{fp && ctl->camera != nullptr ? (-ctl->camera->pitchRad * 180.0f) / 3.1415927f : 0.0f, yawDeg(f), 0.0f};
    // PORT (keyboard + mouse, game/gun_aim.hpp): with the free mouse camera the shot goes from this spawn point to the
    // point under the reticle, through the first-person formula above: horizontal unit direction * 10, vertical
    // sin(pitch) * 10 (pitch clamped like camera +0xc8), effect pitched by -pitch. Never with a gamepad / first person.
    Vec3f aimH{};
    float aimPitch = 0.0f;
    if (gunAimLaunch(owner, at, pos(c), aimH, aimPitch)) {
        setVel(o, {aimH[0] * 10.0f, ps2Sin(aimPitch) * 10.0f, aimH[2] * 10.0f});
        rot = {(-aimPitch * 180.0f) / 3.1415927f, yawDeg(aimH), 0.0f};
    }
    o.I(0x2c) = fxAt(0x14, at, &rot, k100);   // effect handle kept at +0x2c in this class
    o.I(0x30) = 0;
}
void j030Tick(Chr& c) {
    ObjData& o = od(c);
    ownerCheck(c);
    moveByVel(c);
    if (hitManager().testPoint(10.0f, 1000.0f, &c, 0, pos(c), 0, 0, 0, 0, o.P(0x24))) {
        entityRemove(c);
        return;
    }
    if (o.I(0x40) == 1) {
        gunAimLogEnd(pos(c));   // PORT: BAROQUE_AIM_LOG test trace only
        fxAt(0x11, pos(c), nullptr, k100);
        objSe3d(0xb3, pos(c));
        entityRemove(c);
        return;
    }
    if (wallTest(c, 10.0f, nullptr)) o.I(0x40) = 1;
    if (o.I(0x30) < 10) {
        o.I(0x30) = o.I(0x30) + 1;
        o.F(0x34) = o.F(0x34) * 1.2f;
        o.F(0x38) = o.F(0x38) * 1.2f;
        o.F(0x3c) = o.F(0x3c) * 1.2f;
    }
    fxFollow(c, 0x2c);
}

// ---------------------------------------------------------------------------------------------------------------
// J051: E05 icicle. ctor FUN_001c4c70: model "DATA/CHR/E05/E05_ICICLE.IGB" (0x4639c0) mode 1 flags 0x40000001 at
// the holder +0x30. launch FUN_001c4d70: velocity owner forward * 2 with y -0.35, position + 100 y, axes = owner
// forward, scale 0.1 (+0x4c), +0x40 / +0x44 = 0, +0x48 rise speed 10.
// tick FUN_001c4e50: grows (scale +0.025 to 1) while it circles the owner's head for 32 ticks (rotation about its
// forward 0.785 rad per tick, offset owner forward * 40), then rises 16 ticks (+0x48 decreasing by 0.5) and at tick
// 48 is pushed 40 along the owner's forward; then it flies: wall (r 10) -> SE 0xbe + burst 0x28 (scale 150), hit
// sphere 10 (m 2 / 0 / 0x4000000) -> burst 0x28, end; velocity * 1.2 for 20 ticks. The owner gone or playing its
// animation 4 before tick 48 -> burst and end. Published at pos + offset (FUN_00109d40).
// ---------------------------------------------------------------------------------------------------------------
void j051Ctor(Chr& c) { objLoadModel(c, "DATA/CHR/E05/E05_ICICLE.IGB", 1, 0x40000001); }
void j051Launch(Chr& c, Chr& owner, const Vec3f& at) {
    ObjData& o = od(c);
    launchHead(c, owner, at, 2.0f);   // DAT_004639e0
    o.F(0x38) = -0.35f;               // 0xbeb33333
    pos(c)[1] = pos(c)[1] + 100.0f;
    setForward(c.xf.m, owner.xf.m.fwd);
    o.F(0x4c) = 0.1f;
    c.xf.scale = {0.1f, 0.1f, 0.1f};
    o.I(0x40) = 0;
    o.I(0x44) = 0;
    o.F(0x48) = 10.0f;
}
void j051Burst(Chr& c) {   // FUN_00112190(0, 0x28, pos, 0, (150, 150, 150, 1)) + FUN_00207350
    fxAt(0x28, pos(c), nullptr, Vec3f{150.0f, 150.0f, 150.0f});
    entityRemove(c);
}
void j051Grow(ObjData& o, Chr& c) {
    float s = o.F(0x4c) + 0.025f;
    o.F(0x4c) = s;
    if (1.0f < s) o.F(0x4c) = 1.0f;
    c.xf.scale = {o.F(0x4c), o.F(0x4c), o.F(0x4c)};
}
void j051Tick(Chr& c) {
    ObjData& o = od(c);
    Chr* w = ownerCheck(c);
    const int t = o.I(0x40);
    if (t < 0x30 && (w == nullptr || w->model.anim.currentClip() != 4)) {   // held only while the owner casts (anim 4)
        j051Burst(c);
        return;
    }
    // UNVERIFIED: the publish offset (fStack_10..) is not written in the flight phase and at tick 48 (stale stack in
    // the original); 0 here
    Vec3f ofs{0.0f, 0.0f, 0.0f};
    if (t < 0x20) {
        o.I(0x40) = t + 1;
        rotateAboutForward(c.xf.m, 0.7853982f);   // FUN_001289c0(0x3f490fdb)
        j051Grow(o, c);
        setForward(c.xf.m, w->xf.m.fwd);
        const Vec3f& f = w->xf.m.fwd;
        setVel(o, {f[0] * 2.0f, -0.35f, f[2] * 2.0f});   // DAT_004639e8, y overwritten
        ofs = {f[0] * 40.0f, f[1] * 40.0f, f[2] * 40.0f};   // DAT_004639f0
    } else if (t < 0x30) {
        o.I(0x40) = t + 1;
        const Vec3f& f = w->xf.m.fwd;
        if (t + 1 == 0x30) {
            pos(c) = {pos(c)[0] + f[0] * 40.0f, pos(c)[1] + f[1] * 40.0f, pos(c)[2] + f[2] * 40.0f};   // DAT_004639f8
        } else {
            ofs = {f[0] * 40.0f, f[1] * 40.0f, f[2] * 40.0f};   // DAT_00463a00
        }
        rotateAboutRight(c.xf.m, -0.07853982f);   // FUN_00128c00(0xbda0d97c)
        pos(c)[1] = pos(c)[1] + o.F(0x48);
        const float r = o.F(0x48) - 0.5f;
        o.F(0x48) = r;
        if (r < 0.0f) o.F(0x48) = 0.0f;
        j051Grow(o, c);
    } else {
        float dist = 0.0f;
        if (wallTest(c, 10.0f, &dist)) {
            objSe3d(0xbe, pos(c));
            j051Burst(c);
            return;
        }
        if (hitTest(c, 10.0f, 1.0f, 2, 0, 0x4000000)) {
            j051Burst(c);
            return;
        }
        moveByVel(c);
        if (o.I(0x44) < 0x14) {
            o.I(0x44) = o.I(0x44) + 1;
            setVel(o, {o.F(0x34) * 1.2f, o.F(0x38) * 1.2f, o.F(0x3c) * 1.2f});   // DAT_00463a10
        }
    }
    objPublishAt(c, Vec3f{ofs[0] + pos(c)[0], ofs[1] + pos(c)[1], ofs[2] + pos(c)[2]});
}

// ---------------------------------------------------------------------------------------------------------------
// J052: E07 gun shell. launch FUN_001c5450: velocity forward * 30 with y 2, effect 0x2c. tick FUN_001c5530: wall
// (r 10) or hit sphere 10 (m1 1) -> SE 0xd1, burst 0x2d (scale 200), blast sphere 100 (m1 1), end (kill); else
// move, gravity 0.25, the effect follows.
// ---------------------------------------------------------------------------------------------------------------
void j052Launch(Chr& c, Chr& owner, const Vec3f& at) {
    launchHead(c, owner, at, 30.0f);   // DAT_00463a18
    od(c).F(0x38) = 2.0f;
    od(c).I(0x30) = fxAt(0x2c, at, nullptr, k100);
}
void j052Blast(Chr& c) {
    objSe3d(0xd1, pos(c));
    fxAt(0x2d, pos(c), nullptr, Vec3f{200.0f, 200.0f, 200.0f});
    hitTest(c, 100.0f, 1.0f, 1, 0, 0);
    endKill(c);   // FUN_001c57e0
}
void j052Tick(Chr& c) {
    ownerCheck(c);
    if (wallTest(c, 10.0f, nullptr) || hitTest(c, 10.0f, 1.0f, 1, 0, 0)) {
        j052Blast(c);
        return;
    }
    moveByVel(c);
    od(c).F(0x38) = od(c).F(0x38) - 0.25f;
    fxFollow(c, 0x30);
}

// ---------------------------------------------------------------------------------------------------------------
// J054: E08 homing shot. launch FUN_001c58d0: faces the player (turn 30, FUN_00128fc0), rolled by rand(0x8000)/32768
// * 30 - 15 degrees about its forward, starts at the player's position - forward * 300 (the shot comes at the
// player), effect 0x2f at the spawn point, axes then the owner's forward. tick FUN_001c5af0: 16 ticks of homing
// (velocity forward * 20, move, turn toward the player 30, effect position + yaw); then burst 0x32, sphere 20, end
// (stop). The owner gone -> end.
// ---------------------------------------------------------------------------------------------------------------
void j054Launch(Chr& c, Chr& owner, const Vec3f& at) {
    ObjData& o = od(c);
    o.P(0x24) = &owner;
    o.I(0x28) = static_cast<int32_t>(owner.serial);
    o.I(0x2c) = static_cast<int32_t>(owner.id);
    Chr* pl = characters().player();
    if (pl == nullptr) return;   // PORT: the original dereferences iRam0050b144
    Vec3f d{pl->xf.m.pos[0] - at[0], 0.0f, pl->xf.m.pos[2] - at[2]};
    normalize(d);
    turnToward(30.0f, c.xf.m, d, true);   // FUN_00128fc0(0x41f00000, obj, d, 1)
    setForward(c.xf.m, d);
    const float r = static_cast<float>(static_cast<int>(gameWorld().rng.rand(0x8000)));
    rotateAboutForward(c.xf.m, (((r / 32768.0f) * 30.0f + -15.0f) * 3.1415927f) / 180.0f);
    const Vec3f& f = c.xf.m.fwd;
    pos(c) = pl->xf.m.pos;
    pos(c) = {pos(c)[0] + f[0] * -300.0f, pos(c)[1] + f[1] * -300.0f, pos(c)[2] + f[2] * -300.0f};   // DAT_00463a28
    o.I(0x30) = fxAt(0x2f, at, nullptr, k100);
    setForward(c.xf.m, owner.xf.m.fwd);
    o.I(0x40) = 0;
}
void j054Tick(Chr& c) {
    ObjData& o = od(c);
    Chr* w = ownerCheck(c);
    if (w == nullptr) {
        endStop(c);   // FUN_001c5d10
        return;
    }
    Chr* pl = characters().player();
    if (o.I(0x40) < 0x10 && pl != nullptr) {
        const Vec3f& f = c.xf.m.fwd;
        setVel(o, {f[0] * 20.0f, f[1] * 20.0f, f[2] * 20.0f});   // DAT_00463a30
        moveByVel(c);
        Vec3f d{pl->xf.m.pos[0] - pos(c)[0], 0.0f, pl->xf.m.pos[2] - pos(c)[2]};
        normalize(d);
        turnToward(30.0f, c.xf.m, d, true);
        const float rot[3] = {0.0f, yawDeg(c.xf.m.fwd), 0.0f};
        effects().setPositionRotation(o.I(0x30), pos(c).data(), rot);   // FUN_001123f0
        o.I(0x40) = o.I(0x40) + 1;
        return;
    }
    fxAt(0x32, pos(c), nullptr, k100);
    hitTest(c, 20.0f, 1.0f, 0, 0, 0);
    endStop(c);
}

// ---------------------------------------------------------------------------------------------------------------
// J055 / J056: E11 spit (left / right mouth). launch FUN_001c5e00 / FUN_001c6380: velocity forward * 5 turned by
// +-pi/2 (FUN_00206be0), y 4, effect 0x37 / 0x39. tick FUN_001c5f00 / FUN_001c6480: wall (r 10) -> snap, SE 0x36 /
// 0xb8, effect 0xe5 / 0xe6 (a wall or a body in the hits) or 0x38 / 0x3a (floor only), end (stop); hit sphere 10
// (J055: m3 0x20, SE 0x16b + effect 0xe5; J056: m1 1) -> end; else move, gravity 0.5, the effect follows.
// ---------------------------------------------------------------------------------------------------------------
void spitLaunch(Chr& c, Chr& owner, const Vec3f& at, float turn, int fx) {
    launchHead(c, owner, at, 5.0f);   // DAT_00463a38 / DAT_00463a48
    float v[3] = {od(c).F(0x34), od(c).F(0x38), od(c).F(0x3c)};
    rotateY(turn, v);
    setVel(od(c), {v[0], 4.0f, v[2]});
    od(c).I(0x30) = fxAt(fx, at, nullptr, k100);
}
void spitTick(Chr& c, bool left) {
    ownerCheck(c);
    float dist = 0.0f;
    if (wallTest(c, 10.0f, &dist)) {
        snapToWall(c, dist);
        objSe3d(left ? 0x36 : 0xb8, pos(c));
        if (hitWallOrBody()) fxAt(left ? 0xe5 : 0xe6, pos(c), nullptr, k100);
        else fxAt(left ? 0x38 : 0x3a, pos(c), nullptr, k100);
        endStop(c);
        return;
    }
    if (left ? hitTest(c, 10.0f, 1.0f, 0, 0, 0x20) : hitTest(c, 10.0f, 1.0f, 1, 0, 0)) {
        if (left) {
            objSe3d(0x16b, pos(c));
            fxAt(0xe5, pos(c), nullptr, k100);
        }
        endStop(c);
        return;
    }
    moveByVel(c);
    od(c).F(0x38) = od(c).F(0x38) - 0.5f;
    fxFollow(c, 0x30);
}

// ---------------------------------------------------------------------------------------------------------------
// J063: E22 bubble. ctor FUN_001c8130: model holder. launch FUN_001c8210: velocity forward * 15, y -25, axes = owner
// forward, +0x40 / +0x48 = 0, model "DATA/CHR/E22/OBJ063%c.IGB" (owner id's 4th letter) mode 1 flags 0x40000001.
// tick FUN_001c82f0: on contact (r 10) with a wall / body: SE 0x16b, burst 0xf4, end; a floor bounces it (y speed 0,
// bounce count +0x48, ripple 0xf4 at (90, 0, 0)... effect 0x46, wobble 0.5); hit sphere 10 -> SE 0x16b + 0xf4, end;
// 20 bounces end it; the wobble alternates the x/z and y scale (1 +- w, w / 1.5 per tick), y speed - 3.
// ---------------------------------------------------------------------------------------------------------------
void j063Launch(Chr& c, Chr& owner, const Vec3f& at) {
    ObjData& o = od(c);
    launchHead(c, owner, at, 15.0f);   // DAT_00463af8
    setForward(c.xf.m, owner.xf.m.fwd);
    o.F(0x38) = -25.0f;                // 0xc1c80000
    o.F(0x40) = 0.0f;
    o.I(0x48) = 0;
    char path[64];
    std::snprintf(path, sizeof path, "DATA/CHR/E22/OBJ063%c.IGB", static_cast<char>(owner.id >> 24));   // 0x463b00
    objLoadModel(c, path, 1, 0x40000001);
}
void j063Pop(Chr& c) {
    objSe3d(0x16b, pos(c));
    fxAt(0xf4, pos(c), nullptr, k100);
    entityRemove(c);
}
void j063Tick(Chr& c) {
    ObjData& o = od(c);
    ownerCheck(c);
    float dist = 0.0f;
    if (wallTest(c, 10.0f, &dist)) {
        snapToWall(c, dist);
        if (hitWallOrBody()) {
            j063Pop(c);
            return;
        }
        o.F(0x38) = 0.0f;
        o.I(0x48) = o.I(0x48) + 1;
        const Vec3f rot{90.0f, 0.0f, 0.0f};   // 0x42b40000
        fxAt(0x46, pos(c), &rot, k100);
        o.F(0x40) = 0.5f;
        o.I(0x44) = 0;
    }
    if (hitTest(c, 10.0f, 1.0f, 0, 0, 0)) {
        j063Pop(c);
        return;
    }
    if (o.I(0x48) >= 0x14) {
        entityRemove(c);
        return;
    }
    moveByVel(c);
    if (o.F(0x40) == 0.5f) o.F(0x38) = 25.0f;   // 0x41c80000
    float a, b;
    if ((static_cast<uint32_t>(o.I(0x44)) & 1u) == 0) {
        a = o.F(0x40) + 1.0f;
        b = 1.0f - o.F(0x40);
    } else {
        a = 1.0f - o.F(0x40);
        b = o.F(0x40) + 1.0f;
    }
    o.I(0x44) = o.I(0x44) + 1;
    c.xf.scale = {a, b, a};
    o.F(0x40) = o.F(0x40) / 1.5f;
    o.F(0x38) = o.F(0x38) - 3.0f;
    objPublish(c);
}

// ---------------------------------------------------------------------------------------------------------------
// J064: E24 element shot. launch FUN_001c8810: velocity forward * 5, element rand(2) (+0x44): 2 -> effect 0x47,
// 1 -> 0xa0, 0 -> 0x9e. tick FUN_001c8970: wall (r 10): element 2 -> SE 0x7f + decal 0x48, 1 -> SE 0xbe + burst 0x28,
// 0 -> SE 0xd1 + burst 0x2d; end (stop); hit sphere 20 with m1 4 / 2 / 1 -> end; else the bullet flight.
// ---------------------------------------------------------------------------------------------------------------
void j064Launch(Chr& c, Chr& owner, const Vec3f& at) {
    ObjData& o = od(c);
    launchHead(c, owner, at, 5.0f);   // DAT_00463b28
    o.I(0x44) = static_cast<int32_t>(gameWorld().rng.rand(2));
    const int e = o.I(0x44);
    if (e == 2) o.I(0x30) = fxAt(0x47, at, nullptr, k100);
    else if (e == 1) o.I(0x30) = fxAt(0xa0, at, nullptr, k100);
    else if (e == 0) o.I(0x30) = fxAt(0x9e, at, nullptr, k100);
    o.I(0x40) = 0;
}
void j064Tick(Chr& c) {
    ObjData& o = od(c);
    ownerCheck(c);
    const int e = o.I(0x44);
    float dist = 0.0f;
    if (wallTest(c, 10.0f, &dist)) {
        snapToWall(c, dist);
        if (e == 2) {
            objSe3d(0x7f, pos(c));
            wallDecal(0x48, pos(c));
        } else if (e == 1) {
            objSe3d(0xbe, pos(c));
            fxAt(0x28, pos(c), nullptr, k100);
        } else if (e == 0) {
            objSe3d(0xd1, pos(c));
            fxAt(0x2d, pos(c), nullptr, k100);
        }
        endStop(c);   // FUN_001c8f70
        return;
    }
    bool hit = false;
    if (e == 2) hit = hitTest(c, 20.0f, 1.0f, 4, 0, 0);
    else if (e == 1) hit = hitTest(c, 20.0f, 1.0f, 2, 0, 0);
    else if (e == 0) hit = hitTest(c, 20.0f, 1.0f, 1, 0, 0);
    if (hit) {
        endStop(c);
        return;
    }
    moveByVel(c);
    if (o.I(0x40) < 10) {
        o.I(0x40) = o.I(0x40) + 1;
        o.F(0x34) = o.F(0x34) * 1.2f;
        o.F(0x3c) = o.F(0x3c) * 1.2f;
    }
    fxFollow(c, 0x30);
}

// ---------------------------------------------------------------------------------------------------------------
// J065: E25 stream. launch FUN_001c9060: velocity forward * 5, position + 80 y, origin +0x48 = spawn point, effect
// 0x5d (yaw). tick FUN_001c9190: phase 0: beyond 800 from the origin the effect loops out (FUN_001126e0(h, 0, -1,
// -1)) and phase 1 starts; hit sphere 80 (m3 0x8000) every tick; phase 1: removed once the effect is gone
// (FUN_00112500); both phases move with x / z * 1.2 for 10 ticks and the effect follows.
// ---------------------------------------------------------------------------------------------------------------
void j065Launch(Chr& c, Chr& owner, const Vec3f& at) {
    ObjData& o = od(c);
    launchHead(c, owner, at, 5.0f);   // DAT_00463b50
    pos(c)[1] = pos(c)[1] + 80.0f;
    o.F(0x48) = at[0];
    o.F(0x4c) = at[1];
    o.F(0x50) = at[2];
    const Vec3f rot{0.0f, yawDeg(owner.xf.m.fwd), 0.0f};
    o.I(0x30) = fxAt(0x5d, at, &rot, k100);
    o.I(0x40) = 0;
    o.I(0x44) = 0;
}
void j065Tick(Chr& c) {
    ObjData& o = od(c);
    ownerCheck(c);
    if (o.I(0x44) == 0) {
        const float dy = pos(c)[1] - o.F(0x4c), dx = pos(c)[0] - o.F(0x48), dz = pos(c)[2] - o.F(0x50);
        const float d2 = dz * dz + dx * dx + dy * dy;
        const float d = d2 != 0.0f ? std::sqrt(d2) : 0.0f;
        if (800.0f < d) {
            fxLoopOut(o.I(0x30), 0, -1, -1);
            o.I(0x44) = 1;
        }
        hitTest(c, 80.0f, 1.0f, 0, 0, 0x8000);
    } else if (!fxExists(o.I(0x30))) {
        entityRemove(c);
        return;
    }
    moveByVel(c);
    if (o.I(0x40) < 10) {
        o.I(0x40) = o.I(0x40) + 1;
        o.F(0x34) = o.F(0x34) * 1.2f;
        o.F(0x3c) = o.F(0x3c) * 1.2f;
    }
    fxFollow(c, 0x30);
}

// ---------------------------------------------------------------------------------------------------------------
// J066: E27 coin. ctor FUN_001c9390: model holder +0x34. launch FUN_001c9470: model "DATA/CHR/E27/E27%c_COIN.IGB"
// (owner id's 4th letter) mode 1 flags 0x40000001, velocity (+0x38) forward * 5, axes = owner forward, effect 0xda,
// +0x48 = 0. tick FUN_001c95a0: the bullet body on the +0x38 velocity: wall -> snap, SE 0xb1, decal 0xf, end (stop);
// hit sphere 10 -> SE 0xb1, effect 0x17, end; else move, x / z * 1.2 for 10 ticks, model published, effect follows.
// ---------------------------------------------------------------------------------------------------------------
Vec3f vel38(const ObjData& o) { return {o.F(0x38), o.F(0x3c), o.F(0x40)}; }
void j066Launch(Chr& c, Chr& owner, const Vec3f& at) {
    char path[64];
    std::snprintf(path, sizeof path, "DATA/CHR/E27/E27%c_COIN.IGB", static_cast<char>(owner.id >> 24));   // 0x463b60
    objLoadModel(c, path, 1, 0x40000001);
    ObjData& o = od(c);
    o.P(0x24) = &owner;
    o.I(0x28) = static_cast<int32_t>(owner.serial);
    o.I(0x2c) = static_cast<int32_t>(owner.id);
    const Vec3f& f = owner.xf.m.fwd;
    o.F(0x38) = f[0] * 5.0f;   // DAT_00463b58
    o.F(0x3c) = f[1] * 5.0f;
    o.F(0x40) = f[2] * 5.0f;
    pos(c) = at;
    setForward(c.xf.m, f);
    o.I(0x30) = fxAt(0xda, at, nullptr, k100);
    o.I(0x48) = 0;
}
void j066Tick(Chr& c) {
    ObjData& o = od(c);
    ownerCheck(c);
    const Vec3f v = vel38(o);
    float dist = 0.0f;
    if (raycast(pos(c).data(), v.data(), 10.0f, &dist)) {   // 0x463b80
        Vec3f d = v;
        normalize(d);
        const float k = dist + 9.0f;
        pos(c) = {pos(c)[0] + d[0] * k, pos(c)[1] + d[1] * k, pos(c)[2] + d[2] * k};
        objSe3d(0xb1, pos(c));
        wallDecal(0xf, pos(c));
        endStop(c);   // FUN_001c9a70
        return;
    }
    if (hitTest(c, 10.0f, 1.0f, 0, 0, 0)) {
        objSe3d(0xb1, pos(c));
        fxAt(0x17, pos(c), nullptr, k100);
        endStop(c);
        return;
    }
    pos(c) = {pos(c)[0] + v[0], pos(c)[1] + v[1], pos(c)[2] + v[2]};
    if (o.I(0x48) < 10) {
        o.I(0x48) = o.I(0x48) + 1;
        o.F(0x38) = o.F(0x38) * 1.2f;
        o.F(0x40) = o.F(0x40) * 1.2f;
    }
    objPublish(c);
    fxFollow(c, 0x30);
}

// ---------------------------------------------------------------------------------------------------------------
// J068: E19 poison spit. launch FUN_001c9b60: velocity (+0x38) forward * 8, y 4, effect 0x4d, puddle timer +0x34 = 0.
// tick FUN_001c9c40: flying: wall (r 10) -> effect stopped, snap, SE 0x36; on a floor only a puddle (effect 0x4e,
// 120 ticks), otherwise splash 0xe7 and end (stop); hit sphere 10 (m2 0x20) -> effect stopped, SE 0x16b, 0xe7, end;
// else move, gravity 0.5, the effect follows. Puddle: counts down, at 0 loops the effect out and is removed,
// otherwise hit sphere 30 (m2 0x20).
// ---------------------------------------------------------------------------------------------------------------
void j068Launch(Chr& c, Chr& owner, const Vec3f& at) {
    ObjData& o = od(c);
    o.P(0x24) = &owner;
    o.I(0x28) = static_cast<int32_t>(owner.serial);
    o.I(0x2c) = static_cast<int32_t>(owner.id);
    const Vec3f& f = owner.xf.m.fwd;
    o.F(0x38) = f[0] * 8.0f;   // DAT_00463ba0
    o.F(0x3c) = 4.0f;
    o.F(0x40) = f[2] * 8.0f;
    pos(c) = at;
    o.I(0x30) = fxAt(0x4d, at, nullptr, k100);
    o.I(0x34) = 0;
}
void j068Tick(Chr& c) {
    ObjData& o = od(c);
    ownerCheck(c);
    if (o.I(0x34) == 0) {
        const Vec3f v = vel38(o);
        float dist = 0.0f;
        if (raycast(pos(c).data(), v.data(), 10.0f, &dist)) {   // 0x463ba8
            fxStop(o.I(0x30));
            Vec3f d = v;
            normalize(d);
            const float k = dist + 9.0f;
            pos(c) = {pos(c)[0] + d[0] * k, pos(c)[1] + d[1] * k, pos(c)[2] + d[2] * k};
            objSe3d(0x36, pos(c));
            if (!hitWallOrBody()) {
                o.I(0x30) = fxAt(0x4e, pos(c), nullptr, k100);
                o.I(0x34) = 0x78;
            } else {
                fxAt(0xe7, pos(c), nullptr, k100);
                endStop(c);   // FUN_001ca060
            }
            return;
        }
        if (hitTest(c, 10.0f, 1.0f, 0, 0x20, 0)) {
            fxStop(o.I(0x30));
            objSe3d(0x16b, pos(c));
            fxAt(0xe7, pos(c), nullptr, k100);
            endStop(c);
            return;
        }
        pos(c) = {pos(c)[0] + v[0], pos(c)[1] + v[1], pos(c)[2] + v[2]};
        o.F(0x3c) = o.F(0x3c) - 0.5f;
        fxFollow(c, 0x30);
        return;
    }
    o.I(0x34) = o.I(0x34) - 1;
    if (o.I(0x34) == 0) {
        fxLoopOut(o.I(0x30), 0, -1, -1);
        entityRemove(c);
    } else {
        hitTest(c, 30.0f, 1.0f, 0, 0x20, 0);   // UNVERIFIED: excluded character of this call (owner assumed)
    }
}

// ---------------------------------------------------------------------------------------------------------------
// J070 (E21 SP01) / J071 (E22 AT05): sparks orbiting the owner. Rotation FUN_0039de50 (FUN_0039dff0 Euler matrix)
// and FUN_0039f760 (rows 0..2 times the vector).
// ---------------------------------------------------------------------------------------------------------------
// orig FUN_0039dff0(ax, ay, az): rows {cy cz, cy sz, -sy}, {sx sy cz - cx sz, sx sy sz + cx cz, sx cy},
// {cx cz sy + sx sz, cx sz sy - sx cz, cx cy} (the sx sy / cx sz products only when that angle is non-zero)
void eulerRows(float ax, float ay, float az, float r[3][3]) {
    float sx = 0.0f, cx = 1.0f, sy = 0.0f, cy = 1.0f, sz = 0.0f, cz = 1.0f, sxsy = 0.0f, cxsz = 0.0f;
    if (ax != 0.0f) { sx = std::sin(ax); cx = std::cos(ax); }
    if (ay != 0.0f) { sy = std::sin(ay); cy = std::cos(ay); sxsy = sx * sy; }
    if (az != 0.0f) { sz = std::sin(az); cz = std::cos(az); cxsz = cx * sz; }
    r[0][0] = cy * cz; r[0][1] = cy * sz; r[0][2] = -sy;
    r[1][0] = sxsy * cz - cxsz; r[1][1] = sxsy * sz + cx * cz + 0.0f; r[1][2] = sx * cy;
    r[2][0] = cx * cz * sy + sx * sz; r[2][1] = cxsz * sy - sx * cz; r[2][2] = cx * cy;
}
Vec3f rowsTimes(const float r[3][3], const Vec3f& v) {   // FUN_0039f760 (one vector)
    return {v[0] * r[0][0] + v[1] * r[1][0] + v[2] * r[2][0], v[0] * r[0][1] + v[1] * r[1][1] + v[2] * r[2][1],
            v[0] * r[0][2] + v[1] * r[1][2] + v[2] * r[2][2]};
}

// J070 launch FUN_001ca760: two effects 0x47 (+0x30 / +0x34), angles +0x38 / +0x3c / +0x40 and radius +0x44 = 0.
// tick FUN_001ca830: owner gone -> both effects stopped, removed (FUN_001cab70); radius eases to 80
// (r -= (r - 84) / 16); offset = Euler(+0x3c, +0x38, +0x40) * (0, 0, r) with x / z * 0.6; the effects sit at
// owner + 90 y +- offset, a hit sphere 10 (m1 4, m2 2) at each (owner + 90 y +- (x, 0, z)); +0x38 += 0.0838 (wraps).
void j070Launch(Chr& c, Chr& owner, const Vec3f& at) {
    ObjData& o = od(c);
    o.P(0x24) = &owner;
    o.I(0x28) = static_cast<int32_t>(owner.serial);
    o.I(0x2c) = static_cast<int32_t>(owner.id);
    pos(c) = at;
    o.I(0x30) = fxAt(0x47, at, nullptr, k100);
    o.I(0x34) = fxAt(0x47, at, nullptr, k100);
    o.F(0x3c) = 0.0f;
    o.F(0x38) = 0.0f;
    o.F(0x40) = 0.0f;
    o.F(0x44) = 0.0f;
}
void j070Tick(Chr& c) {
    ObjData& o = od(c);
    Chr* w = ownerCheck(c);
    if (w == nullptr) {   // FUN_001cab70 (PORT: a null owner from the start is treated the same; the original faults)
        fxStop(o.I(0x30));
        fxStop(o.I(0x34));
        entityRemove(c);
        return;
    }
    float r = o.F(0x44);
    if (r < 80.0f) {
        r = r - (r - 84.0f) / 16.0f;
        o.F(0x44) = r;
        if (80.0f <= r) o.F(0x44) = 80.0f;
    }
    float m[3][3];
    eulerRows(o.F(0x3c), o.F(0x38), o.F(0x40), m);
    Vec3f v = rowsTimes(m, Vec3f{0.0f, 0.0f, o.F(0x44)});
    v[0] = v[0] * 0.6f;
    v[2] = v[2] * 0.6f;
    const Vec3f& p = w->xf.m.pos;
    const Vec3f a{p[0] + v[0], p[1] + 90.0f + v[1], p[2] + v[2]};
    effects().setPosition(o.I(0x30), a.data());
    hitManager().testPoint(10.0f, 1.0f, &c, 0, Vec3f{p[0] + v[0], p[1] + 90.0f, p[2] + v[2]}, 4, 2, 0,
                           static_cast<uint32_t>(o.I(0x2c)), w);
    const Vec3f b{p[0] - v[0], (p[1] + 90.0f) - v[1], p[2] - v[2]};
    effects().setPosition(o.I(0x34), b.data());
    hitManager().testPoint(10.0f, 1.0f, &c, 0, Vec3f{p[0] - v[0], p[1] + 90.0f, p[2] - v[2]}, 4, 2, 0,
                           static_cast<uint32_t>(o.I(0x2c)), w);
    const float ang = o.F(0x38) + 0.0837758f;
    o.F(0x38) = ang;
    if (3.1415927f < ang) o.F(0x38) = ang - 6.2831855f;
}

// J071 launch FUN_001cac70: four effects 0xcd (+0x30..+0x3c), the last two started at half their sequence length
// (FUN_00112670 / FUN_00112730), angle +0x40 = 0, radius +0x44 = 10. tick FUN_001cadb0: owner gone -> the four
// stopped, removed (FUN_001cb2f0); the owner's AI MODE (FUN_001f1730(ai, 0x1000003)) 0..3 bursts the orbs from that
// index on (SE 0x36, effect 0x56 scale 50, stopped); radius eases to 60 ((r - 64) / 16); orbs at owner + 100 y
// +- R(y)*(0, 0, r) and +- R(y)*(r, 0, 0); the angle -= 0.1047 (wraps).
void j071Launch(Chr& c, Chr& owner, const Vec3f& at) {
    ObjData& o = od(c);
    o.P(0x24) = &owner;
    o.I(0x28) = static_cast<int32_t>(owner.serial);
    o.I(0x2c) = static_cast<int32_t>(owner.id);
    pos(c) = at;
    for (uint32_t k = 0; k < 4; ++k) o.I(0x30 + 4 * k) = fxAt(0xcd, at, nullptr, k100);
    for (uint32_t k = 2; k < 4; ++k) {
        int len = effects().sequenceLength(o.I(0x30 + 4 * k));
        if (len < 0) len = len + 1;
        effects().setTime(o.I(0x30 + 4 * k), len >> 1);
    }
    o.F(0x40) = 0.0f;
    o.F(0x44) = 10.0f;
}
void j071Burst(ObjData& o, uint32_t off) {
    if (o.I(off) == 0) return;
    float p[3];
    effects().getPosition(o.I(off), p);   // FUN_00112290
    objSe3d(0x36, Vec3f{p[0], p[1], p[2]});
    fxAt(0x56, Vec3f{p[0], p[1], p[2]}, nullptr, Vec3f{50.0f, 50.0f, 50.0f});
    fxStop(o.I(off));
    o.I(off) = 0;
}
void j071Tick(Chr& c) {
    ObjData& o = od(c);
    Chr* w = ownerCheck(c);
    if (w == nullptr) {   // FUN_001cb2f0
        for (uint32_t k = 0; k < 4; ++k) fxStop(o.I(0x30 + 4 * k));
        entityRemove(c);
        return;
    }
    const uint32_t mode = aiVar(w->ai, 0x1000003u);
    if (mode <= 3)   // case 0 bursts all four, 1 the last three, 2 the last two, 3 the last one
        for (uint32_t k = mode; k < 4; ++k) j071Burst(o, 0x30 + 4 * k);
    float r = o.F(0x44);
    if (r < 60.0f) {
        r = r - (r - 64.0f) / 16.0f;
        o.F(0x44) = r;
        if (60.0f <= r) o.F(0x44) = 60.0f;
    }
    float m[3][3];
    eulerRows(0.0f, o.F(0x40), 0.0f, m);
    const Vec3f& p = w->xf.m.pos;
    Vec3f v = rowsTimes(m, Vec3f{0.0f, 0.0f, o.F(0x44)});
    if (o.I(0x30) != 0) effects().setPosition(o.I(0x30), Vec3f{p[0] + v[0], p[1] + 100.0f + v[1], p[2] + v[2]}.data());
    if (o.I(0x34) != 0) effects().setPosition(o.I(0x34), Vec3f{p[0] - v[0], (p[1] + 100.0f) - v[1], p[2] - v[2]}.data());
    v = rowsTimes(m, Vec3f{o.F(0x44), 0.0f, 0.0f});
    if (o.I(0x38) != 0) effects().setPosition(o.I(0x38), Vec3f{p[0] + v[0], p[1] + 100.0f + v[1], p[2] + v[2]}.data());
    if (o.I(0x3c) != 0) effects().setPosition(o.I(0x3c), Vec3f{p[0] - v[0], (p[1] + 100.0f) - v[1], p[2] - v[2]}.data());
    const float ang = o.F(0x40) - 0.10471976f;
    o.F(0x40) = ang;
    if (ang < -3.1415927f) o.F(0x40) = ang + 6.2831855f;
}

// ---------------------------------------------------------------------------------------------------------------
// J072: E24 scatter shot. launch FUN_001cb430: speed rand(0x8000)/32768 * 25 + 5 along the owner's forward, turned
// -30 degrees then rand(0x8000)/32768 * 60 degrees, y 8, element rand(2) (+0x40) with effect 0x47 / 0xa0 / 0x9e.
// tick FUN_001cb6b0: wall (r 10): element 2 SE 0x7f + decal 0x48, 1 SE 0xbe + 0x28, 0 SE 0xd1 + 0x2d; end (stop);
// hit sphere 20 (m1 4 / 2 / 1) -> end; else move, gravity 1, the effect follows.
// ---------------------------------------------------------------------------------------------------------------
void j072Launch(Chr& c, Chr& owner, const Vec3f& at) {
    ObjData& o = od(c);
    o.P(0x24) = &owner;
    o.I(0x28) = static_cast<int32_t>(owner.serial);
    o.I(0x2c) = static_cast<int32_t>(owner.id);
    const float r1 = static_cast<float>(static_cast<int>(gameWorld().rng.rand(0x8000)));
    const float sp = (r1 / 32768.0f) * 25.0f + 5.0f;
    const Vec3f& f = owner.xf.m.fwd;
    float v[3] = {f[0] * sp, f[1] * sp, f[2] * sp};
    rotateY(-0.5235988f, v);   // FUN_00206be0(0xbf060a92)
    const float r2 = static_cast<float>(static_cast<int>(gameWorld().rng.rand(0x8000)));
    rotateY(((r2 / 32768.0f) * 60.0f * 3.1415927f) / 180.0f, v);
    setVel(o, {v[0], 8.0f, v[2]});
    pos(c) = at;
    o.I(0x40) = static_cast<int32_t>(gameWorld().rng.rand(2));
    const int e = o.I(0x40);
    if (e == 2) o.I(0x30) = fxAt(0x47, at, nullptr, k100);
    else if (e == 1) o.I(0x30) = fxAt(0xa0, at, nullptr, k100);
    else if (e == 0) o.I(0x30) = fxAt(0x9e, at, nullptr, k100);
}
void j072Tick(Chr& c) {
    ObjData& o = od(c);
    ownerCheck(c);
    const int e = o.I(0x40);
    if (wallTest(c, 10.0f, nullptr)) {   // 0x463bd8 (no snap in this class)
        if (e == 2) {
            objSe3d(0x7f, pos(c));
            wallDecal(0x48, pos(c), false);
        } else if (e == 1) {
            objSe3d(0xbe, pos(c));
            fxAt(0x28, pos(c), nullptr, k100);
        } else if (e == 0) {
            objSe3d(0xd1, pos(c));
            fxAt(0x2d, pos(c), nullptr, k100);
        }
        endStop(c);   // FUN_001cbbc0
        return;
    }
    bool hit = false;
    if (e == 2) hit = hitTest(c, 20.0f, 1.0f, 4, 0, 0);
    else if (e == 1) hit = hitTest(c, 20.0f, 1.0f, 2, 0, 0);
    else if (e == 0) hit = hitTest(c, 20.0f, 1.0f, 1, 0, 0);
    if (hit) {
        endStop(c);
        return;
    }
    moveByVel(c);
    o.F(0x38) = o.F(0x38) - 1.0f;
    fxFollow(c, 0x30);
}

// ---------------------------------------------------------------------------------------------------------------
// J073: E14 cymbal wave. launch (vt+0x28 at 0x1cbcb0, disassembly): velocity owner forward * 35 (DAT_00463bf8),
// position, +0x40 = 0. tick FUN_001cbd20: for 19 ticks: FUN_00143150 (result unused), hit sphere 20 -> SE 0xc5 +
// effect 0x17; move; then removed. No effect of its own.
// ---------------------------------------------------------------------------------------------------------------
void j073Launch(Chr& c, Chr& owner, const Vec3f& at) {
    launchHead(c, owner, at, 35.0f);
    od(c).I(0x40) = 0;
}
void j073Tick(Chr& c) {
    ObjData& o = od(c);
    ownerCheck(c);
    if (o.I(0x40) >= 0x13) {
        entityRemove(c);
        return;
    }
    wallTest(c, 20.0f, nullptr);   // 0x463c00, result unused
    if (hitTest(c, 20.0f, 1.0f, 0, 0, 0)) {
        objSe3d(0xc5, pos(c));
        fxAt(0x17, pos(c), nullptr, k100);
    }
    moveByVel(c);
    o.I(0x40) = o.I(0x40) + 1;
}

// ---------------------------------------------------------------------------------------------------------------
// J074: C27A battery shot. launch FUN_001cbf60: velocity = normalise(owner +0x8c..+0x94) * 5 (the NPC's battery
// direction, npc_class C27A), effect 0x93. tick FUN_001cc070: wall (r 10) or hit sphere 20 power 0 (m3 0x800) ->
// SE 0x7f, effect 0x94 (yaw of the velocity), end (kill); else move, x / z * 1.2 for 10 ticks, the effect follows.
// ---------------------------------------------------------------------------------------------------------------
void j074Launch(Chr& c, Chr& owner, const Vec3f& at) {
    ObjData& o = od(c);
    o.P(0x24) = &owner;
    o.I(0x28) = static_cast<int32_t>(owner.serial);
    o.I(0x2c) = static_cast<int32_t>(owner.id);
    Vec3f d = owner.xf.m.fwd;   // UNVERIFIED fallback while the NPC module provides no +0x8c direction
    float nd[3];
    if (projectileHooks().ownerDirection && projectileHooks().ownerDirection(owner, nd)) d = {nd[0], nd[1], nd[2]};
    normalize(d);
    setVel(o, {d[0] * 5.0f, d[1] * 5.0f, d[2] * 5.0f});   // DAT_00463c08
    pos(c) = at;
    o.I(0x30) = fxAt(0x93, at, nullptr, k100);
    o.I(0x40) = 0;
}
void j074End(Chr& c) {
    objSe3d(0x7f, pos(c));
    const Vec3f rot{0.0f, yawDeg(vel(od(c))), 0.0f};
    fxAt(0x94, pos(c), &rot, k100);
    endKill(c);   // FUN_001cc300
}
void j074Tick(Chr& c) {
    ObjData& o = od(c);
    ownerCheck(c);
    if (wallTest(c, 10.0f, nullptr)) {   // 0x463c10
        j074End(c);
        return;
    }
    if (hitTest(c, 20.0f, 0.0f, 0, 0, 0x800)) {
        j074End(c);
        return;
    }
    moveByVel(c);
    if (o.I(0x40) < 10) {
        o.I(0x40) = o.I(0x40) + 1;
        o.F(0x34) = o.F(0x34) * 1.2f;
        o.F(0x3c) = o.F(0x3c) * 1.2f;
    }
    fxFollow(c, 0x30);
}

}  // namespace

ProjectileHooks& projectileHooks() {
    static ProjectileHooks h;
    return h;
}

void projConstruct2(Chr& c) { (void)c; }   // the other ctors only build the base object (FUN_0012ae60)

void projTick2(Chr& c) {
    const uint32_t t = c.id;
    if (t == tagOf("J065")) j065Tick(c);
    else if (t == tagOf("J066")) j066Tick(c);
    else if (t == tagOf("J068")) j068Tick(c);
    else if (t == tagOf("J070")) j070Tick(c);
    else if (t == tagOf("J071")) j071Tick(c);
    else if (t == tagOf("J072")) j072Tick(c);
    else if (t == tagOf("J073")) j073Tick(c);
    else if (t == tagOf("J074")) j074Tick(c);
}

void projLaunch2(Chr& c, Chr& owner, const Vec3f& at, int bone) {
    (void)bone;   // no class reads the bone argument
    const uint32_t t = c.id;
    if (t == tagOf("J065")) j065Launch(c, owner, at);
    else if (t == tagOf("J066")) j066Launch(c, owner, at);
    else if (t == tagOf("J068")) j068Launch(c, owner, at);
    else if (t == tagOf("J070")) j070Launch(c, owner, at);
    else if (t == tagOf("J071")) j071Launch(c, owner, at);
    else if (t == tagOf("J072")) j072Launch(c, owner, at);
    else if (t == tagOf("J073")) j073Launch(c, owner, at);
    else if (t == tagOf("J074")) j074Launch(c, owner, at);
}

// ---- dispatch (objects_classes.cpp: ObjClass::Projectile) ----
void projConstruct(Chr& c) {
    const uint32_t t = c.id;
    if (t == tagOf("J030")) j030Ctor(c);
    else if (t == tagOf("J051")) j051Ctor(c);
    else projConstruct2(c);
}

void projTick(Chr& c) {
    const uint32_t t = c.id;
    if (const BulletSpec* b = bulletOf(t)) bulletTick(c, *b);
    else if (t == tagOf("J030")) j030Tick(c);
    else if (t == tagOf("J051")) j051Tick(c);
    else if (t == tagOf("J052")) j052Tick(c);
    else if (t == tagOf("J054")) j054Tick(c);
    else if (t == tagOf("J055")) spitTick(c, true);
    else if (t == tagOf("J056")) spitTick(c, false);
    else if (t == tagOf("J063")) j063Tick(c);
    else if (t == tagOf("J064")) j064Tick(c);
    else projTick2(c);
}

// vtable +8 (class destructor) side effects, run by the reaper when the record is deleted
void projDestroy(Chr& c) {
    if (c.id == tagOf("J030")) fxStop(od(c).I(0x2c));   // FUN_001c4150: FUN_00112560(+0x2c)
}

// vt+0x28 (owner, spawn position, owner bone)
void projLaunch(Chr& c, Chr& owner, const Vec3f& at, int bone) {
    const uint32_t t = c.id;
    if (const BulletSpec* b = bulletOf(t)) bulletLaunch(c, owner, at, *b);
    else if (t == tagOf("J030")) j030Launch(c, owner, at);
    else if (t == tagOf("J051")) j051Launch(c, owner, at);
    else if (t == tagOf("J052")) j052Launch(c, owner, at);
    else if (t == tagOf("J054")) j054Launch(c, owner, at);
    else if (t == tagOf("J055")) spitLaunch(c, owner, at, 1.5707964f, 0x37);   // FUN_00206be0(0x3fc90fdb)
    else if (t == tagOf("J056")) spitLaunch(c, owner, at, -1.5707964f, 0x39);  // FUN_00206be0(0xbfc90fdb)
    else if (t == tagOf("J063")) j063Launch(c, owner, at);
    else if (t == tagOf("J064")) j064Launch(c, owner, at);
    else projLaunch2(c, owner, at, bone);
}

}  // namespace baroque::game
