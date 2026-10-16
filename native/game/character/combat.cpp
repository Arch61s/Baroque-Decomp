// Combat, translated from the functions listed in combat.hpp (work/decompiled/<addr>.c, ENEMIES_COMBAT.md §4).
// Constants read from the ELF: 0x44a728 = 30.0f (knockback speed), 0x44a730 "ST02", 0x44a738 "E19", 0x44a740 "E12",
// 0x44b088 "SD21", 0x44b090 "ST19", 0x44b0f8 "ST18", 0x44bd60 = 1.0f (ground probe radius), 0x44bde0 "C09A",
// 0x44bde8 "ST07", 0x44bdf0 "J030", 0x44bdf8 "FBLW", 0x44be00 "DTNT", 0x44be08 "C10A", 0x44be10 "C19",
// 0x44be18 "C08B", 0x44be20 "C08C", 0x48e4e0 level-up HP table {2,3,3,4,4,4,4,5,5,5,5,5,5,5,6,6,6,6,7,7,8}.
// Call arguments recovered from the disassembly: FUN_0014ad00 gets (f12 R, f13 power, a0 mgr, a1 attacker,
// a2 hitId, a3 pos, t0..t2 m1..m3, t3 weaponTag, stack owner); the hit-slot testers leave t3 unset (UNVERIFIED:
// weaponTag = 0 here) and pass owner 0. Hit_a/b/c pass type 1 (stack word) to the slot constructors.
#include "game/character/combat.hpp"

#include "alchemy/ps2_float.hpp"
#include "game/character/act_props.hpp"
#include "game/character/boss_classes.hpp"
#include "game/character/character.hpp"
#include "game/character/player_control.hpp"
#include "game/character/status_set.hpp"
#include "game/camera.hpp"
#include "game/dungeon.hpp"
#include "game/flag_manager.hpp"
#include "game/game_world.hpp"
#include "game/hud/hud_api.hpp"
#include "game/items/chr_adapter.hpp"
#include "game/items/item_world.hpp"
#include "game/objects/objects.hpp"
#include "game/sound_stub.hpp"
#include "platform/platform.hpp"

#include <cmath>
#include <cstdlib>
#include <cstring>

#pragma fenv_access(on)

namespace baroque::game {

namespace {

HitManager gHits;
// construct-on-first-use: the game build installs services from another translation unit during static init
CombatHooks& hooksStorage() {
    static CombatHooks h;
    return h;
}
CombatHooks& gHooks = hooksStorage();

Rng& rng() { return gameWorld().rng; }
Chr* player() { return characters().player(); }
bool isPlayer(const Chr* c) { return c != nullptr && c == characters().player(); }

uint32_t tag3(uint32_t id) { return id & 0xffffffu; }
uint32_t tagOf4(const std::string& s) {
    uint32_t v = 0;
    for (size_t i = 0; i < 4 && i < s.size(); ++i) v |= uint32_t(uint8_t(s[i])) << (8 * i);
    return v;
}
constexpr uint32_t tagOf(const char (&s)[5]) {
    return uint32_t(uint8_t(s[0])) | uint32_t(uint8_t(s[1])) << 8 | uint32_t(uint8_t(s[2])) << 16 |
           uint32_t(uint8_t(s[3])) << 24;
}

// FUN_002071e0(0x53eb20, n) as the callers convert it: u64 -> float
float randUnit32768() { return static_cast<float>(rng().rand(0x8000)) / 32768.0f; }

const data::PrmChar* prmOf(uint32_t tag) {   // FUN_00133560
    const data::PrmCharFile* f = characters().prm;
    return f != nullptr ? data::findPrmChar(*f, tag) : nullptr;
}

void se3d(int id, const Vec3f& pos) { sound().playSe3d(1.0f, id, pos.data()); }   // FUN_00115470(1.0, snd, id, pos)
void effect(int fx, const Vec3f& pos, const Vec3f* rot = nullptr, float scale = 100.0f) {
    if (gHooks.effect) gHooks.effect(fx, pos, rot, scale);
}
void log(int msg, int value = 0) {
    if (gHooks.log) gHooks.log(msg, value);
}
int weaponClass() { return gHooks.weaponClass ? gHooks.weaponClass() : -1; }   // FUN_001d7d10
std::vector<EquipEffect> equipEffects() {
    std::vector<EquipEffect> v;
    if (gHooks.equipEffects) gHooks.equipEffects(v);
    return v;
}

// Bone world position at the current tick: (bone matrix x model world matrix).translation
// (FUN_0039d860 copy, FUN_0039d450 multiply, FUN_0039d9a0 translation).
Vec3f boneWorld(Chr& c, int bone) {
    const alchemy::Mat4 world = chrActorMatrix(c);
    const std::vector<alchemy::Mat4>& pose = chrTickPose(c);
    if (bone < 0 || static_cast<size_t>(bone) >= pose.size()) return {world.m[12], world.m[13], world.m[14]};
    const alchemy::Mat4 m = alchemy::mul(pose[static_cast<size_t>(bone)], world);
    return {m.m[12], m.m[13], m.m[14]};
}

}  // namespace

HitManager& hitManager() { return gHits; }
CombatHooks& combatHooks() { return hooksStorage(); }

// =============================================================================================================
// Hit manager
// =============================================================================================================
void HitManager::reset() {
    for (HurtVolume& v : victims) {
        v.ent = nullptr;
        for (HurtReg& r : v.regs) r.attacker = nullptr;
    }
    for (HitSlot& s : slots) s.owner = nullptr;
    frame = 0;
}

int HitManager::freeVictim() const {
    for (int i = 0; i < 0x40; ++i)
        if (victims[static_cast<size_t>(i)].ent == nullptr) return i;
    return -1;
}

int HitManager::addSphere(float r, Chr* ent) {
    const int i = freeVictim();
    if (i < 0) return i;
    HurtVolume& v = victims[static_cast<size_t>(i)];
    v.ent = ent;
    v.r = r;
    v.a = v.b = v.c = -1.0f;
    return i;
}

int HitManager::addCylinder(float r, float height, Chr* ent) {
    const int i = freeVictim();
    if (i < 0) return i;
    HurtVolume& v = victims[static_cast<size_t>(i)];
    v.ent = ent;
    v.r = r;
    v.a = height;
    v.b = -1.0f;
    v.c = -1.0f;
    v.topY = v.a + ent->xf.m.pos[1];
    return i;
}

int HitManager::addBox(float height, float b, float c, Chr* ent) {
    const int i = freeVictim();
    if (i < 0) return i;
    HurtVolume& v = victims[static_cast<size_t>(i)];
    v.ent = ent;
    v.a = height;
    v.b = b;
    v.c = c;
    v.r = std::sqrt(b * b + c * c);
    v.topY = v.a + ent->xf.m.pos[1];
    return i;
}

void HitManager::removeVictim(int index) {
    if (index >= 0 && index < 0x40) victims[static_cast<size_t>(index)].ent = nullptr;
}

HitSlot* HitManager::freeSlot() {
    for (HitSlot& s : slots)
        if (s.owner == nullptr) return &s;
    return nullptr;
}

void HitManager::hitEnd(const Chr* owner, int hitId, int type) {
    for (HitSlot& s : slots) {
        if (s.owner != owner || s.hitId != hitId) continue;
        const bool act = (s.flags & 0x100u) != 0;
        if ((type == 1 && act) || (type == 0 && !act)) {
            s.owner = nullptr;
            return;
        }
    }
}

void HitManager::removeOwner(const Chr* owner, int type) {
    for (HitSlot& s : slots) {
        if (s.owner != owner) continue;
        const bool act = (s.flags & 0x100u) != 0;
        if ((type == 1 && act) || (type == 0 && !act)) s.owner = nullptr;
    }
}

void HitManager::forget(const Chr* c) {
    for (HitSlot& s : slots)
        if (s.owner == c) s.owner = nullptr;
    for (HurtVolume& v : victims) {
        if (v.ent == c) v.ent = nullptr;
        for (HurtReg& r : v.regs)
            if (r.attacker == c) r.attacker = nullptr;
    }
}

// orig FUN_0014a9f0 (+ FUN_0014a990 find, FUN_0014a940 free entry)
HurtReg* HitManager::registerHit(int victim, Chr* attacker, int hitId, uint32_t m1) {
    HurtVolume& v = victims[static_cast<size_t>(victim)];
    for (HurtReg& r : v.regs) {
        if (r.attacker == attacker && r.hitId == hitId) {
            if (r.stamp != frame) {
                r.stamp = frame;
                r.flags &= 0xfffffffeu;
            }
            return nullptr;
        }
    }
    for (HurtReg& r : v.regs) {
        if (r.attacker != nullptr) continue;
        r.attacker = attacker;
        r.hitId = hitId;
        r.stamp = frame;
        r.m1 = m1;
        r.flags = 1;
        if (isPlayer(attacker) && static_cast<unsigned>(weaponClass()) < 3u) r.flags |= 0x10000u;
        return &r;
    }
    return nullptr;   // UNVERIFIED: the original writes through a null entry when all 32 are in use
}

// orig FUN_0014ad00 with the shape tests FUN_0014af70 (sphere), FUN_0014afd0 (cylinder), FUN_0014b060 (box)
bool HitManager::testPoint(float R, float power, Chr* attacker, int hitId, const Vec3f& p, uint32_t m1, uint32_t m2,
                           uint32_t m3, uint32_t weaponTag, const Chr* exclude) {
    bool any = false;
    for (int i = 0; i < 0x40; ++i) {
        HurtVolume& v = victims[static_cast<size_t>(i)];
        Chr* e = v.ent;
        if (e == nullptr || (e->taskFlags & 0x10000000u) != 0 || e == attacker || e == exclude) continue;
        // vt+0x20: FUN_0012d400 (+0x24 bit 0) for characters / items, the class entry for objects (FUN_0012d390
        // model loaded, FUN_0012a5d0 always) -- doors O??N and walls O??M are struck through their box volumes
        const bool vtActive = e->obj != nullptr ? objectActive(*e) : e->active();
        if (!vtActive || !(0.0f < v.r)) continue;
        const Vec3f& ep = e->xf.m.pos;
        // disasm 0x14adbc..0x14ae94: the three tests are not exclusive -- a sphere volume (a = b = -1) whose sphere
        // test fails is retried as a cylinder (topY = ent.y + a, refreshed by FUN_0014e1f0 for every volume)
        bool hit = false;
        if (v.a < 0.0f) {
            const float dy = ep[1] - p[1], dx = ep[0] - p[0], dz = ep[2] - p[2];
            hit = dz * dz + dx * dx + dy * dy < (R + v.r) * (R + v.r);
        }
        if (!hit && v.b < 0.0f) {
            const float dx = ep[0] - p[0], dz = ep[2] - p[2];
            if (!((R + v.r) * (R + v.r) < dz * dz + dx * dx + 0.0f))
                hit = p[1] - R < v.topY && !(p[1] + R <= ep[1]);
        } else if (!hit && 0.0f < v.b) {
            const ChrTransform& t = e->xf.m;
            float f = (t.fwd[2] * p[2] + t.fwd[0] * p[0] + t.fwd[1] * p[1]) -
                      (t.fwd[2] * ep[2] + t.fwd[0] * ep[0] + t.fwd[1] * ep[1]);
            if (f < 0.0f) f = -f;
            if (!(R + v.c < f)) {
                float r = (t.right[2] * p[2] + t.right[0] * p[0] + t.right[1] * p[1]) -
                          (t.right[2] * ep[2] + t.right[0] * ep[0] + t.right[1] * ep[1]);
                if (r < 0.0f) r = -r;
                if (!(R + v.b < r) && !(v.topY <= p[1] - R)) hit = ep[1] < p[1] + R;
            }
        }
        if (!hit) continue;
        HurtReg* reg = registerHit(i, attacker, hitId, m1);
        if (reg == nullptr) continue;
        uint32_t a = m1, b = m2, c = m3;
        if (onHit(power, *e, attacker, a, b, c, weaponTag, hitId, const_cast<Chr*>(exclude)) == 1) {
            any = true;
            reg->m1 = a;
        } else {
            reg->flags |= 2u;
        }
    }
    return any;
}

// orig FUN_0014eae0: samples from `from` toward `to` every R units (the last sample excluded)
void HitManager::sweep(float R, HitSlot& s, const Vec3f& from, const Vec3f& to) {
    Vec3f d{to[0] - from[0], to[1] - from[1], to[2] - from[2]};
    float len = normalize(d);
    d = {d[0] * R, d[1] * R, d[2] * R};
    Vec3f p = from;
    while (R < len) {
        p = {p[0] + d[0], p[1] + d[1], p[2] + d[2]};
        testPoint(s.radius, s.power, s.owner, s.hitId, p, s.m1, s.m2, s.m3, 0, nullptr);
        len = len - R;
    }
}

// orig FUN_0014e3e0
void HitManager::sphere(HitSlot& s) {
    const Vec3f prev = s.posA;
    s.posA = boneWorld(*s.owner, s.boneA);
    testPoint(s.radius, s.power, s.owner, s.hitId, s.posA, s.m1, s.m2, s.m3, 0, nullptr);
    if ((s.flags & 1u) != 0) sweep(s.radius, s, s.posA, prev);
}

// orig FUN_0014e510: capsule from bone A to bone B sampled every R; each sample also sweeps back to its previous
// position (the previous segment interpolated at the same step)
void HitManager::capsule(HitSlot& s) {
    const Vec3f oldA = s.posA, oldB = s.posB;
    s.posA = boneWorld(*s.owner, s.boneA);
    s.posB = boneWorld(*s.owner, s.boneB);
    const float R = s.radius;
    Vec3f prevStep{oldB[0] - oldA[0], oldB[1] - oldA[1], oldB[2] - oldA[2]};
    normalize(prevStep);
    prevStep = {prevStep[0] * R, prevStep[1] * R, prevStep[2] * R};
    Vec3f curStep{s.posB[0] - s.posA[0], s.posB[1] - s.posA[1], s.posB[2] - s.posA[2]};
    float len = normalize(curStep);
    curStep = {curStep[0] * R, curStep[1] * R, curStep[2] * R};
    Vec3f cur = s.posA, prev = oldA;
    do {
        testPoint(R, s.power, s.owner, s.hitId, cur, s.m1, s.m2, s.m3, 0, nullptr);
        if ((s.flags & 1u) != 0) sweep(R, s, cur, prev);
        prev = {prev[0] + prevStep[0], prev[1] + prevStep[1], prev[2] + prevStep[2]};
        len = len - R;
        cur = {cur[0] + curStep[0], cur[1] + curStep[1], cur[2] + curStep[2]};
    } while (R < len);
}

// orig FUN_0014e810: like the capsule, end point = bone A + ofs rotated by the bone matrix (FUN_003a0330)
void HitManager::offset(HitSlot& s) {
    const Vec3f oldA = s.posA, oldB = s.posB;
    Chr& c = *s.owner;
    const alchemy::Mat4 world = chrActorMatrix(c);
    const std::vector<alchemy::Mat4>& pose = chrTickPose(c);
    alchemy::Mat4 m = world;
    if (s.boneA >= 0 && static_cast<size_t>(s.boneA) < pose.size()) m = alchemy::mul(pose[static_cast<size_t>(s.boneA)], world);
    s.posA = {m.m[12], m.m[13], m.m[14]};
    const float mm[12] = {m.m[0], m.m[1], m.m[2], m.m[3], m.m[4], m.m[5], m.m[6], m.m[7], m.m[8], m.m[9], m.m[10], m.m[11]};
    const Vec3f o = mulVec3(s.ofs, mm);
    s.posB = {s.posA[0] + o[0], s.posA[1] + o[1], s.posA[2] + o[2]};
    const float R = s.radius;
    Vec3f prevStep{oldB[0] - oldA[0], oldB[1] - oldA[1], oldB[2] - oldA[2]};
    normalize(prevStep);
    prevStep = {prevStep[0] * R, prevStep[1] * R, prevStep[2] * R};
    Vec3f curStep{s.posB[0] - s.posA[0], s.posB[1] - s.posA[1], s.posB[2] - s.posA[2]};
    float len = normalize(curStep);
    curStep = {curStep[0] * R, curStep[1] * R, curStep[2] * R};
    Vec3f cur = s.posA, prev = oldA;
    do {
        testPoint(R, s.power, s.owner, s.hitId, cur, s.m1, s.m2, s.m3, 0, nullptr);
        if ((s.flags & 1u) != 0) sweep(R, s, cur, prev);
        prev = {prev[0] + prevStep[0], prev[1] + prevStep[1], prev[2] + prevStep[2]};
        len = len - R;
        cur = {cur[0] + curStep[0], cur[1] + curStep[1], cur[2] + curStep[2]};
    } while (R < len);
}

// orig FUN_0014e1b0: topY refresh FUN_0014e1f0, slot tests FUN_0014e300, registration expiry FUN_0014e240/FUN_0014e2b0
void HitManager::tick() {
    alchemy::ps2::Ps2FloatScope fp;
    for (HurtVolume& v : victims)
        if (v.ent != nullptr) v.topY = v.a + v.ent->xf.m.pos[1];
    for (HitSlot& s : slots) {
        if (s.owner == nullptr) continue;
        if (s.boneB < 0) {
            if (s.boneB == -2) offset(s);
            else sphere(s);
        } else {
            capsule(s);
        }
        s.flags |= 1u;
    }
    for (HurtVolume& v : victims) {
        if (v.ent == nullptr) continue;
        for (HurtReg& r : v.regs)
            if (r.attacker != nullptr && r.stamp != frame) r.attacker = nullptr;
    }
    ++frame;
}

// orig FUN_0014df50 + FUN_0014e020 (victims whose registration by `ent` is from this frame and dealt damage)
std::vector<Chr*> HitManager::victimsHitBy(const Chr* ent) const {
    std::vector<Chr*> out;
    for (const HurtVolume& v : victims) {
        if (v.ent == nullptr) continue;
        for (const HurtReg& r : v.regs) {
            // FUN_0014e020: registration by `ent` with the new bit (this frame) and without the no-damage bit
            if (r.attacker == ent && (r.flags & 1u) != 0 && (r.flags & 2u) == 0) {
                out.push_back(v.ent);
                break;
            }
        }
    }
    return out;
}

// =============================================================================================================
// ACT commands
// =============================================================================================================
namespace {

// orig FUN_00147410: attribute codes -> m1 (element), m2 (status), m3 (effects)
void decodeAttributes(const data::ActCmnd& c, uint32_t& m1, uint32_t& m2, uint32_t& m3) {
    m1 = m2 = m3 = 0;
    for (int k = 0; k < 4; ++k) {
        const int code = static_cast<size_t>(2 + k) < c.args.size() ? c.args[static_cast<size_t>(2 + k)].i : 0;
        switch (code) {
        case 1: m3 |= 2; break;
        case 2: m3 |= 4; break;
        case 3: m3 |= 8; break;
        case 4: m3 |= 0x10; break;
        case 5: m1 |= 1; break;
        case 6: m1 |= 2; break;
        case 7: m1 |= 4; break;
        case 8: m3 |= 1; m1 |= 1; break;
        case 9: m2 |= 1; break;
        case 10: m2 |= 2; break;
        case 0xb: m2 |= 4; break;
        case 0xc: m2 |= 8; break;
        case 0xd: m2 |= 0x10; break;
        case 0xe: m2 |= 0x20; break;
        case 0xf: m2 |= 0x40; break;
        case 0x10: m2 |= 0x80; break;
        default:
            if (code >= 0x11 && code <= 0x26) m3 |= 0x20u << (code - 0x11);
            break;
        }
    }
}

// room of the character's cell (chr +0x30): word & 0xf when (word & 0xf000) == 0x1000, else -1
int cellRoom(const Chr& c) {
    if (c.cell == nullptr) return -1;
    const uint32_t w = *c.cell;
    return (w & 0xf000u) == 0x1000u ? static_cast<int>(w & 0xfu) : -1;
}

// orig FUN_00149e40 (ACT Warp): 1 = gravity scale 1.0 and a random cell outside the player's room (FUN_00124660);
// 3 = with AI flag 0x100 the AI's warp point (FUN_001ef820); otherwise a random cell of the own room (FUN_00124620).
void actWarp(Chr& owner, int mode) {
    if (mode == 1) {
        owner.phys.gravity = 1.0f;   // phys +0x50
        Chr* pl = player();
        Dungeon* d = gameWorld().dungeon;
        if (pl == nullptr || d == nullptr) return;
        float p[3] = {0.0f, 0.0f, 0.0f};
        const int room = pl->cell != nullptr ? static_cast<int>(*pl->cell & 0xfu) : -1;
        if (d->randomCellOutside(p, room, 0x1000) != 1) return;
        owner.xf.m.pos = {p[0], p[1], p[2]};
        return;
    }
    if (mode == 3 && (owner.ai.flags & 0x100u) != 0) {
        Vec3f p;   // FUN_001ef820 (ai.cpp): free point 400..600 around the AI target
        if (aiWarpPoint(owner, p)) owner.xf.m.pos = p;
        return;
    }
    const int room = cellRoom(owner);
    if (room == -1) return;
    Vec3 out;
    if (gameAdapter().randomCellInRoom(room, out)) owner.xf.m.pos = {out.x, out.y, out.z};   // FUN_00124620 == 1
}

int boneIndex(const Chr& c, const std::string& name) {   // actor+0x48 vt+0x70 (joint index by name)
    const alchemy::Skeleton* sk = c.model.skeleton();
    if (sk == nullptr) return -1;
    for (size_t b = 0; b < sk->bones.size(); ++b)
        if (sk->bones[b].name == name) return static_cast<int>(b);
    return -1;   // UNVERIFIED: the original's lookup result for an unknown name
}

// Sword reach extension shared by ST15 and the 0x1d records of FUN_001462c0: the Hit_c offset scaled in place (the
// hit slot is built from it afterwards), SE 0xb0 at the owner, ACT flag 0x400000 (HitEnd stops effect slot 7).
// UNVERIFIED: the effect 0xc attached to the bone in ACT effect slot 7 (6 when 7 is busy) through FUN_00144b30 /
// FUN_00112770 and the FUN_0013de20 flash are not translated.
void swingReach(ActRunner& act, Chr& owner, Vec3f& ofs, float k) {
    ofs = {ofs[0] * k, ofs[1] * k, ofs[2] * k};
    se3d(0xb0, owner.xf.m.pos);   // FUN_00115470(1.0, snd, 0xb0, owner pos)
    act.orFlags(0x400000u);
}

// orig FUN_001462c0(act, bone, ofs) (player only; Hit_a / Hit_b pass no bone / offset): brand 'ST14' (0x44b340) shot
// FUN_001e6550 with a sword (weapon class < 3) under PRMITEM ST14 +0x78 (0x778); brand 'ST15' (0x44b348) reach x1.5
// under ST15 +0x78 (0x7f8) on Hit_c; otherwise a second rand(99) against the equipped category-2 records: 0x10 / 0x11 /
// 0x12 element shots FUN_001e70c0(player, bone, 1 / 2 / 4, FUN_001344f0(-1)), 0x13 shot FUN_001e6550, 0x1d reach =
// unit offset * record +2 (a failed 0x1d roll subtracts its chance).
// UNVERIFIED: the shot objects FUN_001e6550 / FUN_001e70c0 (type-5 entities, vtables 0x4cf0f0 / 0x4cf060) are not
// translated; their rolls end the function as in the original.
void playerSwingEffects(ActRunner& act, Chr& owner, Vec3f* ofs) {
    if (!isPlayer(&owner)) return;
    const uint32_t r = rng().rand(99);
    const uint32_t brand = owner.status.block.brand;
    if (brand == tagOf("ST14")) {
        if (static_cast<unsigned>(weaponClass()) < 3u && gHooks.brandValue &&
            r < static_cast<uint32_t>(gHooks.brandValue(14))) {
            return;   // FUN_001e6550(player, FUN_001344f0(st, -1)) + FUN_0013de20 (not translated)
        }
    } else if (brand == tagOf("ST15") && ofs != nullptr && gHooks.brandValue &&
               r < static_cast<uint32_t>(gHooks.brandValue(15))) {
        Vec3f d = *ofs;
        const float len = normalize(d);   // FUN_00206ab0 normalises in place and returns the length
        *ofs = d;
        swingReach(act, owner, *ofs, len * 1.5f);
        return;
    }
    uint32_t r2 = rng().rand(99);
    for (const EquipEffect& e : equipEffects()) {
        if (e.category != 2) continue;
        const uint32_t ch = static_cast<uint32_t>(static_cast<int>(static_cast<int16_t>(e.chance)));
        if (e.type >= 0x10 && e.type <= 0x13) {
            if (r2 < ch) return;   // FUN_001e70c0 / FUN_001e6550 shot (not translated)
        } else if (e.type == 0x1d) {
            if (r2 < ch) {
                if (ofs == nullptr) return;   // PORT: Hit_a / Hit_b pass a null offset (the original normalises it)
                normalize(*ofs);
                swingReach(act, owner, *ofs, static_cast<float>(e.value));
                return;
            }
            r2 = r2 - ch;
        }
    }
}

}  // namespace

void actCombatCommand(Chr& owner, ActRunner& act, const data::ActCmnd& c) {
    using data::ActCommand;
    auto argI = [&](size_t i) { return i < c.args.size() ? c.args[i].i : 0; };
    auto argF = [&](size_t i) { return i < c.args.size() ? c.args[i].f : 0.0f; };
    auto argS = [&](size_t i) { return i < c.args.size() ? c.args[i].s : std::string(); };
    switch (c.cmd) {
    case ActCommand::Hit_a:
    case ActCommand::Hit_b:
    case ActCommand::Hit_c: {
        uint32_t m1, m2, m3;
        decodeAttributes(c, m1, m2, m3);
        Vec3f ofs{};
        if (c.cmd == ActCommand::Hit_c) ofs = {argF(8), argF(9), argF(10)};
        playerSwingEffects(act, owner, c.cmd == ActCommand::Hit_c ? &ofs : nullptr);   // FUN_001462c0
        HitSlot* s = gHits.freeSlot();
        if (s != nullptr) {
            s->owner = &owner;
            s->hitId = argI(0);
            s->radius = argF(6);
            s->boneA = boneIndex(owner, argS(7));
            s->boneB = c.cmd == ActCommand::Hit_a ? -1 : c.cmd == ActCommand::Hit_b ? boneIndex(owner, argS(8)) : -2;
            s->ofs = c.cmd == ActCommand::Hit_c ? ofs : Vec3f{0.0f, 0.0f, 0.0f};
            s->m1 = m1;
            s->m2 = m2;
            s->m3 = m3;
            s->power = argF(1);
            s->flags = 0x100;
        }
        act.orFlags(0x2000000u);
        break;
    }
    case ActCommand::HitEnd:
        gHits.hitEnd(&owner, argI(0), 1);
        if ((act.flags() & 0x400000u) != 0) act.clearFlags(0x400000u);   // + FUN_00144ac0(act, 7, 1) effect track stop
        break;
    case ActCommand::MoveAcc:   // FUN_00147a70
        if (owner.phys.actMoveSpeed != 0.0f) owner.phys.actMoveAcc = argF(0);
        break;
    case ActCommand::MoveJump: {   // FUN_00147aa0 (string 0x44b790 "E02")
        const float speed = argF(0);
        if (speed == 0.0f) {
            owner.phys.actMoveSpeed = 0.0f;
            owner.phys.actMoveAcc = 0.0f;
        } else {
            owner.phys.actMoveAngle = argF(1);   // +0x98 (+0x9c = 0)
            owner.phys.flags |= 1u;              // FUN_00142160: airborne, vy = -jump
            owner.phys.vy = -argF(2);
            owner.phys.actMoveSpeed = speed;
            owner.phys.actMoveAcc = 0.0f;
            if (tag3(owner.id) == tag3(tagOf("E02 "))) owner.phys.gravity = 0.0f;
        }
        break;
    }
    case ActCommand::ChrColls:   // FUN_00149730 -> FUN_00142200(r, h, phys, chr): cylinder push shape
        owner.shape.owner = &owner;
        owner.shape.radius = argF(0);
        owner.shape.height = argF(1);
        owner.shape.a = -1.0f;
        owner.shape.b = -1.0f;
        owner.shape.top = owner.shape.height + owner.xf.m.pos[1];   // FUN_00142270
        break;
    case ActCommand::Invincib:   // 0x149e00: 0 clears, else sets ACT flag 0x1000000
        if (argI(0) == 0) act.clearFlags(0x1000000u);
        else act.orFlags(0x1000000u);
        break;
    case ActCommand::RotYAct: {   // FUN_00149a70: first listed live character (type < 3) with the tag -> FUN_00146060
        const uint32_t tag = tagOf4(argS(0));
        for (Chr* t : characters().list()) {
            if ((t->taskFlags & 0x10000000u) != 0) continue;
            if (t->type < 3 && t->id == tag) {
                owner.actTurn.active = true;     // FUN_00145fa0
                owner.actTurn.rate = argF(1);    // +0x10
                owner.actTurn.ofs = argF(2);     // +0x14
                owner.actTurn.target = t;        // +0x00
                break;
            }
        }
        break;
    }
    case ActCommand::RotYEnd:   // thunk -> FUN_001460d0
        owner.actTurn = ActTurn{};
        break;
    case ActCommand::Fade: {   // FUN_001493f0: FUN_0010a0a0(model, [1] | [2] << 4, [0], [6] < 0 ? null : &[3], &[7])
        const uint32_t mode = static_cast<uint32_t>(argI(1)) | (static_cast<uint32_t>(argI(2)) << 4);
        const float from[4] = {argF(3), argF(4), argF(5), argF(6)};
        const float to[4] = {argF(7), argF(8), argF(9), argF(10)};
        modelFadeStart(owner.model, mode, argI(0), argF(6) < 0.0f ? nullptr : from, to);
        break;
    }
    case ActCommand::FadeEnd:   // FUN_00149470 -> FUN_0010a130
        modelFadeEnd(owner.model);
        break;
    case ActCommand::Object: {   // FUN_00148350 (tag, offset x y z, bone)
        // spawn point = the bone's matrix (actor bone cache) x the published actor matrix, translation, plus the
        // offset rotated by the actor matrix (FUN_003a0330 with the model transform +0x20); kept at ACT +0x50
        const int bone = boneIndex(owner, argS(4));
        const alchemy::Mat4 am = chrActorMatrix(owner);
        alchemy::Mat4 w = am;
        const std::vector<alchemy::Mat4>& pose = chrTickPose(owner);
        if (bone >= 0 && static_cast<size_t>(bone) < pose.size()) w = alchemy::mul(pose[static_cast<size_t>(bone)], am);
        const float ox = argF(1), oy = argF(2), oz = argF(3);
        Vec3f at{w.m[12], w.m[13], w.m[14]};
        for (int k = 0; k < 3; ++k)
            at[static_cast<size_t>(k)] += ox * am.m[static_cast<size_t>(k)] + oy * am.m[static_cast<size_t>(4 + k)] +
                                          oz * am.m[static_cast<size_t>(8 + k)];
        act.objectPos = at;   // ACT +0x50
        const uint32_t tag = tagOf4(argS(0));
        if (tag == tagOf4("ITEM")) {   // 0x44b7a0
            if (owner.type != kChrEnemy && owner.type != kChrNpc) {
                act.orFlags(0x100000u);   // the player: ThrowLock throws the selected item
                break;
            }
            // carried item (status +0x60): FUN_001d5640(item, at, normalise(fwd.x, 0.5, fwd.z) * 30, owner)
            if (owner.status.block.carried != nullptr) {
                Vec3f d{owner.xf.m.fwd[0], 0.5f, owner.xf.m.fwd[2]};
                normalize(d);
                throwHeldItem(*static_cast<Item*>(owner.status.block.carried), Vec3{at[0], at[1], at[2]},
                              Vec3{d[0] * 30.0f, d[1] * 30.0f, d[2] * 30.0f}, chrAdapter(&owner), itemSystem());
            }
            break;
        }
        if (Chr* e = characters().createTag(tag, true)) objectLaunch(*e, owner, at, bone);   // FUN_0012ce70 + vt+0x28
        break;
    }
    case ActCommand::SetFlag:   // FUN_00149ff0: [1] == 0 clears flag [0], else sets it (FUN_00178a30 / FUN_00178890)
        if (argI(1) == 0) flags().clear(static_cast<uint32_t>(argI(0)));
        else flags().set(static_cast<uint32_t>(argI(0)));
        break;
    case ActCommand::ChkFlag:   // FUN_0014a040: run the list when FUN_00178a80(flag [0]) == [1]
        if (argI(1) == (flags().test(static_cast<uint32_t>(argI(0))) ? 1 : 0)) act.runList(c.body);
        break;
    case ActCommand::Destroy:   // FUN_00149b80 -> FUN_00207350(owner)
        if ((owner.taskFlags & 1u) != 0) owner.taskFlags |= 0x30000000u;
        break;
    case ActCommand::SetPos: {   // FUN_00149c70: room-relative in a room cell, then FUN_00128ae0([3] deg)
        Vec3f p{argF(0), argF(1), argF(2)};
        const int room = cellRoom(owner);
        if (room != -1) p = {argF(0) + hooks::roomOriginX(room), argF(1), argF(2) + hooks::roomOriginZ(room)};
        owner.xf.m.pos = p;
        rotateAboutUp(owner.xf.m, (argF(3) * 3.1415927f) / 180.0f);
        break;
    }
    case ActCommand::SetScale:   // FUN_00149dc0: transform +0 scale
        owner.xf.scale = {argF(0), argF(1), argF(2)};
        break;
    case ActCommand::AnmScale:   // FUN_00149df0: ACT +0x6c
        act.animScale = argI(0);
        break;
    case ActCommand::RotYPos: {   // FUN_00149980 -> FUN_00145fe0([3], act, room-relative pos)
        Vec3f p{argF(0), argF(1), argF(2)};
        const int room = cellRoom(owner);
        if (room != -1) p = {argF(0) + hooks::roomOriginX(room), argF(1), argF(2) + hooks::roomOriginZ(room)};
        owner.actTurn.active = true;   // FUN_00145fa0
        owner.actTurn.ofs = 0.0f;      // +0x14
        owner.actTurn.rate = argF(3);  // +0x10
        owner.actTurn.pos = p;         // +0x04..+0x0c
        owner.actTurn.target = nullptr;
        break;
    }
    case ActCommand::Warp:   // FUN_00149e40
        actWarp(owner, argI(0));
        break;
    case ActCommand::Enemy: {   // FUN_00149480 (tag, name8, yaw, offset x y z, bone)
        const int bone = boneIndex(owner, argS(6));
        const alchemy::Mat4 am = chrActorMatrix(owner);
        alchemy::Mat4 w = am;
        const std::vector<alchemy::Mat4>& pose = chrTickPose(owner);
        if (bone >= 0 && static_cast<size_t>(bone) < pose.size()) w = alchemy::mul(pose[static_cast<size_t>(bone)], am);
        const float ox = argF(3), oy = argF(4), oz = argF(5);
        Vec3f at{w.m[12], w.m[13], w.m[14]};
        for (int k = 0; k < 3; ++k)
            at[static_cast<size_t>(k)] += ox * am.m[static_cast<size_t>(k)] + oy * am.m[static_cast<size_t>(4 + k)] +
                                          oz * am.m[static_cast<size_t>(8 + k)];
        act.objectPos = at;   // ACT +0x50
        if (Chr* e = characters().createTag(tagOf4(argS(0)), true)) {   // FUN_0012ce70(0x50b1a0, tag, 1)
            e->xf.m.pos = at;
            e->prevPos = at;   // PORT: presentation interpolation
            snapForward(e->xf.m, owner.xf.m.fwd);                                  // FUN_001287a0(e, owner fwd)
            rotateAboutUp(e->xf.m, (argF(2) * 3.1415927f) / 180.0f);              // FUN_00128ae0
            const std::string n = argS(1);                                         // FUN_0023fca0(AI +0x8c, name, 8)
            for (size_t k = 0; k < 8; ++k) e->ai.pending[k] = k < n.size() ? n[k] : '\0';
        }
        break;
    }
    // Track / TrackEx / TrackEnd (weapon trails), NTurnPos / NTurnAct / NTurnEnd (head look-at), ShowBox / HideBox
    // (C12A box node) and the item props ObjThrow / DropItem / ClearItm / GetItem / ShowItem / HideItem: act_props.cpp.
    default:
        if (!actPropCommand(owner, act, c)) bossActCommand(owner, act, c);   // ThrowItm FUN_00148880 (boss_classes.cpp)
        break;
    }
}

// orig FUN_00146220 (start of the ACT tick FUN_00143fe0): AnmScale 1 squashes the transform scale each tick
void actScaleStep(Chr& c) {
    if (c.act.animScale != 1) return;
    Vec3f& s = c.xf.scale;
    const float y = s[1];
    if (0.001f < s[0]) {
        float x = 0.001f, z = 0.001f;
        const float nx = (s[0] + 0.0f) - s[0] * 0.33f;
        if (0.001f <= nx) {
            z = (s[2] + 0.0f) - s[2] * 0.33f;
            x = nx;
        }
        s = {x, y * 1.2f, z};
    }
}

// orig FUN_00146120 (start of the ACT tick FUN_00143fe0): turn toward the turn object's target
void actTurnStep(Chr& c) {
    ActTurn& t = c.actTurn;
    if (!t.active) return;
    if (t.target != nullptr) t.pos = t.target->xf.m.pos;
    float d[3] = {t.pos[0] - c.xf.m.pos[0], 0.0f, t.pos[2] - c.xf.m.pos[2]};
    Vec3f v{d[0], d[1], d[2]};
    normalize(v);                                                    // FUN_00206ab0
    if (t.ofs != 0.0f) {
        float w[3] = {v[0], v[1], v[2]};
        rotateY((t.ofs * 3.1415927f) / 180.0f, w);                   // FUN_00206be0
        v = {w[0], w[1], w[2]};
    }
    turnToward(t.rate, c.xf.m, v, true);                             // FUN_00128fc0(rate, chr, dir, 1)
}

// FUN_00143fe0: an action whose hit slots (0x2000000) registered damage this frame gets 0x4000000 (CollsRes bit 8)
void actHitLatch(Chr& c) {
    const uint32_t f = c.act.flags();
    if ((f & 0x2000000u) == 0 || (f & 0x4000000u) != 0) return;
    if (!gHits.victimsHitBy(&c).empty()) c.act.orFlags(0x4000000u);   // FUN_0014df50(0x50c780, chr) +0x100 != 0
}

// =============================================================================================================
// Status helpers
// =============================================================================================================
int statusHpMax(const StatusObj& s) {
    if (gHooks.hpMaxTotal) return gHooks.hpMaxTotal(s);
    return statusHpMaxTotal(s);
}

// orig FUN_001349e0
bool statusAddHp(StatusObj& s, int d) {
    StatusBlock& b = s.block;
    b.hp = b.hp + d;
    const bool alive = 0 < b.hp;
    if (alive) {
        const int mx = statusHpMax(s);   // FUN_00134960
        if (mx < b.hp) b.hp = mx;
    } else {
        b.hp = 0;
    }
    if (isPlayer(s.owner)) {
        if (gHooks.hudHp) gHooks.hudHp(b.hp);
        flags().clear(0x401);
        flags().clear(0x2fb);
        flags().clear(0x2fc);
        if (b.hp < 0xb) flags().set(0x401);
        else if (b.hp < 0x15) flags().set(0x2fb);
        else if (b.hp < 0x1f) flags().set(0x2fc);
    }
    return alive;
}

namespace {
// player regeneration counters 0x50b1d0 (HP), 0x50b1d8 (VT), 0x50b1e0 (WG12)
int gRegenHp = 0, gRegenVt = 0, gRegenWg12 = 0;
}  // namespace

// orig FUN_00134b60 (+ FUN_001349a0 clamp to the VT maximum)
void statusAddVt(StatusObj& s, int d) {
    StatusBlock& b = s.block;
    b.vt = b.vt + d;
    if (b.vt < 0) {
        b.vt = 0;
    } else {
        const int mx = statusVtMaxTotal(s);   // FUN_001349a0
        if (mx < b.vt) b.vt = mx;
    }
    if (isPlayer(s.owner)) {
        if (gHooks.hudVt) gHooks.hudVt(b.vt);
        flags().clear(0x2f8);
        flags().clear(0x402);
        flags().clear(0x2fa);
        flags().clear(0x2f9);
        if (b.vt == 0) flags().set(0x2f8);
        else if (b.vt < 0xb) flags().set(0x402);
        else if (b.vt < 0x10) flags().set(0x2fa);
        else if (b.vt < 0x1f) flags().set(0x2f9);
        gRegenVt = 0;   // uRam0050b1d8 = 0: every VT change restarts the drain period of FUN_00133bf0
    }
}

// orig FUN_00134740
int statusDef(const StatusObj& s) {
    if (isPlayer(s.owner)) return s.block.defLevel + s.block.defMax + (gHooks.playerDef ? gHooks.playerDef() : 0);
    return s.block.defMax;
}

// orig FUN_001344f0 (string 0x44b068 "SD19"): with the 'SD19' weapon (iRam0053d9b4) the attack is scaled by record
// 1's +6 % when HP <= record 1's +4 % of the HP maximum, else by record 0's +6 % under record 0's +4 %.
int statusAtk(const StatusObj& s, int hand) {
    if (isPlayer(s.owner)) {
        const int w = gHooks.playerAtk ? gHooks.playerAtk(hand) : 0;
        int a = w + s.block.atkMax + s.block.atkLevel;
        const std::vector<EquipEffect> effs = equipEffects();
        const EquipEffect* rec[2] = {nullptr, nullptr};
        for (const EquipEffect& e : effs)
            if (e.slot == 0 && e.itemTag == tagOf("SD19") && e.rec < 2) rec[e.rec] = &e;
        if (rec[0] != nullptr && rec[1] != nullptr) {
            const float hp = static_cast<float>(s.block.hp);
            if (hp <= (static_cast<float>(rec[1]->extra) / 100.0f) * static_cast<float>(statusHpMax(s))) {
                a = alchemy::ps2::ftoi(static_cast<float>(a) *
                                       (static_cast<float>(static_cast<int16_t>(rec[1]->chance)) / 100.0f));
            } else if (hp <= (static_cast<float>(rec[0]->extra) / 100.0f) * static_cast<float>(statusHpMax(s))) {
                a = alchemy::ps2::ftoi(static_cast<float>(a) *
                                       (static_cast<float>(static_cast<int16_t>(rec[0]->chance)) / 100.0f));
            }
        }
        return a;
    }
    return s.block.atkMax;
}

// orig FUN_001347b0: player = FUN_00134310(crit, 5) (critical + the +6 words of the equipped records with +1 == 1 and
// +0 == 5) + FUN_001d8720(0x53d910)
int statusCrit(const StatusObj& s) {
    if (isPlayer(s.owner)) {
        int c = s.block.critical;
        for (const EquipEffect& e : equipEffects())
            if (e.category == 1 && e.type == 5) c = c + static_cast<int16_t>(e.chance);
        return c + (gHooks.playerCritBonus ? gHooks.playerCritBonus() : 0);
    }
    return s.block.critical;
}

// orig FUN_00135d80 (difficulty flags 0x4f1..0x4f4)
float difficultyScale(const StatusObj& s, const Chr* attacker) {
    const bool vicPlayer = isPlayer(s.owner), atkPlayer = isPlayer(attacker);
    if (flags().test(0x4f1)) {
        if (vicPlayer) return 0.5f;
        if (atkPlayer) return 1.5f;
    } else {
        if (flags().test(0x4f2)) return 1.0f;
        if (flags().test(0x4f3)) {
            if (vicPlayer) return 1.25f;
            if (atkPlayer) return 0.75f;
        } else if (flags().test(0x4f4)) {
            if (vicPlayer) return 1.5f;
            if (atkPlayer) return 0.5f;
        }
    }
    return 1.0f;
}

// orig FUN_00135f00
float victimReduction(StatusObj& s, const Chr* attacker) {
    float f = 1.0f - (static_cast<float>(s.block.hidden[4]) / 255.0f) * 0.25f;
    if (isPlayer(s.owner)) {
        const uint32_t r = rng().rand(99);
        for (const EquipEffect& e : equipEffects()) {
            if (e.category != 5) continue;
            if (e.type == 0x1e) {
                if (r < e.chance) f = f * (static_cast<float>(e.value) / 100.0f);
            } else if (e.type == 0x19) {
                if (gHooks.wearItem) gHooks.wearItem(e.item, static_cast<int16_t>(e.chance));
            }
        }
    }
    return f * difficultyScale(s, attacker);
}

// orig FUN_00135c50
float elementWeakness(const StatusObj& s, uint32_t m1) {
    if (isPlayer(s.owner) && gHooks.heldElementMask) m1 &= ~gHooks.heldElementMask();
    if ((m1 & 7u) == 0) return 0.0f;
    int res = -1;
    for (int i = 0; i < 3; ++i) {
        if ((m1 & (1u << i)) == 0) continue;
        const int v = s.block.element[static_cast<size_t>(i)];
        if (res < 0 || v < res) res = v;
    }
    if ((s.block.statusMask & 0x4000u) != 0) res = res + 3;
    if (res < 7) return static_cast<float>(7 - res) / 10.0f;
    return 0.0f;
}

// orig FUN_00137aa0: status duration (frames) and its SE
void statusSetDuration(StatusObj& s, int i) {
    StatusBlock& b = s.block;
    int se = -1;
    int& t = b.timers[static_cast<size_t>(i)];
    switch (i) {
    case 0: se = 0x7f; t = 300; break;
    case 1: se = 0x80; t = 0x1c2; break;
    case 2: se = 0x81; t = 900; break;
    case 3: se = 0x82; t = 0x5dc; break;
    case 4: se = 0x83; t = 0x9f6; break;
    case 5: se = 0x84; t = 600; break;
    case 6:
        t = -1;
        b.atkMax = b.atkMax - 1;
        if (b.atkMax < 0) b.atkMax = 0;
        b.defMax = b.defMax - 1;
        if (b.defMax < 0) b.defMax = 0;
        se = 0x89;
        break;
    case 0xd: t = 900; break;
    case 0xe: se = 0x85; t = 0x960; break;
    case 0xf: se = 0x86; t = 0x708; break;
    case 0x10: se = 0x87; t = 0x1c2; break;
    case 0x11: se = 0x88; t = 0x1c2; break;
    case 0x12: t = 0x708; break;
    default: t = 0; break;
    }
    Dungeon* dg = gameWorld().dungeon;
    if (se != -1 && dg != nullptr && (dg->floorFlags() & 1u) != 0 && s.owner != nullptr) se3d(se, s.owner->xf.m.pos);
}

namespace {
// orig FUN_00134ef0: HP = HP maximum - HP (0 -> 1), status HUD for the player
void invertHp(StatusObj& s) {
    StatusBlock& b = s.block;
    b.hp = statusHpMax(s) - b.hp;
    if (b.hp == 0) b.hp = 1;
    if (isPlayer(s.owner) && gHooks.hudHp) gHooks.hudHp(b.hp);   // FUN_00151ae0(0x5179c0, hp)
}
// orig FUN_00134f80: VT = VT maximum - VT (0 -> 1), uRam00517b2c for the player
void invertVt(StatusObj& s) {
    StatusBlock& b = s.block;
    b.vt = statusVtMaxTotal(s) - b.vt;
    if (b.vt == 0) b.vt = 1;
    if (isPlayer(s.owner) && gHooks.hudVt) gHooks.hudVt(b.vt);
}

// orig FUN_00138020 (instant status effects). Effects at the owner's middle (PRMCHAR +0xac * 0.5 above the feet,
// scale 100): bit 0 effect 0xd, bit 0x80 HP / VT inversion (0x100000 HP only, 0x200000 VT only; messages
// 0x27b/0x27c HP, 0x283/0x284 VT, effect 0x7b with yaw rand(0x167), SE 0x93), bit 0x100 HP -> 1 + SE 0x66.
// The effects are skipped while the item screen is open (iRam00517b68).
// UNVERIFIED: bit 0x1000 (teleport FUN_00124660 + effect 0x83 + SE 0x45) and bits 0x400 / 0x200 (flesh item drops
// FUN_0012ce70 / FUN_001d5410 + FUN_00207350 of the owner) are not translated (room search / item entity creation).
}  // namespace

void statusInstantEffects(StatusObj& s, uint32_t bits) {
    Chr* self = s.owner;
    if (self == nullptr) return;
    StatusBlock& b = s.block;
    const data::PrmChar* p = prmOf(self->id);
    const Vec3f mid{self->xf.m.pos[0], (p != nullptr ? p->iconHeight : 0.0f) * 0.5f + self->xf.m.pos[1] + 0.0f,
                    self->xf.m.pos[2]};
    const bool screen = gHooks.itemScreenOpen && gHooks.itemScreenOpen();
    if ((bits & 1u) != 0 && !screen) effect(0xd, mid);
    if ((bits & 0x80u) != 0) {
        const int vt0 = b.vt;
        const int hp0 = b.hp;
        if ((bits & 0x100000u) == 0) {
            if ((bits & 0x200000u) == 0) {
                invertHp(s);
                invertVt(s);
            } else {
                invertVt(s);
            }
        } else {
            invertHp(s);
        }
        const int dh = b.hp - hp0;
        if (dh != 0) {
            if (dh < 0) log(0x27c, -dh);
            else log(0x27b, dh);
        }
        const int dv = b.vt - vt0;
        if (dv != 0) {
            if (dv < 0) log(0x284, -dv);
            else log(0x283, dv);
        }
        if (!screen) {
            const Vec3f rot{0.0f, static_cast<float>(rng().rand(0x167)), 0.0f};
            effect(0x7b, mid, &rot);
        }
        se3d(0x93, self->xf.m.pos);
    }
    if ((bits & 0x100u) != 0) {
        statusAddHp(s, -(b.hp + -1));   // HP -> 1
        se3d(0x66, self->xf.m.pos);
    }
}

// orig FUN_00138660. Per bit: res = min(resist, 10); inflicted iff rand(9) < 10 - res; then the player flag
// FUN_00137220, duration FUN_00137aa0, visual FUN_00137d40, message FUN_00137540 (status_set.cpp); the instant effects
// FUN_00138020 follow.
uint32_t inflictStatus(StatusObj& s, uint32_t mask) {
    StatusBlock& b = s.block;
    if ((b.statusMask & 0x10000u) != 0) mask &= 0x3d000u;
    if (isPlayer(s.owner) && gHooks.playerImmunities) mask &= ~gHooks.playerImmunities();
    // 0x1000 teleport needs a destination (FUN_00138ad0); unset hook = assumed possible
    if ((mask & 0x1000u) != 0 && gHooks.canWarp && !gHooks.canWarp(s)) mask &= 0xffffefffu;
    if (mask == 0) return 0;
    uint32_t applied = 0;
    for (int i = 0; i < 0x13; ++i) {
        const uint32_t bit = 1u << i;
        if ((mask & bit) == 0) continue;
        int res = b.resist[static_cast<size_t>(i)];
        if (10 < res) res = 10;
        const int r = static_cast<int>(rng().rand(9));
        if (r < 10 - res) {
            if (isPlayer(s.owner)) statusSetPlayerFlag(i);   // FUN_00137220
            statusSetDuration(s, i);                         // FUN_00137aa0
            statusVisualOn(s, i);                            // FUN_00137d40
            statusSetMessage(i, isPlayer(s.owner));          // FUN_00137540
            if (b.timers[static_cast<size_t>(i)] != 0) b.statusMask |= bit;
            applied |= bit;
        }
    }
    statusInstantEffects(s, applied | (mask & 0x300000u));   // FUN_00138020
    return applied;
}

// orig FUN_00137d40 (on) / FUN_00137f40 (off): status 0x12 SE override (FUN_001161a0 / FUN_001161b0), status 5 the
// actor's rate scale 0 = 0.5 / 1.0 (FUN_0010ae50), statuses 2 / 4 (on, not for the player, no effect yet) effect 0x69 /
// 0x6b at pos + (0, PRMCHAR +0xac, 0), scale 100, handle at status +0x44[i]; off stops the handle of 2 / 3 / 4.
void statusVisual(StatusObj& s, int i, bool on) {
    Chr* self = s.owner;
    StatusBlock& b = s.block;
    if (on) {
        statusVisualOn(s, i);   // FUN_00137d40 (status_set.cpp)
        return;
    }
    if (i == 0x12) {
        if (gHooks.seOverride) gHooks.seOverride(false);   // FUN_001161b0
        return;
    }
    if (i == 5) {
        if (self != nullptr) self->model.anim.setScale(1.0f, 0);   // FUN_0010ae50(1.0, actor, 0)
        return;
    }
    if (i != 4 && i != 3 && i != 2) return;
    if (b.effects[size_t(i)] != -1) {
        if (gHooks.effectStop) gHooks.effectStop(b.effects[size_t(i)]);   // FUN_00112560
        b.effects[size_t(i)] = -1;
    }
}

// orig FUN_00138ac0 (messages FUN_001373b0/FUN_00137cf0/FUN_001378a0 not translated: presentation)
uint32_t cureAllStatus(StatusObj& s) {
    uint32_t cured = 0;
    for (int i = 0; i < 0x13; ++i) {
        const uint32_t bit = 1u << i;
        if ((s.block.statusMask & bit) != 0) {
            s.block.statusMask &= ~bit;
            cured |= bit;
            statusVisual(s, i, false);   // FUN_00137f40
        }
    }
    if (cured != 0 && s.block.hp != 0 && s.owner != nullptr) se3d(0x8b, s.owner->xf.m.pos);
    return cured;
}

// orig FUN_00138970 (messages FUN_001373b0 (player), FUN_00137cf0, FUN_001378a0 not translated: presentation)
uint32_t statusExpire(StatusObj& s, uint32_t mask) {
    uint32_t cleared = 0;
    for (int i = 0; i < 0x13; ++i) {
        const uint32_t bit = 1u << i;
        if ((mask & bit) != 0 && (s.block.statusMask & bit) != 0) {
            s.block.statusMask &= ~bit;
            cleared |= bit;
            statusVisual(s, i, false);   // FUN_00137f40
        }
    }
    if (cleared != 0 && s.block.hp != 0 && s.owner != nullptr) se3d(0x8b, s.owner->xf.m.pos);
    return cleared;
}

// orig FUN_00133bf0 (strings 0x44b030 ST21, 0x44b038 WG12, 0x44b040 ST00, 0x44b048 BX24, 0x44b050 CT11,
// 0x44b058 WG00, 0x44b060 ST01)
void statusTick(StatusObj& s) {
    StatusBlock& b = s.block;
    if (b.hp == 0) return;
    if (hooks::eventState() == 1) return;                                        // iRam0051b620 != 1
    if (gHooks.talkBusy && gHooks.talkBusy()) return;                           // iRam0053c6a8 != 1
    const bool pl = s.owner != nullptr && s.owner == player();
    if (pl && playerCtl() != nullptr && (playerCtl()->flags & 0x10000u) != 0) return;
    for (int i = 0; i < 0x13; ++i) {
        int32_t& t = b.timers[size_t(i)];
        if (t == 0 || !(0 < t)) continue;
        t = t - (b.brand == tagOf("ST21") ? 2 : 1);
        if (b.timers[size_t(i)] < 1) {
            statusExpire(s, 1u << i);
            continue;
        }
        if (i != 4 && i != 3) {
            if (i != 2) continue;
            if ((static_cast<uint32_t>(b.timers[size_t(i)]) & 0x3f) == 0) {
                statusAddHp(s, -3);                                              // poison
                if (pl) log(0x2df);
            }
        }
        // effect handle +0x44[i] != -1: FUN_001122d0 moves the status effect to pos + (0, PRMCHAR+0xac, 0)
        if (b.effects[size_t(i)] != -1 && s.owner != nullptr && gHooks.effectMove) {
            const data::PrmChar* p = prmOf(s.owner->id);
            const Vec3f& op = s.owner->xf.m.pos;
            gHooks.effectMove(b.effects[size_t(i)], Vec3f{op[0], (p != nullptr ? p->iconHeight : 0.0f) + op[1], op[2]});
        }
    }
    if (!pl) return;
    ++gRegenHp;
    ++gRegenVt;
    if (gHooks.equippedItem && gHooks.equippedItem(tagOf("WG12")) != nullptr) {
        ++gRegenWg12;
        if (0x2d < gRegenWg12) {
            gRegenWg12 = 0;
            statusAddVt(s, 1);
        }
    }
    if (b.vt == 0) {
        if (0x3c < gRegenHp) {
            gRegenHp = 0;
            statusAddHp(s, -1);
        }
        return;
    }
    float t = 60.0f;
    if (b.brand == tagOf("ST00")) t = 45.0f;
    if (gHooks.inventoryHas && gHooks.inventoryHas(tagOf("BX24"))) t = t * 2.0f;
    if (static_cast<int>(t * (1.0f - (static_cast<float>(b.hidden[4]) / 255.0f) * 0.5f)) < gRegenHp) {
        gRegenHp = 0;
        statusAddHp(s, 1);
    }
    float v = 90.0f;
    if (gHooks.equippedItem && gHooks.itemChance) {
        if (const void* it = gHooks.equippedItem(tagOf("CT11")))
            v = (100.0f / static_cast<float>(gHooks.itemChance(it))) * 90.0f;
        if (const void* it = gHooks.equippedItem(tagOf("WG00")))
            v = v * (100.0f / static_cast<float>(gHooks.itemChance(it)));
    }
    if (b.brand == tagOf("ST01")) {
        // PRMITEM ST01 +0x78 (installed by the game build; unset in the test tools = factor skipped)
        if (gHooks.brandSt01Value) v = v * (100.0f / static_cast<float>(gHooks.brandSt01Value()));
    }
    v = v * (static_cast<float>(b.hidden[4]) / 255.0f + 1.0f);
    if (flags().test(0x4f1)) {
        v = v * 1.25f;
    } else if (!flags().test(0x4f2) && !flags().test(0x4f3) && flags().test(0x4f4)) {
        v = v * 0.75f;
    }
    if (static_cast<int>(v) < gRegenVt) {
        gRegenVt = 0;
        statusAddVt(s, -1);
    }
}

// orig FUN_00138e40 (level table FUN_00138e00 = FUN_00138dc0 next threshold)
bool statusAddExp(StatusObj& s, int exp) {
    StatusBlock& b = s.block;
    if (99 <= b.level) return false;
    b.exp = b.exp + exp;
    if (b.exp < b.expNext) return false;
    static constexpr int8_t kHpGain[21] = {2, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 5, 5, 5, 6, 6, 6, 6, 7, 7, 8};
    do {
        const uint32_t k = rng().rand(0x14);
        b.hpMax = b.hpMax + kHpGain[k];
        if (999 < b.hpMax) b.hpMax = 999;
        statusAddHp(s, 0);
        b.atkLevel = b.atkLevel + 1;
        if (999 < b.atkLevel) b.atkLevel = 999;
        b.defLevel = b.defLevel + 1;
        if (999 < b.defLevel) b.defLevel = 999;
        b.level = b.level + 1;
        b.expNext = statusExpForLevel(b.level + 1);   // FUN_00138e00
        if (b.level == 99) {
            b.exp = b.expNext;
            break;
        }
    } while (b.expNext <= b.exp);
    if (b.brand == tagOf("ST18")) inflictStatus(s, 0x20000);
    if (0xf <= b.level) flags().set(0x3ce);
    if (1 < b.level) flags().set(0x33c);
    sound().playSe(1.0f, 0x8e);
    return true;
}

// orig FUN_00136580
bool applyDamage(StatusObj& vic, Chr* attacker, int dmg, uint32_t m3, void* evolveItem, Chr* owner) {
    StatusBlock& b = vic.block;
    Chr* self = vic.owner;
    if ((b.statusMask & 0x10000u) != 0) {   // invincible: "guard" effect 0x66 facing the attacker, SE 0x87
        Vec3f rot{0.0f, 0.0f, 0.0f};
        if (attacker != nullptr && self != nullptr) {
            Vec3f d{attacker->xf.m.pos[0] - self->xf.m.pos[0], 0.0f, attacker->xf.m.pos[2] - self->xf.m.pos[2]};
            normalize(d);
            rot = {0.0f, (std::atan2(d[0], d[2]) * 180.0f) / 3.1415927f, 0.0f};   // FUN_00206d20
        }
        if (self != nullptr) {
            const data::PrmChar* p = prmOf(self->id);
            const Vec3f at{self->xf.m.pos[0], (p != nullptr ? p->iconHeight : 0.0f) * 0.5f + self->xf.m.pos[1] + 0.0f,
                           self->xf.m.pos[2]};
            effect(0x66, at, &rot);
            se3d(0x87, self->xf.m.pos);
        }
        return false;
    }
    // m3 & 0x20 against the player: a random inventory item rots (inventory module; UNVERIFIED, not translated --
    // the original draws rand(count - 1) only when the inventory is non-empty)
    if ((m3 & 0x200u) != 0) statusAddVt(vic, dmg);
    if ((m3 & 0x400u) != 0) statusAddHp(vic, dmg);
    if ((m3 & 0x600u) != 0) return false;
    if ((m3 & 0x80u) != 0) {
        statusAddVt(vic, -dmg);
        if (attacker != nullptr && static_cast<unsigned>(attacker->type) < 3u) {
            statusAddVt(attacker->status, dmg);
            if (isPlayer(attacker)) log(0x2a6, dmg);   // + FUN_001e6d10 drain effect
        }
    }
    if ((m3 & 0x180u) == 0x80u) return false;
    const int taken = dmg <= b.hp ? dmg : b.hp;
    const bool alive = statusAddHp(vic, -dmg);
    if ((m3 & 0x100u) != 0 && attacker != nullptr && static_cast<unsigned>(attacker->type) < 3u) {
        statusAddHp(attacker->status, taken);
        if (isPlayer(attacker)) {
            log(0x2a5, taken);   // + FUN_001e6d10 drain effect (not translated)
            // FUN_001d4ee0 on the evolving 'SD23' sword (+0x54 + +0x60 < 99): message 0x252 when it grew
            if (evolveItem != nullptr && gHooks.evolveItem) gHooks.evolveItem(evolveItem, taken);
        }
    }
    if (alive) return false;
    float dropMul = 1.0f;
    if (attacker != nullptr) {
        const bool byPlayer = isPlayer(attacker) || ((attacker->type == 3 || attacker->type == 5) && owner != nullptr &&
                                                     isPlayer(owner));
        if (byPlayer) {
            // FUN_00136290: equipment EXP modifiers (category 10): flat add, multiplier, drop multiplier
            int add = 0;
            float mul = 1.0f;
            for (const EquipEffect& e : equipEffects()) {
                if (e.category != 10) continue;
                if (e.type == 0x18) {
                    if (gHooks.wearAtkItem) gHooks.wearAtkItem(e.item, static_cast<int16_t>(e.chance));   // FUN_001d4f70
                } else if (e.type == 0x1b) {
                    if (e.itemTag == tagOf("WG05")) add = static_cast<int16_t>(e.chance);   // 0x44b0a0: flat bonus
                    else mul = mul * (static_cast<float>(static_cast<int16_t>(e.chance)) / 100.0f);
                } else if (e.type == 0x1a) {
                    dropMul = static_cast<float>(static_cast<int16_t>(e.chance)) / 100.0f;
                }
            }
            const data::PrmChar* prm = vic.prm;
            StatusObj& ps = player()->status;
            int exp = static_cast<int>(static_cast<float>(prm != nullptr ? prm->f24 : 0) * mul) + add;
            if ((ps.block.statusMask & 0x8000u) != 0) exp = exp * 2;
            if (exp == 0) exp = 1;
            statusAddExp(ps, exp);
        }
    }
    b.lastExpMul = dropMul;
    return true;
}

// =============================================================================================================
// Damage formula
// =============================================================================================================
int damageFormula(float power, StatusObj& vic, Chr* attacker, uint32_t m1, uint32_t weaponTag, uint32_t& flagsOut,
                  int hitId, float& atkF) {
    if (isPlayer(vic.owner) && gHooks.heldElementMask) m1 &= ~gHooks.heldElementMask();
    atkF = 0.0f;
    if (power == 0.0f) return 0;
    flagsOut = 0;
    // thrown 'SD21' item: evade chance (item entities not translated)
    float mult = 1.0f;
    bool raw = false;
    if (attacker == nullptr || 2u < static_cast<unsigned>(attacker->type)) {
        if (weaponTag == 0) {
            atkF = power;
            raw = true;
        } else {
            const data::PrmChar* p = prmOf(weaponTag);
            atkF = static_cast<float>(p != nullptr ? p->statA : 0) * power;
        }
    } else {
        // FUN_00135010: player equipment guard (type 0x15, category 3) / per-hand infusion multiplier (0x1e)
        if (isPlayer(attacker)) {
            const uint32_t r = rng().rand(99);
            for (const EquipEffect& e : equipEffects()) {
                if (e.category == 3) {
                    if (e.type == 0x15) {
                        if (r < e.chance) {
                            flagsOut |= 2;
                            return 0;
                        }
                    } else if (e.type == 0x1e &&
                               ((e.rec == 0 && (m1 & 0x10000u) != 0) || (e.rec == 1 && (m1 & 0x20000u) != 0))) {
                        mult = static_cast<float>(e.value) / 100.0f;
                        break;
                    }
                }
            }
        }
        StatusObj& as = attacker->status;
        const int atk = statusAtk(as, hitId);
        if (!isPlayer(attacker) && atk == 0) return 0;
        atkF = static_cast<float>(atk) * power;
        if (isPlayer(attacker)) {
            uint32_t n = 0;
            const PlayerControl* ctl = playerCtl();
            if (ctl != nullptr && (ctl->counter & 0x20000000u) != 0) n = ctl->counter & 0xdfffffffu;
            const int ni = static_cast<int>(n);
            if (0x1e < ni) {
                if (ni < 0x3d) atkF = atkF * 1.1f;
                else if (ni < 0x5b) atkF = atkF * 1.2f;
                else if (ni < 0x79) atkF = atkF * 1.3f;
                else if (ni < 0x97) atkF = atkF * 1.4f;
                else atkF = atkF * 1.5f;
            }
        } else if (attacker->type == kChrEnemy) {
            Dungeon* dg = gameWorld().dungeon;
            const uint32_t floorNo = dg != nullptr ? static_cast<uint32_t>(floorNumber(dg->floorId())) : 0u;
            atkF = atkF + static_cast<float>(floorNo >> 1);
        }
        int crit = statusCrit(as);
        if ((as.block.statusMask & 0x10u) != 0 && as.block.brand == tagOf("ST19")) crit = crit + 0x32;
        const int atkPb = as.block.hidden[1];
        const int vicPb = vic.block.hidden[1];
        const int roll = static_cast<int>(rng().rand(0xff));
        if (roll < alchemy::ps2::ftoi(static_cast<float>(crit) * (static_cast<float>(atkPb) / 255.0f + 1.0f) *
                                      (1.0f - (static_cast<float>(vicPb) / 255.0f) * 0.5f)))
            flagsOut |= 1;
        Vec3f d{vic.owner->xf.m.pos[0] - attacker->xf.m.pos[0], vic.owner->xf.m.pos[1] - attacker->xf.m.pos[1],
                vic.owner->xf.m.pos[2] - attacker->xf.m.pos[2]};
        normalize(d);
        const Vec3f& vf = vic.owner->xf.m.fwd;
        const float f = d[2] * vf[2] + d[0] * vf[0] + d[1] * vf[1];
        const data::PrmChar* vp = vic.prm;
        int dir = 0x80;
        if (vp != nullptr) dir = f < -0.7f ? vp->v34[0] : (0.7f < f ? vp->v34[2] : vp->v34[1]);
        if (dir != 0x80) atkF = atkF * (static_cast<float>(0x80 - dir) / 128.0f + 1.0f);
        atkF = atkF * ((static_cast<float>(as.block.hidden[2]) / 255.0f) * 0.5f + 1.0f);
    }
    const int def = statusDef(vic);
    if (999.0f < atkF) atkF = 999.0f;
    float defF = 999.0f;
    if (!(999.0f < static_cast<float>(def))) defF = static_cast<float>(def);
    float base = 1.0f;
    if (!(atkF - defF < 1.0f)) base = atkF - defF;
    base = base * mult;
    if ((flagsOut & 1u) != 0) base = base * 1.5f;
    if ((m1 & 7u) != 0) {
        const float w = elementWeakness(vic, m1);
        base = base * w + base + 0.0f;
    }
    if (!raw && isPlayer(vic.owner)) {
        // FUN_001351b0: coat effects (scale 0x1e, evade 0x17, wear 0x19) and the ST17 brand counter
        float s = 1.0f;
        const uint32_t r = rng().rand(99);
        for (const EquipEffect& e : equipEffects()) {
            if (e.category != 5) continue;
            if (e.type == 0x1e) {
                if (r < static_cast<uint32_t>(static_cast<int16_t>(e.chance))) s = static_cast<float>(e.value) / 100.0f;
            } else if (e.type == 0x17) {
                if (r < static_cast<uint32_t>(static_cast<int16_t>(e.chance))) {
                    flagsOut |= 4;
                    return 0;
                }
            } else if (e.type == 0x19) {
                if (gHooks.wearItem) gHooks.wearItem(e.item, static_cast<int16_t>(e.chance));
            }
        }
        // ST17 brand counter (string 0x44b080): the same rand(99) against PRMITEM ST17 +0x78 (0x8f8)
        Chr* pl = player();
        if (pl != nullptr && pl->status.block.brand == tagOf("ST17") && gHooks.brandValue &&
            static_cast<int64_t>(r) < static_cast<int64_t>(gHooks.brandValue(17))) {
            if (gHooks.st17Counter) gHooks.st17Counter(*pl);   // FUN_001e5610(.., 'ST17') + FUN_0013de20
        }
        base = base * s;
    }
    const float pb4 = static_cast<float>(vic.block.hidden[4]);
    float v = base * (1.0f - (pb4 / 255.0f) * 0.25f) * difficultyScale(vic, attacker);
    if ((m1 & 0x80000u) != 0) v = v * 0.1f;
    int dmg = alchemy::ps2::ftoi(v);
    if (dmg < 1) dmg = 1;
    return dmg;
}

// =============================================================================================================
// Hit FX (FUN_0014bc30 with FUN_0014b9f0 / FUN_0014b840 / FUN_0014c580)
// =============================================================================================================
namespace {

// orig FUN_0014b9f0: random point on the victim facing the camera (two RNG draws)
Vec3f hitPoint(const Chr& c) {
    if (2u < static_cast<unsigned>(c.type)) return c.xf.m.pos;   // UNVERIFIED: output left unset in the original
    const data::PrmChar* p = prmOf(c.id);
    const Vec3f cam = gHooks.cameraPos ? gHooks.cameraPos() : c.xf.m.pos;
    Vec3f t{cam[0] - c.xf.m.pos[0], 0.0f, cam[2] - c.xf.m.pos[2]};
    normalize(t);
    const Vec3f& up = upVector();
    Vec3f side{up[1] * t[2] - up[2] * t[1], up[2] * t[0] - up[0] * t[2], up[0] * t[1] - up[1] * t[0]};
    normalize(side);
    const float spread = (p != nullptr ? p->throwSpread : 0.0f) * 0.25f;
    const float half = spread * 0.5f;
    const float k = spread * randUnit32768();
    Vec3f out{side[0] * k - half + c.xf.m.pos[0], 0.0f, side[2] * k - half + c.xf.m.pos[2]};
    const float ih = p != nullptr ? p->iconHeight : 0.0f;
    const float q = ih * 0.25f;
    const float mid = ih * 0.5f + c.xf.m.pos[1] + 0.0f;
    out[1] = (q * randUnit32768() + mid + 0.0f) - q * 0.5f;
    return out;
}

// orig FUN_0014b840: effect at `at` (or the victim's middle) with a random yaw unless a rotation is given
void spark(float scale, const Chr& c, int fx, const Vec3f* at, const Vec3f* rot) {
    if (2u < static_cast<unsigned>(c.type)) return;
    Vec3f pos;
    if (at == nullptr) {
        const data::PrmChar* p = prmOf(c.id);
        pos = {c.xf.m.pos[0], (p != nullptr ? p->iconHeight : 0.0f) * 0.5f + c.xf.m.pos[1] + 0.0f, c.xf.m.pos[2]};
    } else {
        pos = *at;
    }
    Vec3f r;
    if (rot == nullptr) r = {0.0f, 0.0f, randUnit32768() * 360.0f};
    else r = *rot;
    effect(fx, pos, &r, scale);
}

// orig FUN_0014c580: element sparks and SE
void elementFx(Chr& victim, Chr* attacker, uint32_t m1, const Vec3f* at) {
    if ((m1 & 0x40000u) == 0) {
        Vec3f p = at != nullptr ? *at : hitPoint(victim);
        const bool pl = attacker != nullptr && isPlayer(attacker);
        if ((m1 & 1u) != 0) {
            if (pl) spark(100.0f, victim, 2, &p, nullptr);
            else {
                const Vec3f zero{0.0f, 0.0f, 0.0f};
                spark(100.0f, victim, 0xf0, &p, &zero);
            }
        }
        if ((m1 & 2u) != 0) spark(100.0f, victim, pl ? 3 : 0xf1, &p, nullptr);
        if ((m1 & 4u) != 0) spark(100.0f, victim, pl ? 4 : 0xf2, &p, nullptr);
    } else {
        Vec3f from{victim.xf.m.pos[0], victim.xf.m.pos[1] + 10.0f, victim.xf.m.pos[2]};
        const Vec3f down{0.0f, -300.0f, 0.0f};
        float dist = 0.0f;
        if (raycast(from.data(), down.data(), 1.0f, &dist)) from[1] = from[1] - dist;   // FUN_00143150 / FUN_001431e0
        if ((m1 & 1u) != 0) effect(0x84, from);
        if ((m1 & 2u) != 0) {
            const Vec3f rot{0.0f, static_cast<float>(rng().rand(0x167)), 0.0f};
            effect(0x86, from, &rot);
        }
        if ((m1 & 4u) != 0) effect(0x85, from);
    }
    if ((m1 & 1u) != 0) se3d(0xab, victim.xf.m.pos);
    if ((m1 & 2u) != 0) se3d(0xac, victim.xf.m.pos);
    if ((m1 & 4u) != 0) se3d(0xad, victim.xf.m.pos);
}

}  // namespace

void hitElementFx(Chr& victim, Chr* attacker, uint32_t m1, const Vec3f* at) { elementFx(victim, attacker, m1, at); }

void hitFx(float value, Chr& victim, Chr* attacker, uint32_t m1, uint32_t m2, uint32_t m3, uint32_t dmgFlags) {
    (void)m2;
    const Vec3f p = hitPoint(victim);
    const bool pl = isPlayer(attacker);
    const int wc = pl ? weaponClass() : -1;
    const Vec3f& vp = victim.xf.m.pos;
    struct Row { uint32_t bit; float sPl; int fxPl; float sEn; int fxEn; int se; };
    if ((m3 & 2u) != 0) {
        if (pl) spark(95.0f, victim, 5, &p, nullptr);
        else spark(100.0f, victim, 0x16, &p, nullptr);
        se3d(0x9d, vp);
    }
    if ((m3 & 4u) != 0) {
        if (pl) spark(110.0f, victim, 5, &p, nullptr);
        else spark(100.0f, victim, 0x17, &p, nullptr);
        se3d(0x9e, vp);
    }
    if ((m3 & 8u) != 0) {
        int se;
        if (pl) {
            if (wc == 0) { spark(95.0f, victim, 7, &p, nullptr); se = 0x9f; }
            else if (wc == 1) { spark(95.0f, victim, 7, &p, nullptr); se = 0xa3; }
            else { spark(95.0f, victim, 8, &p, nullptr); se = 0xa1; }
        } else {
            spark(80.0f, victim, 0x18, &p, nullptr);
            se = 0x9f;
        }
        se3d(se, vp);
    }
    if ((m3 & 0x10u) != 0) {
        int se;
        if (pl) {
            if (wc == 0) { spark(110.0f, victim, 7, &p, nullptr); se = 0xa0; }
            else if (wc == 1) { spark(110.0f, victim, 7, &p, nullptr); se = 0xa4; }
            else { spark(110.0f, victim, 8, &p, nullptr); se = 0xa2; }
        } else {
            spark(90.0f, victim, 0x18, &p, nullptr);
            se = 0xa0;
        }
        se3d(se, vp);
    }
    static const Row kRows[] = {
        {0x8000, 95.0f, 0, 100.0f, 0x18, 0xa1},   {0x10000, 110.0f, 0, 90.0f, 0x19, 0xa2},
        {0x20000, 95.0f, 7, 100.0f, 0x19, 0xa3},  {0x40000, 110.0f, 7, 110.0f, 0x19, 0xa4},
    };
    for (const Row& r : kRows) {
        if ((m3 & r.bit) == 0) continue;
        if (pl) spark(r.sPl, victim, r.fxPl, &p, nullptr);
        else spark(r.sEn, victim, r.fxEn, &p, nullptr);
        se3d(r.se, vp);
    }
    static const Row kRows2[] = {
        {0x80000, 0, 0, 100.0f, 0x16, 0xc4},   {0x100000, 0, 0, 100.0f, 0x17, 0xc5},
        {0x200000, 0, 0, 100.0f, 0x18, 0xc6},  {0x400000, 0, 0, 100.0f, 0x19, 199},
        {0x800000, 0, 0, 100.0f, 0x16, 200},   {0x1000000, 0, 0, 100.0f, 0x17, 0xc9},
        {0x2000000, 0, 0, 100.0f, 0x18, 0xca}, {0x4000000, 0, 0, 100.0f, 0x16, 0xcb},
        {0x8000000, 0, 0, 100.0f, 0xea, 0x9d}, {0x10000000, 0, 0, 100.0f, 5, 0x9d},
    };
    for (const Row& r : kRows2) {
        if ((m3 & r.bit) == 0) continue;
        spark(r.sEn, victim, r.fxEn, &p, nullptr);
        se3d(r.se, vp);
    }
    if ((dmgFlags & 1u) != 0) {   // critical
        spark(100.0f, victim, pl ? 1 : 0x1a, &p, nullptr);
        se3d(0xa9, vp);
    }
    if ((m3 & 0x40u) != 0 && static_cast<unsigned>(victim.type) < 3u && attacker != nullptr && gHooks.steal &&
        gHooks.steal(attacker->status, victim.status)) {
        spark(100.0f, victim, 0xe3, &p, nullptr);
        se3d(0xc2, vp);
    }
    if ((m3 & 1u) != 0 && gHooks.damageNumber) {
        Chr* a = attacker;
        if (a != nullptr && 2u < static_cast<unsigned>(a->type)) a = nullptr;
        gHooks.damageNumber(a, alchemy::ps2::ftoi(value), p);
    }
    if ((m1 & 0x30000u) != 0) spark(100.0f, victim, 0xe, &p, nullptr);
    elementFx(victim, attacker, m1, &p);
}

// =============================================================================================================
// Player attack attributes FUN_0014b170 (strings 0x44bd50 "ST12", 0x44bd58 "SD23")
// =============================================================================================================
// m1: the player's parasite (status +0x5c) and its +0x38 parasite element bits (PRMITEM WM +0x70, 0x53d88c +0x44), the
// held sword's FUN_001d50f0; m3 0x800 from the ST12 brand under PRMITEM ST12 +0x78 (0x678). Then one rand(99) is
// consumed by the category-3 records of the equipment slots (slots 0 / 1 only the hand `hitId`, slots 2 / 3 always):
// 0x0e knockback 0x800, 0x14 0x1, 0x16 steal 0x40, 0x0a HP cost (SE 0x8d when negative), 0x0c drain 0x100 (an 'SD23'
// sword is returned as the evolving item), 0x0d VT drain 0x80, 0x1e infusion m1 0x10000 / 0x20000 by record (SE 0xb1),
// 0x1f status bits into m2 (+2 bit 0x800: one of them at random; bits 0x1000 | 0x2000 with rand(2) == 0: inflicted on
// the player instead). Each roll except 0x0a subtracts its chance. UNVERIFIED: FUN_0013de20 (ST12 flash) omitted.
void* playerAttackAttributes(uint32_t& m1, uint32_t& m2, uint32_t& m3, int hitId) {
    m1 = m2 = m3 = 0;
    Chr* p = player();
    void* evolve = nullptr;
    if (p == nullptr) return nullptr;
    if (gHooks.parasiteElementMask) m1 = m1 | gHooks.parasiteElementMask();
    if (p->status.block.brand == tagOf("ST12") && gHooks.brandValue) {
        const uint32_t r = rng().rand(99);
        if (r < static_cast<uint32_t>(gHooks.brandValue(12))) m3 = m3 | 0x800;   // + FUN_0013de20
    }
    uint32_t r = rng().rand(99);
    for (const EquipEffect& e : equipEffects()) {
        if (e.slot < 2 && static_cast<int>(e.slot) != hitId) continue;
        if (e.rec == 0 && gHooks.swordElementMask) m1 = m1 | gHooks.swordElementMask(e.item);   // item +0x24 == 0
        if (e.category != 3) continue;
        const int16_t amount = static_cast<int16_t>(e.chance);
        const uint32_t ch = static_cast<uint32_t>(static_cast<int>(amount));
        const bool hit = r < ch;
        switch (e.type) {
        case 0x0e: if (hit) m3 |= 0x800; r = r - ch; break;
        case 0x14: if (hit) m3 |= 1; r = r - ch; break;
        case 0x16: if (hit) m3 |= 0x40; r = r - ch; break;
        case 0x0a:
            statusAddHp(p->status, amount);   // FUN_001349e0(player status, +6)
            if (amount < 0) se3d(0x8d, p->xf.m.pos);
            break;
        case 0x0c:
            if (hit) {
                if (e.itemTag == tagOf("SD23")) evolve = e.item;
                m3 |= 0x100;
            }
            r = r - ch;
            break;
        case 0x0d: if (hit) m3 |= 0x80; r = r - ch; break;
        case 0x1e:
            if (hit) {
                m1 |= e.rec == 0 ? 0x10000u : 0x20000u;
                se3d(0xb1, p->xf.m.pos);
            }
            r = r - ch;
            break;
        case 0x1f: {
            if (hit) {
                const uint16_t v = static_cast<uint16_t>(e.value);
                static constexpr uint16_t kSrc[9] = {1, 2, 4, 8, 0x10, 0x20, 0x40, 0x100, 0x200};
                static constexpr uint32_t kDst[9] = {1, 2, 4, 8, 0x10, 0x20, 0x40, 0x80, 0x1000};
                uint32_t n = 0;
                for (int k = 0; k < 9; ++k) {
                    if ((v & kSrc[k]) != 0) {
                        ++n;
                        m2 |= kDst[k];
                    }
                }
                if ((v & 0x800u) != 0) {   // keep one of the set m2 bits at random
                    int pick = static_cast<int>(rng().rand(n - 1));
                    for (uint32_t bit = 1; bit != 0; bit <<= 1) {
                        if ((m2 & bit) == 0) continue;
                        if (pick == 0) {
                            m2 &= bit;
                            break;
                        }
                        --pick;
                    }
                }
                const int both = ((v & 0x1000u) != 0 ? 1 : 0) | ((v & 0x2000u) != 0 ? 2 : 0);
                if (both == 3 && rng().rand(2) == 0) {
                    inflictStatus(p->status, m2);   // FUN_00138660(player status, m2)
                    m2 = 0;
                }
            }
            r = r - ch;
            break;
        }
        default:
            break;
        }
    }
    return evolve;
}

// =============================================================================================================
// On-hit resolution FUN_0014c970
// =============================================================================================================
namespace {
// table 0x44bd70: 27 rows (E01..E27) of 4 flag ids (variant A..D) at 0x48ecd0.. (read from the ELF)
constexpr uint32_t kKillFlags[27][4] = {
    {0xc72, 0xc73, 0xc74, 0}, {0xc75, 0xc76, 0xc77, 0}, {0xc78, 0xc2a, 0xc79, 0xc7a}, {0xc7b, 0xc7c, 0xc7d, 0},
    {0xc7e, 0xc7f, 0xc80, 0}, {0xc81, 0xc82, 0xc83, 0}, {0xc84, 0xc85, 0xc86, 0}, {0xc87, 0xc88, 0xc89, 0},
    {0xc8a, 0xc8a, 0xc8b, 0xc8c}, {0xc8d, 0xc8e, 0xc8f, 0}, {0xc90, 0xc91, 0xc92, 0}, {0xc93, 0xc94, 0xc95, 0},
    {0xc96, 0xc97, 0xc98, 0}, {0xc99, 0xc9a, 0xc9b, 0}, {0xc9c, 0xc9d, 0xc9e, 0}, {0xc9f, 0xca0, 0xca1, 0},
    {0xca2, 0xca3, 0xca4, 0}, {0xca5, 0xc2b, 0xca6, 0xca7}, {0xca8, 0xca9, 0xcaa, 0}, {0xcab, 0xcac, 0xcad, 0},
    {0xcae, 0xcaf, 0xcb0, 0}, {0xcb1, 0xcb2, 0xcb3, 0}, {0xcb4, 0xcb5, 0xcb6, 0}, {0xcb7, 0xcb8, 0xcb9, 0},
    {0xcba, 0xcbb, 0xcbc, 0}, {0xcbd, 0xcbe, 0xcbf, 0}, {0xcc0, 0xcc1, 0xcc2, 0},
};

// orig FUN_0014c8f0: flag table[strtol(id bytes 1..2, 10) - 1][(id >> 24) - 'A'] (FUN_002388f0 = strtol base 10)
void killFlag(const Chr& victim) {
    const char num[3] = {static_cast<char>((victim.id >> 8) & 0xffu), static_cast<char>((victim.id >> 16) & 0xffu), 0};
    const long n = std::strtol(num, nullptr, 10);
    const int row = static_cast<int>(n) - 1;
    const int col = static_cast<int>((victim.id >> 24) & 0xffu) - 'A';
    // PORT: the original indexes without a check (ids outside E01..E27 / A..D read neighbouring data)
    if (row < 0 || row >= 27 || col < 0 || col >= 4) return;
    flags().set(kKillFlags[row][col]);   // FUN_00178890(0x53c3b0, ..)
}
}  // namespace

int onHit(float power, Chr& victim, Chr* attacker, uint32_t& m1, uint32_t& m2, uint32_t& m3, uint32_t weaponTag,
          int hitId, Chr* owner) {
    alchemy::ps2::Ps2FloatScope fp;
    StatusObj* vs = nullptr;
    StatusBlock* vb = nullptr;
    Chr* fighter = nullptr;
    if (static_cast<unsigned>(victim.type) < 3u) {
        vs = &victim.status;
        vb = &victim.status.block;
        fighter = &victim;
    }
    int dmg = -1;
    void* evolveItem = nullptr;
    if (attacker != nullptr && isPlayer(attacker)) {
        uint32_t a, b, c;
        evolveItem = playerAttackAttributes(a, b, c, hitId);   // FUN_0014b170 (uStack_74)
        m1 |= a;
        m2 |= b;
        m3 |= c;
    } else if (attacker != nullptr && attacker->type != kChrItem) {
        const uint32_t tag = attacker->id;
        Dungeon* dg = gameWorld().dungeon;
        const int floorNo = dg != nullptr ? floorNumber(dg->floorId()) : 0;
        if ((tag & 0xffu) == 'O') {   // traps O??x
            if (victim.type == kChrNpc || fighter == nullptr) return 0;
            const uint32_t k = tag >> 24;
            if (k == 'D' || k == 'F' || k == 'G' || k == 'H' || k == 'I' || k == 'L') {
                const float red = victimReduction(*vs, nullptr);
                const float weak = elementWeakness(*vs, m1);
                const float hpPart = static_cast<float>(statusHpMax(*vs)) * 0.1f;
                dmg = alchemy::ps2::ftoi(red * (weak + 1.0f) * (hpPart + static_cast<float>(floorNo)));
                if (dmg < 10) dmg = 10;
            } else if (k - 'J' < 2u) {
                const float red = victimReduction(*vs, nullptr);
                float f = static_cast<float>(statusHpMax(*vs)) * 0.01f * red;
                if (victim.type == kChrEnemy) f = f / 10.0f;
                dmg = alchemy::ps2::ftoi(f);
                if (dmg < 1) dmg = 1;
            } else if (k == 'E') {
                const float red = victimReduction(*vs, nullptr);
                dmg = alchemy::ps2::ftoi(static_cast<float>(vb->hp) * 0.2f * red);
                if (dmg < 1) dmg = 1;
            } else if (k == 'O') {
                const float red = victimReduction(*vs, nullptr);
                dmg = alchemy::ps2::ftoi(static_cast<float>(vb->hp) * 0.3f * red);
                if (dmg < 1) dmg = 1;
            } else if ((vb->statusMask & 0x10000u) != 0) {
                return 0;
            }
        } else if ((tag & 0xffu) == 'J' && tag != tagOf("J030")) {   // projectiles J???
            if (victim.type == kChrNpc || fighter == nullptr) return 0;
            const float red = victimReduction(*vs, attacker);
            const float weak = elementWeakness(*vs, m1);
            const data::PrmChar* wp = prmOf(weaponTag);
            const int def = statusDef(*vs);
            dmg = alchemy::ps2::ftoi(red * (weak + 1.0f) * power *
                                     ((static_cast<float>(wp != nullptr ? wp->statA : 0) +
                                       static_cast<float>(static_cast<uint32_t>(floorNo) >> 1)) - static_cast<float>(def)));
            if (dmg < 1) dmg = 1;
        } else if (attacker->type == kChrEffect) {
            if (victim.type == kChrNpc || fighter == nullptr) return 0;
            if (tag == tagOf("FBLW") || tag == tagOf("DTNT")) {   // 0x44bdf8 / 0x44be00
                // FUN_00136170(vs, owner, (int)power, m1): translated by the items module (ChrAdapter::projectileDamage)
                dmg = chrAdapter(&victim).projectileDamage(owner != nullptr ? &chrAdapter(owner) : nullptr,
                                                           alchemy::ps2::ftoi(power), m1);
                if (dmg < 1) dmg = 1;
            }
        } else if (tag == tagOf("C10A")) {
            if (!isPlayer(&victim)) return 0;
            // disasm 0x14d090: a1 = 5 is still live at the jal FUN_00178a80 (0x14d1bc) -> flag 5
            if (!flags().test(5) && 0.0f < power) {
                if (PlayerControl* pc = playerCtl()) pc->flags |= 0x100000u;   // uRam0050b620 |= 0x100000
                hitFx(0.0f, victim, attacker, m1, m2, m3, 0);
                statusAddHp(*vs, -statusHpMax(*vs));
                requestDeath(*fighter, 0);
                return 1;
            }
        }
    }
    if (attacker != nullptr && attacker->type == kChrItem) {
        // thrown item (disasm 0x14ca78..0x14cd00)
        int cls = 0;
        if (weaponTag == 0) {
            cls = throwHooks().itemClass && attacker->item != nullptr ? throwHooks().itemClass(attacker->item) : 0;   // +0x24
        } else {
            if ((weaponTag & 0xffu) == 'O' && (weaponTag >> 24) == 'A') return isPlayer(&victim) ? 1 : 0;
            if ((weaponTag & 0xffu) == 'C') {
                if (isPlayer(&victim)) return 1;
                if (victim.type == kChrNpc) return 1;
                if (victim.type == kChrEnemy && (owner == nullptr || !isPlayer(owner)) && weaponTag != tagOf("C09A"))
                    return 0;
                if (fighter == nullptr) return 0;
                const float red = victimReduction(*vs, attacker);
                const float weak1 = elementWeakness(*vs, m1) + 1.0f;
                float st07 = 0.0f, lvl = 0.0f, h3 = 1.0f, h2 = 1.0f;
                if (owner != nullptr && isPlayer(owner)) {
                    const StatusBlock& ob = owner->status.block;
                    if (ob.brand == tagOf("ST07") && gHooks.brandValue) st07 = static_cast<float>(gHooks.brandValue(7));   // +0x3f8
                    lvl = static_cast<float>(ob.level);                                                   // +0x48
                    h3 = static_cast<float>(ob.hidden[3]) / 255.0f + 1.0f;
                    h2 = (static_cast<float>(ob.hidden[2]) / 255.0f) * 0.5f + 1.0f;
                }
                if (0.0f < power) {
                    const int def = statusDef(*vs);
                    const float x = ((power + st07) + 0.0f + 2.0f * lvl) - static_cast<float>(def);
                    dmg = alchemy::ps2::ftoi(red * (h2 * (h3 * (weak1 * x))));
                    if (dmg < 1) dmg = 1;
                } else {
                    dmg = 0;
                }
            }
            cls = throwHooks().itemClass && attacker->item != nullptr ? throwHooks().itemClass(attacker->item) : 0;
        }
        if (cls == 9) return 1;
    }
    const unsigned vt = static_cast<unsigned>(victim.type);
    if (2u < vt) {   // items / objects: vt+0x24 (FUN_00129140 or the object class's), FX on 1
        const int r = chrContact(victim, attacker, m1, m2, m3);
        if (r == 1) {
            hitFx(0.0f, victim, attacker, 0, 0, m3, 0);   // FUN_0014bc30(0, victim, attacker, 0, 0, m3, 0)
            return 1;
        }
        return r;
    }
    if (victim.type == kChrNpc) {
        if ((m3 & 0x780u) == 0) hitFx(0.0f, victim, attacker, 0, 0, 0x8000000, 0);
        const uint32_t id = victim.id;
        if (tag3(id) != (tagOf("C19\0") & 0xffffffu) && id != tagOf("C08B") && id != tagOf("C08C") && attacker != nullptr)
            react(victim, 0x1000, attacker->xf.m.pos, false);
        // attacker == player: uRam0053c5f4 = 0 (talk idle counter, not modelled)
        return chrContact(victim, attacker, m1, m2, m3);   // vt+0x24 (FUN_00129140: 1 for NPCs)
    }
    if (vb->hp < 1) return 0;
    if (aiOnHit(victim.ai) || (victim.act.flags() & 0x1000000u) != 0) return 1;   // reaction handled / i-frames
    if (attacker != nullptr && !isPlayer(attacker) && !isPlayer(owner) && attacker->type != kChrItem) {
        const int at = attacker->type;
        if ((m3 & 0x4000u) != 0 && at == kChrEnemy && victim.type == kChrEnemy) return 0;
        if (victim.type == kChrEnemy) {
            if (at == kChrEnemy || (at == kChrObject && owner != nullptr && owner->type == kChrEnemy)) m1 |= 0x80000u;
        }
    }
    if ((vb->statusMask & 0x10000u) != 0) {
        applyDamage(*vs, attacker, 0, 0, nullptr, nullptr);
        return 1;
    }
    uint32_t dflags = 0;
    float atkF = power;
    if (dmg < 0) dmg = damageFormula(power, *vs, attacker, m1, weaponTag, dflags, hitId, atkF);
    if ((dflags & 2u) != 0) {
        log(0x291);
        return 0;
    }
    if ((dflags & 4u) != 0) {
        log(0x2b2);
        return 0;
    }
    if (isPlayer(attacker)) {
        // FUN_00179810(counters, 0xb) / counter 13 crit streak (statistics): UNVERIFIED, not translated
        if ((dflags & 1u) != 0 && static_cast<unsigned>(weaponClass()) < 3u) counters().add(0xd, 1);
    }
    if (isPlayer(&victim) && attacker != nullptr && attacker->type == kChrEnemy) counters().add(0xe, dmg);
    m2 = inflictStatus(*vs, m2);
    hitFx(atkF, victim, attacker, m1, m2, m3, dflags);
    if (attacker != nullptr && attacker->type == kChrItem && victim.type == kChrEnemy && dmg != 0)
        log(0x246, dmg < 0 ? -dmg : dmg);
    if (attacker != nullptr && attacker->id == tagOf("J030")) {   // instant vanish
        const data::PrmChar* vp = prmOf(victim.id);
        const Vec3f at{victim.xf.m.pos[0], (vp != nullptr ? vp->iconHeight : 0.0f) * 0.5f + victim.xf.m.pos[1] + 0.0f,
                       victim.xf.m.pos[2]};
        const Vec3f rot{0.0f, 0.0f, randUnit32768() * 360.0f};
        effect(6, at, &rot);                          // FUN_00112190(30.0, 6, .., (0, 0, yaw), 100)
        groundEffect(victim, 0xdc);                   // FUN_0012ad50(victim, 0xdc)
        counters().add(4, 1);
        counters().add(0x16, 1);
        statusAddHp(*vs, -statusHpMax(*vs));
        requestDeath(victim, 1);
        return 1;
    }
    if (!applyDamage(*vs, attacker, dmg, m3, evolveItem, owner)) {
        react(victim, m3, attacker != nullptr ? attacker->xf.m.pos : victim.xf.m.pos, false);
        return 1;
    }
    requestDeath(victim, 0);
    if (isPlayer(attacker) || (attacker != nullptr && attacker->type == kChrItem && isPlayer(owner))) {
        if (isPlayer(attacker)) {
            counters().add(0x16, 1);   // a2 = 1 (delay slot 0x14d9ac)
            if (static_cast<unsigned>(weaponClass()) < 3u) counters().add(8, 1);
        }
        if (gHooks.bx24Tick) gHooks.bx24Tick();   // FUN_001d9560(0x53d910)
        counters().add(4, 1);
        killFlag(victim);                          // FUN_0014c8f0
    }
    if (attacker != nullptr && attacker->type == kChrItem && owner != nullptr && isPlayer(owner) && victim.type == kChrEnemy)
        counters().add(7, 1);
    if (flags().test(5) && isPlayer(&victim) && attacker != nullptr && attacker->type == kChrEnemy &&
        flags().test(0x33c) && gameWorld().dungeon != nullptr && gameWorld().dungeon->floorId() == 0x5b)
        flags().setGuarded(0x481);
    return 1;
}

// =============================================================================================================
// Reactions and death requests
// =============================================================================================================
namespace {
bool canInterrupt(const StateMachine& sm, int maxPrio) {
    return (sm.curFactory == StateId::None && sm.next == StateId::None) || sm.priority <= maxPrio;
}
void requestState(Chr& c, StateId s, int prio) {
    StateMachine& sm = c.sm;
    sm.next = s;
    sm.popRequested = false;
    sm.arg = 0;
    sm.priority = prio;
    sm.priorityCopy = prio;
}
}  // namespace

void react(Chr& c, uint32_t m3, const Vec3f& src, bool invert) {
    StatusBlock& b = c.status.block;
    if ((b.statusMask & 0x10000u) != 0) return;
    if (isPlayer(&c)) {
        if (b.brand == tagOf("ST02") && (m3 & 0x2800u) != 0) m3 &= 0xffffd7ffu;   // + FUN_0013de20 flash
    } else {
        const uint32_t t = tag3(c.id);
        if (t == (tagOf("E19\0") & 0xffffffu)) {
            if (((c.ai.flags & 6u) >> 1) == 0) m3 &= 0xffffd7ffu;
        } else if (t == (tagOf("E12\0") & 0xffffffu)) {
            m3 &= 0xffffd7ffu;
        }
    }
    Vec3f& kv = c.phys.knock;   // physics +0x88..+0x90
    if ((m3 & 0x800u) != 0) {
        Vec3f d{c.xf.m.pos[0] - src[0], 0.0f, c.xf.m.pos[2] - src[2]};
        normalize(d);
        if (invert) d = {-d[0], -d[1], -d[2]};
        kv = {d[0] * 30.0f, d[1] * 30.0f, d[2] * 30.0f};
        if (canInterrupt(c.sm, 0)) requestState(c, StateId::Knockback, 0);
    } else if ((m3 & 0x2000u) != 0) {
        Vec3f d{src[0] - c.xf.m.pos[0], 0.0f, src[2] - c.xf.m.pos[2]};
        normalize(d);
        kv = d;
        if (canInterrupt(c.sm, 0)) requestState(c, StateId::Pull, 0);
    } else {
        if (canInterrupt(c.sm, 0)) requestState(c, StateId::HitStagger, 0);
        kv = {(m3 & 0x1000u) != 0 ? -1.0f : 1.0f, 0.0f, 0.0f};
    }
}

void requestDeath(Chr& c, int variant) {
    StateMachine& sm = c.sm;
    if (sm.next == StateId::None && (sm.curFactory == StateId::DeathA || sm.curFactory == StateId::DeathB)) return;
    if (!((sm.curFactory == StateId::None && sm.next == StateId::None) || sm.priority <= 1)) return;
    requestState(c, variant == 1 ? StateId::DeathB : StateId::DeathA, 1);
}

}  // namespace baroque::game
