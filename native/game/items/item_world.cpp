#include "game/items/item_world.hpp"
#include "game/collision.hpp"
#include "game/hud/hud_api.hpp"
#include "game/items/item_effects.hpp"
#include <cmath>

namespace baroque::game {

// 0x499270 [V floats]
const float kItemRestOffset[kItemClassCountAll] = {4.0f, 3.7f, 9.89f, 6.44f, 4.14f, 11.0f, 4.0f, 4.5f, 2.5f, 1.7f, 4.935f, 5.72f, 3.7f, 4.5f, 4.5f};

namespace {
constexpr float kPi = 3.1415927f;
constexpr uint32_t kIdBN02 = itemId("BN02");
constexpr uint32_t kIdBN22 = itemId("BN22");
constexpr uint32_t kIdHT02 = itemId("HT02");
constexpr uint32_t kIdMT03 = itemId("MT03");
constexpr uint32_t kIdST07 = itemId("ST07");
constexpr uint32_t kIdST16 = itemId("ST16");

float length(const Vec3& v) { return std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z); }
// orig FUN_00206ab0: normalise in place, returns the previous length
float normalize(Vec3& v) {
    const float l = length(v);
    if (l != 0.0f) { v.x /= l; v.y /= l; v.z /= l; }
    return l;
}
Vec3 sub(const Vec3& a, const Vec3& b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
float dot(const Vec3& a, const Vec3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }

// ---- transform helpers of the actor base 0x4ccc70 on the item's matrix rows (xform +0x18 right, +0x24 up, +0x30
// forward). Rotation matrix FUN_0039ddd0 -> FUN_0039ded0 (axis-angle, row-vector) and FUN_003a0330 (v * M).
struct Rot { float m[12] = {}; };
Rot axisAngle(float rad, const Vec3& a) {   // orig FUN_0039ded0
    Rot r;
    const float s = std::sin(rad), c = std::cos(rad), omc = 1.0f - c;   // UNVERIFIED: PS2 libm vs host (ulp)
    float k = omc * a.x;
    r.m[0] = k * a.x + c + 0.0f;
    r.m[1] = k * a.y + s * a.z + 0.0f;
    r.m[2] = k * a.z - s * a.y;
    k = omc * a.y;
    r.m[4] = k * a.x - s * a.z;
    r.m[5] = k * a.y + c + 0.0f;
    r.m[6] = k * a.z + s * a.x + 0.0f;
    k = omc * a.z;
    r.m[8] = k * a.x + s * a.y + 0.0f;
    r.m[9] = k * a.y - s * a.x;
    r.m[10] = k * a.z + c + 0.0f;
    return r;
}
Vec3 mulRot(const Vec3& v, const Rot& r) {   // orig FUN_003a0330
    return {v.z * r.m[8] + v.x * r.m[0] + v.y * r.m[4], v.z * r.m[9] + v.x * r.m[1] + v.y * r.m[5],
            v.z * r.m[10] + v.x * r.m[2] + v.y * r.m[6]};
}
Vec3 cross(const Vec3& a, const Vec3& b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
// orig FUN_001286c0: axes = (1,0,0), (0,1,0), (0,0,1) (FUN_00206f90 / FUN_00206f10 / FUN_00207010)
void resetAxes(Item& it) { it.right = {1, 0, 0}; it.up = {0, 1, 0}; it.fwd = {0, 0, 1}; }
// orig FUN_00128ae0: right rotated about up, fwd = right x up, right = up x fwd
void rotateAboutUp(Item& it, float rad) {
    it.right = mulRot(it.right, axisAngle(rad, it.up));
    normalize(it.right);
    it.fwd = cross(it.right, it.up);
    normalize(it.fwd);
    it.right = cross(it.up, it.fwd);
    normalize(it.right);
}
// orig FUN_00128c00: fwd rotated about right, up = fwd x right, fwd = right x up
void rotateAboutRight(Item& it, float rad) {
    it.fwd = mulRot(it.fwd, axisAngle(rad, it.right));
    normalize(it.fwd);
    it.up = cross(it.fwd, it.right);
    normalize(it.up);
    it.fwd = cross(it.right, it.up);
    normalize(it.fwd);
}
// orig FUN_001289c0: up rotated about fwd, right = up x fwd, up = fwd x right
void rotateAboutForward(Item& it, float rad) {
    it.up = mulRot(it.up, axisAngle(rad, it.fwd));
    normalize(it.up);
    it.right = cross(it.up, it.fwd);
    normalize(it.right);
    it.up = cross(it.fwd, it.right);
    normalize(it.up);
}
// orig FUN_001288b0: up = n, right = up x fwd, fwd = right x up
void alignUp(Item& it, const Vec3& n) {
    it.up = n;
    it.right = cross(it.up, it.fwd);
    normalize(it.right);
    it.fwd = cross(it.right, it.up);
    normalize(it.fwd);
}
// FUN_002071e0(0x53eb20, 0x167) degrees -> radians
float randomYaw(ItemSystem& sys) { return static_cast<float>(static_cast<int>(sys.rand(0x167))) * kPi / 180.0f; }
// FUN_001286c0 + FUN_00128ae0(random yaw)
void randomAxes(Item& it, ItemSystem& sys) {
    resetAxes(it);
    rotateAboutUp(it, randomYaw(sys));
}
constexpr float kTumble = 0.5235988f;   // 0x3f060a92 (30 degrees)
// orig FUN_00109d40(item +0x2c, pos, scale, fwd, up, right): the model node matrix
void publishModel(Item& it, const Vec3& pos) {
    it.modelXf.pos = pos;
    it.modelXf.right = it.right;
    it.modelXf.up = it.up;
    it.modelXf.fwd = it.fwd;
    it.modelXf.scale = it.scale;
    it.modelXf.valid = true;
    it.modelXf.publishes += 1;
}
void publishModel(Item& it) { publishModel(it, it.pos); }
// orig FUN_001cec60 (work/decompiled/001cec60.c): published raised along up (down when reversed) by 0x499270[cls]
void publishRaised(Item& it) {
    Vec3 u = it.up;
    if (it.flags & kItemReversed) u = {-u.x, -u.y, -u.z};
    const float k = kItemRestOffset[it.cls < kItemClassCountAll ? it.cls : 0];
    publishModel(it, {it.pos.x + u.x * k, it.pos.y + u.y * k, it.pos.z + u.z * k});
}
// orig FUN_001d50f0 (work/decompiled/001d50f0.c): PRM +0x70 of the item, ORed with the PRMITEM11 (WM) +0x70 of its
// parasite and of the parasite's parasite
uint32_t elementMask(ItemSystem& sys, const Item& it) {
    uint32_t m = static_cast<uint32_t>(sys.word(it, 0x70));
    if (const Item* p = it.parasite) {
        m |= static_cast<uint32_t>(sys.word(kClsWM, p->idx, 0x70));
        if (const Item* pp = p->parasite) m |= static_cast<uint32_t>(sys.word(kClsWM, pp->idx, 0x70));
    }
    return m;
}
void clearAirborne(Item& it) { it.physFlags &= ~3u; it.vy = 0; }        // FUN_00142140
void jump(Item& it, float v) { it.physFlags |= 1; it.vy = -v; }           // FUN_00142160
bool aliveCombatant(const CharacterAccess& c) { return !((c.actorType() == 0 || c.actorType() == 2) && !c.isAlive()); }
}  // namespace

uint32_t itemElementMask(ItemSystem& sys, const Item& item) { return elementMask(sys, item); }

void itemRequestState(Item& item, ItemState state) {
    ItemStateSlot& s = item.state;
    if ((s.factory == 0 && s.pending == 0) || s.param0 < 1) {
        s.pending = static_cast<int>(state);
        s.param0 = 0;
        s.param1 = 0;
    }
}

// ---- state enter / update ------------------------------------------------------------------------

namespace {
// orig FUN_001cfd90 (work/decompiled/001cfd90.c): FUN_00143150(pos, (0, -1000, 0), 1.0 at 0x463d38)
void enterLying(Item& it, CollisionQuery& cq) {
    it.moveVel = {};
    it.gravityScale = 1.0f;
    it.state.enterY = it.pos.y;
    if (it.cls == kClsIS || it.cls == kClsFlesh) {
        float d;
        if (cq.raycastDown(it.pos, 1000.0f, d)) it.pos.y = (it.pos.y - d) + 120.0f;
    }
}
// orig FUN_001cfea0 (work/decompiled/001cfea0.c)
void updateLying(Item& it, ItemSystem& sys, CollisionQuery& cq) {
    if (it.pos.y < -10000.0f) { sys.game->removeFromWorld(&it); return; }
    if (it.cls == kClsIS || it.cls == kClsFlesh) { settleItem(it, cq); return; }
    const uint32_t m = cq.moveAndCollide(it);
    if (m & 0x30) {
        if (!(it.flags & kItemPlacedB)) {
            if (it.state.enterY != 0.0f) sys.game->se3d(0xb6, it.pos);
        }
        it.flags &= ~kItemPlacedB;
        settleItem(it, cq);
    }
    publishModel(it);   // LAB_001cff94
}
// orig FUN_001cffc0 (work/decompiled/001cffc0.c)
void enterThrown(Item& it, ItemSystem& sys) {
    ItemStateSlot& s = it.state;
    s.thv = static_cast<float>(sys.word(it, 0x6c));
    s.dir = it.velocity;
    const int cls = it.cls < kItemClassCountAll ? it.cls : 0;
    if (it.thrower && it.thrower->isPlayer()) {
        it.gravityScale = kItemClassPhysics[cls].gravityScale;
        s.speed = throwSpeed(sys, true);          // +0x1c keeps the launch vector as given (the aim direction)
        s.decel = kItemClassPhysics[cls].decel;
    } else if (it.thrower && (it.thrower->actorType() == 1 || it.thrower->actorType() == 2)) {
        it.gravityScale = kItemClassPhysics[cls].gravityScale;
        s.speed = normalize(s.dir);
        s.decel = kItemClassPhysics[cls].decel;
    } else {
        it.gravityScale = 1.0f;
        s.speed = normalize(s.dir);
        s.decel = 0.0f;
    }
    jump(it, s.dir.y * s.speed);
    it.moveVel = {s.dir.x * s.speed, 0.0f, s.dir.z * s.speed};
    randomAxes(it, sys);
}
// orig FUN_001d0370 (work/decompiled/001d0370.c)
void updateThrown(Item& it, ItemSystem& sys, CollisionQuery& cq) {
    GameAccess& g = *sys.game;
    ItemStateSlot& s = it.state;
    if (it.pos.y < -10000.0f) { g.removeFromWorld(&it); return; }
    if (it.thrower) {
        bool keep = false;
        const int n = g.characterCount();
        for (int i = 0; i < n; ++i) {
            CharacterAccess* c = g.character(i);
            if (c == it.thrower && !c->isRemoved()) { keep = !((c->actorType() == 0 || c->actorType() == 2) && !c->isAlive()); break; }
        }
        if (!keep) it.thrower = nullptr;
    }
    // FUN_001d0d30: element mask FUN_001d50f0, status / reaction masks, "hit" result
    uint32_t statusMask, reactMask;
    const uint32_t element = elementMask(sys, it);
    int hit = throwHitMask(sys, it, statusMask, reactMask);
    reactMask |= 0x10000000;
    const int cls = it.cls < kItemClassCountAll ? it.cls : 0;
    CharacterAccess* hits[0x40];
    // FUN_0014ad00(radius, +0x28, 0x50c780, item, 0, pos, element, status, react, thrower id, thrower) + FUN_0014df50
    const int nh = cq.hitTest(it, kItemClassPhysics[cls].radius, s.thv, element, statusMask, reactMask, it.throwerId,
                              it.thrower, hits, 0x40);
    if (nh >= 0) {   // FUN_0014ad00 returned 1
        for (int i = 0; i < nh; ++i) {
            CharacterAccess* c = hits[i];
            if (c->actorType() == 2) {
                g.talkEventEnemyHit();
                if (it.cls == kClsTP) { s.speed = 0; hit = 1; clearAirborne(it); }
                else throwEffect(sys, it, *c, it.thrower);
            } else if (c->actorType() == 1) {
                if (npcAcceptsItem(*c, sys) && giveItemToNpc(it, *c, sys)) return;
                s.speed = 0; hit = 1; clearAirborne(it);
            } else if (c->isPlayer()) {
                const uint32_t b0 = it.throwerId & 0xff;
                if (b0 == 'C' || (b0 == 'O' && (it.throwerId >> 24) == 'A')) {
                    hit = 1;
                    if (sys.inventory->add(&it, sys)) { obtainedItem(it, sys); return; }
                    s.speed = 0; clearAirborne(it);
                }
            }
        }
        if (hit == 0) { g.removeFromWorld(&it); return; }
    }
    // FUN_001289c0 / FUN_00128ae0 / FUN_00128c00 (0x3f060a92 = 30 degrees): tumble
    rotateAboutForward(it, kTumble);
    rotateAboutUp(it, kTumble);
    rotateAboutRight(it, kTumble);
    if (s.speed > 0.0f && it.id == kIdBN02) {
        Vec3 d = it.moveVel;
        normalize(d);
        CharacterAccess* c = g.nearest(500.0f, 45.0f, nullptr, 2, &d, &it.pos);   // FUN_0012c520(500, 45, list, item, 2, &dir)
        if (c) {
            clearAirborne(it);
            it.gravityScale = 0.0f;
            Vec3 to = sub(c->position(), it.pos);
            to.y += c->height() * 0.5f;
            normalize(to);
            Vec3 b{(to.x + d.x) * 0.5f, (to.y + d.y) * 0.5f, (to.z + d.z) * 0.5f};   // DAT_00463d40 = 0.5
            it.moveVel = {b.x * s.speed, b.y * s.speed, b.z * s.speed};
        }
    }
    if (it.gravityScale > 0.0f) {
        it.moveVel = {s.dir.x * s.speed, 0.0f, s.dir.z * s.speed};
        s.speed -= s.decel;
        if (s.speed < 0.0f) s.speed = 0.0f;
    }
    const uint32_t m = cq.moveAndCollide(it);
    if (it.cls == kClsBN && (m & 0x31)) {
        g.effect(0xe8, it.pos);
        g.se3d(0xd5, it.pos);
        if (it.id == kIdBN22) printCubits(sys);
        g.removeFromWorld(&it);
        return;
    }
    if (m & kContactWall) {
        s.speed = 0;
        it.moveVel = {};
        clearAirborne(it);
        jump(it, 0.0f);
        if (it.id == kIdBN22) { printCubits(sys); g.removeFromWorld(&it); return; }
        if (it.onFloor && g.floorIsWaterish(it.floorAttr)) {
            // FUN_001d4860(item, id): a1 still holds the compared id word (0x1d0b88..0x1d0bac / 0x1d0be8..0x1d0c0c [V]):
            // a heart becomes "HT02" (0x463d60), a meat "MT03" (0x463d68)
            if (it.cls == kClsHT && it.id != kIdHT02) changeItemId(it, kIdHT02, sys);
            else if (it.cls == kClsMT && it.id != kIdMT03) changeItemId(it, kIdMT03, sys);
        }
    }
    if (m & 0x30) {
        bounceItem(it, sys);
        randomAxes(it, sys);
        if (it.cls == kClsTP) setDiskFace(it, sys, cq);
        it.flags &= ~(kItemInFlight | kItemThrownBack);
        settleItem(it, cq);
    }
    publishModel(it);
}
// orig FUN_001d1450 / FUN_001d1480 (work/decompiled/001d1450.c, 001d1480.c)
void enterGeneric(Item& it) { it.gravityScale = 1.0f; }
void updateGeneric(Item& it, ItemSystem& sys, CollisionQuery& cq) {
    if (it.pos.y < -10000.0f) { sys.game->removeFromWorld(&it); return; }
    const uint32_t m = cq.moveAndCollide(it);
    if (m & kContactWall) {
        if (it.onFloor) {   // GroundInfo +0 != 0: pushed out along the first contact face's normal by the radius
            const float push = kItemClassPhysics[it.cls < kItemClassCountAll ? it.cls : 0].radius;   // physics +0x14
            it.pos.x += it.wallNormal.x * push; it.pos.y += it.wallNormal.y * push; it.pos.z += it.wallNormal.z * push;
        }
        it.moveVel = {};
        clearAirborne(it);
    }
    if (m & 0x30) {
        sys.game->se3d(0x4d, it.pos);
        it.flags &= ~(kItemInFlight | kItemThrownBack);
        settleItem(it, cq);
    }
    publishModel(it);
}
// orig FUN_001d15f0 (work/decompiled/001d15f0.c): TL "drop" state 0x4ceef0 enter: hop of 10..20 in a random direction
void enterDropped(Item& it, ItemSystem& sys) {
    const float v = static_cast<float>(static_cast<int>(sys.rand(10)) + 10);
    const float a = randomYaw(sys);
    Vec3f d{};
    rotateY(a, d, Vec3f{0.0f, 0.0f, 1.0f});   // FUN_00206ca0(a, out, FUN_00207010)
    it.gravityScale = 1.0f;
    jump(it, v);
    it.moveVel = {d[0] * v, d[1] * v, d[2] * v};
    randomAxes(it, sys);
}
// orig FUN_001d17c0 (work/decompiled/001d17c0.c)
void updateDropped(Item& it, ItemSystem& sys, CollisionQuery& cq) {
    if (it.pos.y < -10000.0f) { sys.game->removeFromWorld(&it); return; }
    rotateAboutForward(it, kTumble);
    rotateAboutUp(it, kTumble);
    rotateAboutRight(it, kTumble);
    const uint32_t m = cq.moveAndCollide(it);
    if (m & kContactWall) {
        it.moveVel = {};
        clearAirborne(it);
    }
    if (m & 0x30) {
        bounceItem(it, sys);
        randomAxes(it, sys);
        if (it.cls == kClsTP) setDiskFace(it, sys, cq);
        settleItem(it, cq);
    }
    publishModel(it);
}
// orig FUN_001ceae0 (work/decompiled/001ceae0.c): settled state enter
void enterSettled(Item& it, CollisionQuery& cq) {
    ItemStateSlot& s = it.state;
    s.lflags = 0;
    s.standing = nullptr;
    s.ringEffect = -1;
    s.crystalRing = -1;
    s.fleshTimer = 0;
    it.moveVel = {};
    if (it.cls == kClsIS || it.cls == kClsFlesh) {
        // FUN_00143150((x, y + 5, z), (0, -1000, 0), 1.0 at 0x463d28)
        const Vec3 from{it.pos.x, it.pos.y + 5.0f, it.pos.z};
        float d;
        if (cq.raycastDown(from, 1000.0f, d)) s.restY = (from.y - d) + 120.0f;
        else s.restY = it.pos.y;
        it.moveVel.y = 1.0f;
        if (it.cls == kClsFlesh) s.fleshTimer = 300;
    } else {
        publishRaised(it);   // FUN_001cec60
    }
}
// orig FUN_001cec10 (work/decompiled/001cec10.c): settled state exit, ring effects stopped (FUN_00112530)
void exitSettled(Item& it, ItemSystem& sys) {
    if (it.state.ringEffect != -1) sys.game->effectKill(it.state.ringEffect);
    if (it.state.crystalRing != -1) sys.game->effectKill(it.state.crystalRing);
    it.state.ringEffect = -1;
    it.state.crystalRing = -1;
}

// orig FUN_001cec60 (work/decompiled/001cec60.c): draw offset along up (not kept: the model matrix is renderer side)

// orig FUN_001cf3e0 (work/decompiled/001cf3e0.c): disk step trigger
void diskTick(Item& it, ItemSystem& sys) {
    GameAccess& g = *sys.game;
    ItemStateSlot& s = it.state;
    CharacterAccess& p = g.player();
    if (p.brand() == kIdST16) {
        Vec3 d = sub(it.pos, p.position());
        if (normalize(d) < 600.0f && dot(p.forward(), d) > 0.5f) {
            if (!(s.lflags & 2)) {
                s.lflags |= 2;
                it.flags |= kItemIdentified;
                g.message(0x248, itemDisplayName(it, sys));
            }
        } else {
            s.lflags &= ~2u;
        }
    }
    auto within = [&](CharacterAccess& c) {   // FUN_001cf9e0
        if (!c.aiAlertable() || c.actorType() == 1) return false;
        const Vec3 cp = c.position();
        const float dx = cp.x - it.pos.x, dz = cp.z - it.pos.z;
        if (!(dz * dz + dx * dx < 3600.0f)) return false;
        if (it.pos.y + 10.0f <= cp.y) return false;
        return it.pos.y < cp.y + c.bodyHeight();
    };
    auto reversedBenefit = [&](CharacterAccess& c) {   // FUN_001cfaf0
        if (!(it.flags & kItemReversed) || !c.isPlayer()) return false;
        const uint32_t m = c.statusMask();
        switch (it.idx) {
            case 0x19: return (m & 0x10) == 0;
            case 0xe: return (m & 0x7f) == 0;
            case 0xd: return (m & 0x40) == 0;
            case 0xc: return (m & 8) == 0;
            case 0xb: return (m & 2) == 0;
            case 0x16: return true;
            default: return it.idx >= 1 && it.idx <= 0x11 && it.idx != 0xb && it.idx != 0xc && it.idx != 0xd && it.idx != 0xe;
        }
    };
    auto apply = [&](CharacterAccess& c) {   // FUN_001cfcb0
        // ring effect for idx 0: the 0xf3 (reversed) variant compares the ITEM (state +4) with the player
        // (0x1cfd1c [V]), never true: always 0x89
        if (s.ringEffect == -1 && it.idx == 0) s.ringEffect = g.effect(0x89, it.pos);
        return diskEffect(sys, it, c);
    };
    if (s.standing) {
        bool found = false;
        const int n = g.characterCount();
        for (int i = 0; i < n; ++i) {
            CharacterAccess* c = g.character(i);
            if (c == s.standing && !c->isRemoved()) { found = true; break; }
        }
        if (found && aliveCombatant(*s.standing) && within(*s.standing)) {
            if (it.idx != 0) return;
            apply(*s.standing);
            return;
        }
        if (s.ringEffect != -1) {                    // FUN_001126e0(h, 0, -1, -1): the ring ends
            g.effectEnd(s.ringEffect);
            s.ringEffect = -1;
        }
        if (diskSpend(it, sys)) { g.message(0x24d, itemDisplayName(it, sys)); return; }
        s.standing = nullptr;
    }
    bool any = false;
    const int n = g.characterCount();
    for (int i = 0; i < n; ++i) {
        CharacterAccess* c = g.character(i);
        if (!c || !aliveCombatant(*c) || !within(*c)) continue;
        if (reversedBenefit(*c)) {
            if (sys.inventory->add(&it, sys)) { obtainedItem(it, sys); return; }
            if (!(s.lflags & 1)) g.message(0x2cf, itemDisplayName(it, sys));
        } else {
            s.standing = c;
            s.standingSerial = 0;
            if (c->isPlayer()) {
                g.talkEventStep((it.flags & kItemReversed) == 0);
                g.setFlag(700);
                if (!(s.lflags & 1)) g.message(0x25b, itemDisplayName(it, sys));
            } else if (!(s.lflags & 1)) {
                g.message(0x25a, itemDisplayName(it, sys));
            }
            if (apply(*c)) { g.effect(0x88, it.pos); return; }   // FUN_0012adb0(0x88, pos)
            s.standing = nullptr;
        }
        any = true;
    }
    if (any) s.lflags |= 1; else s.lflags &= ~1u;
}

// orig FUN_001ced50 (work/decompiled/001ced50.c): settled item tick
void updateSettled(Item& it, ItemSystem& sys, CollisionQuery& cq) {
    GameAccess& g = *sys.game;
    ItemStateSlot& s = it.state;
    if (it.pos.y < -10000.0f) { g.removeFromWorld(&it); return; }
    if (it.cls != kClsIS && it.cls != kClsFlesh) {
        // push apart overlapping items of the same height
        int n = 0;
        Vec3 acc;
        const int ni = g.floorItemCount();
        for (int i = 0; i < ni; ++i) {
            Item* o = g.floorItem(i);
            if (!o || !(o->taskFlags & 1) || o->removed() || o->cls == kClsIS || o->cls == kClsFlesh || o == &it) continue;
            const float rr = kItemClassPhysics[it.cls].radius + kItemClassPhysics[o->cls < kItemClassCountAll ? o->cls : 0].radius;
            const float dz = o->pos.z - it.pos.z, dx = o->pos.x - it.pos.x;
            const float d2 = dz * dz + dx * dx;
            if (d2 < rr * rr && it.pos.y < o->pos.y + 10.0f && o->pos.y < it.pos.y + 10.0f) {
                Vec3 t;
                if (d2 == 0.0f) t = {it.fwd.x * rr + it.pos.x, it.fwd.y * rr + it.pos.y, it.fwd.z * rr + it.pos.z};
                else {
                    const float d = std::sqrt(d2), k = d - rr;
                    t = {(dx / d) * k + it.pos.x, it.pos.y, (dz / d) * k + it.pos.z};
                }
                acc.x += t.x; acc.y += t.y; acc.z += t.z;
                n++;
            }
        }
        if (n == 0) it.moveVel = {};
        else {
            const float f = static_cast<float>(n);
            it.moveVel = {acc.x / f - it.pos.x, acc.y / f - it.pos.y, acc.z / f - it.pos.z};
        }
        const uint32_t m = cq.moveAndCollide(it);
        if (it.hasFloorNormal) {
            Vec3 nrm = it.floorNormal;
            if (it.flags & kItemReversed) nrm = {-nrm.x, -nrm.y, -nrm.z};
            alignUp(it, nrm);
        }
        if (m & kContactLanded) bounceItem(it, sys);
        publishRaised(it);   // FUN_001cec60
    } else {
        if (s.fleshTimer != 0) {
            s.fleshTimer--;
            if (s.fleshTimer == 0) {
                const float sc = it.scale - 0.1f;
                if (sc >= 0.1f) { it.scale = sc; publishModel(it); s.fleshTimer++; return; }
                it.pos.y = -100.0f;
                publishModel(it);
                g.removeFromWorld(&it);
                return;
            }
        }
        if (it.pos.y < s.restY) { it.moveVel.y += 0.05f; if (it.moveVel.y > 1.0f) it.moveVel.y = 1.0f; }
        else { it.moveVel.y -= 0.05f; if (it.moveVel.y < -1.0f) it.moveVel.y = -1.0f; }
        it.pos.y += it.moveVel.y;
        publishModel(it);
    }
    const bool lust = (g.player().statusMask() & 0x10) != 0;
    if (it.cls == kClsTP) {
        diskTick(it, sys);
    } else {
        if (pickupTest(it, (s.lflags & 1) != 0, sys)) s.lflags |= 1; else s.lflags &= ~1u;
        if (it.cls == kClsIS && s.crystalRing == -1 && !lust) s.crystalRing = g.effect(0x92, it.pos, 75.0f);
    }
    if (s.crystalRing != -1) {
        if (!it.hasModel || (it.flags & 0x10000000u) != 0 || it.cls != kClsIS || lust) {
            g.effectKill(s.crystalRing);             // FUN_00112530
            s.crystalRing = -1;
        } else {
            g.effectMove(s.crystalRing, it.pos);     // FUN_001122d0(h, item pos)
        }
    }
}
}  // namespace

// orig FUN_001d5180 (work/decompiled/001d5180.c)
void worldItemTick(Item& item, ItemSystem& sys, CollisionQuery& cq) {
    ItemStateSlot& s = item.state;
    if (s.pending != 0) {
        s.lastFactory = s.factory;
        if (static_cast<ItemState>(s.current) == ItemState::Settled) exitSettled(item, sys);   // vt+0x14 (others empty)
        s.current = 0;
        if (s.pending == -1) { s.factory = 0; s.pending = 0; }
        else {
            s.factory = s.pending;
            s.pending = 0;
            s.current = s.factory;
            s.param0 = s.param1;
            switch (static_cast<ItemState>(s.current)) {
                case ItemState::Lying: enterLying(item, cq); break;
                case ItemState::Thrown: enterThrown(item, sys); break;
                case ItemState::Generic: enterGeneric(item); break;
                case ItemState::Dropped: enterDropped(item, sys); break;
                case ItemState::Settled: enterSettled(item, cq); break;
                default: break;
            }
        }
    }
    switch (static_cast<ItemState>(s.current)) {
        case ItemState::Lying: updateLying(item, sys, cq); break;
        case ItemState::Thrown: updateThrown(item, sys, cq); break;
        case ItemState::Generic: updateGeneric(item, sys, cq); break;
        case ItemState::Dropped: updateDropped(item, sys, cq); break;
        case ItemState::Settled: updateSettled(item, sys, cq); break;
        default: break;
    }
}

// orig FUN_001d5410 (work/decompiled/001d5410.c)
void dropItemAtActor(Item& item, float yawDeg, CharacterAccess* actor, ItemSystem& sys) {
    resetAxes(item);                                     // FUN_001286c0
    float yaw = 0.0f;
    if (yawDeg < 0.0f) yaw = randomYaw(sys);
    rotateAboutUp(item, yaw);                            // FUN_00128ae0
    if (actor) {
        item.pos = actor->position();
        item.pos.y = actor->height() * 0.5f + item.pos.y + 0.0f;   // PRMCHAR +0xa0 (FUN_00133560)
        if (item.cls == kClsFlesh) item.idx = static_cast<int>(actor->charId());
    }
    if (actor || (item.flags & kItemPlacedB)) {
        if (item.cls == kClsTP) {
            if (sys.game->floorId() == 0x5b && (item.flags & kItemPlacedB)) item.flags |= kItemPlacedA;
            else setDiskFace(item, sys);                 // FUN_001d5250
        }
    }
    sys.game->itemEnterWorld(item);                      // FUN_001d3a30
    itemRequestState(item, ItemState::Lying);            // LAB_001d5610 -> 0x1db750
}

// orig FUN_001d5620 (work/decompiled/001d5620.c): placed by the floor (random items FUN_00121f80, scripted items)
void placeItem(Item& item, ItemSystem& sys) {
    item.flags |= kItemPlacedB;
    dropItemAtActor(item, -1.0f, nullptr, sys);
}

// orig FUN_001d5640 (work/decompiled/001d5640.c)
void throwHeldItem(Item& item, const Vec3& pos, const Vec3& dir, CharacterAccess& thrower, ItemSystem& sys) {
    GameAccess& g = *sys.game;
    if (thrower.isPlayer()) {
        g.addCounter(10, 1);
        if (item.cls == kClsBN && item.count > 1) {
            item.count--;
            Item* one = g.createItem(item.id, true);
            if (one) {
                if (item.identified()) one->flags |= kItemIdentified;
                one->count = 1;
                launchItem(*one, pos, dir, &thrower);
            }
            return;
        }
        if (sys.inventory->remove(sys.inventory->indexOf(&item), sys) == nullptr) return;
    } else {
        thrower.dropHeldItem();                          // FUN_00139540(status): status block +0x60 cleared
    }
    launchItem(item, pos, dir, &thrower);
}

// orig FUN_001d5780 (work/decompiled/001d5780.c)
void launchItem(Item& item, const Vec3& pos, const Vec3& dir, CharacterAccess* thrower) {
    item.thrower = thrower;
    item.throwerId = thrower ? thrower->charId() : 0;
    item.throwerSerial = 0;
    item.pos = pos;
    item.velocity = dir;
    item.flags |= kItemInFlight;
    itemSystem().game->itemEnterWorld(item);                    // FUN_001d3a30
    itemRequestState(item, ItemState::Thrown);           // LAB_001d5840 -> 0x1db7e0
}

// orig FUN_001d5850 (work/decompiled/001d5850.c): 30 (40 with brand ST07) * (1 + 0.2 * hidden[3]/255)
float throwSpeed(ItemSystem& sys, bool byPlayer) {
    if (!byPlayer) return 30.0f;
    CharacterAccess& p = sys.game->player();
    float v = 30.0f;
    if (p.brand() == kIdST07) v = 40.0f;
    return v * ((static_cast<float>(p.hiddenStat(3)) / 255.0f) * 0.2f + 1.0f);
}

// orig FUN_001d5920 / FUN_001d5990 (work/decompiled/001d5920.c, 001d5990.c)
bool tossFromMenu(Item& item, const Vec3& pos, const Vec3& vel, ItemSystem& sys) {
    if (sys.inventory->remove(sys.inventory->indexOf(&item), sys) == nullptr) return false;
    tossItem(item, pos, vel);
    return true;
}
void tossItem(Item& item, const Vec3& pos, const Vec3& vel) {
    item.pos = pos;
    item.moveVel = vel;
    item.flags |= kItemInFlight;
    itemSystem().game->itemEnterWorld(item);                    // FUN_001d3a30
    itemRequestState(item, ItemState::Generic);          // LAB_001d5a40 -> 0x1db870
}

// orig FUN_001d5250 (work/decompiled/001d5250.c): flag 0x39d or 50 % -> flipped (FUN_00128c00(pi))
void setDiskFace(Item& item, ItemSystem& sys) {
    if (item.cls != kClsTP) return;
    if (sys.game->testFlag(0x39d)) rotateAboutRight(item, kPi);
    else if (sys.rand(100) < 0x32) rotateAboutRight(item, kPi);
    updateDiskFace(item);
}
void setDiskFace(Item& item, ItemSystem& sys, CollisionQuery&) { setDiskFace(item, sys); }

// orig FUN_001d4db0 (work/decompiled/001d4db0.c): up . FUN_00206f10 (0, 1, 0) < 0 -> reversed
void updateDiskFace(Item& item) {
    const Vec3 w{0.0f, 1.0f, 0.0f};
    if (item.up.z * w.z + item.up.x * w.x + item.up.y * w.y < 0.0f) item.flags |= kItemReversed; else item.flags &= ~kItemReversed;
}
void updateDiskFace(Item& item, CollisionQuery&) { updateDiskFace(item); }

// orig FUN_001d52f0 (work/decompiled/001d52f0.c)
void settleItem(Item& item, CollisionQuery&) {
    item.shadow = false;    // FUN_00109f70(model, 0)
    itemRequestState(item, ItemState::Settled);   // LAB_001d5400 -> 0x1db6c0
    if (item.cls == kClsIS || item.cls == kClsFlesh) return;
    if (item.cls == kClsTP) updateDiskFace(item);
    if (item.hasFloorNormal) {
        Vec3 n = item.floorNormal;
        if (item.flags & kItemReversed) n = {-n.x, -n.y, -n.z};
        alignUp(item, n);
    }
}

// orig FUN_001cea30 (work/decompiled/001cea30.c)
void bounceItem(Item& item, ItemSystem& sys) {
    if (item.hasFloorNormal) sys.game->effect(0x96, item.pos);
    sys.game->se3d(0xb6, item.pos);
}

// orig FUN_001ce7e0 (work/decompiled/001ce7e0.c)
bool pickupTest(Item& item, bool quiet, ItemSystem& sys) {
    GameAccess& g = *sys.game;
    CharacterAccess& p = g.player();
    if (!aliveCombatant(p)) return false;
    const Vec3 pp = p.position();
    const float dx = pp.x - item.pos.x, dz = pp.z - item.pos.z;
    const float r = p.radius() + kItemClassPhysics[item.cls < kItemClassCountAll ? item.cls : 0].radius;
    if (!(dz * dz + dx * dx < r * r)) return false;
    // the third test reads the item FLAGS word +0x70 (bit 0x10000000), not the task flags +0x10
    if (!(pp.y < item.pos.y + 10.0f && item.pos.y < pp.y + p.bodyHeight()) || (item.flags & 0x10000000u) != 0) return false;
    if (item.cls == kClsFlesh) {
        g.se(0x4a);
        // PRMCHAR +0x90 / +0x94 of the creature whose flesh this is (FUN_00133560(idx))
        if (sys.prm) {
            const data::PrmChar* pc = data::findPrmChar(sys.prm->chars, static_cast<uint32_t>(item.idx));
            if (pc) { p.addHp(pc->fleshHp); p.addVt(pc->fleshVt); }
        }
        item.pos.y = -100.0f;
        g.removeFromWorld(&item);
    } else if (sys.inventory->add(&item, sys)) {
        obtainedItem(item, sys);
    } else if (!quiet) {
        g.message(0x2cf, itemDisplayName(item, sys));   // "At maximum capacity. Can't hold %s."
    }
    return true;
}

// orig FUN_001ce730 (work/decompiled/001ce730.c)
void obtainedItem(Item& item, ItemSystem& sys) {
    GameAccess& g = *sys.game;
    g.se(0x4b);
    g.message(0x261, itemDisplayName(item, sys));       // "Obtained %s."
    g.talkEventItemTaken(&item);
    item.pos.y = -100.0f;
    publishModel(item);                                  // FUN_00109d40
    g.itemLeaveWorld(item);                              // FUN_001d3b30
    item.onFloor = false;                                // FUN_001421a0 clears the ground info
    item.floorAttr = nullptr;
    item.hasFloorNormal = false;
}

// orig FUN_001d0220 (work/decompiled/001d0220.c)
int cubitValue(ItemSystem& sys) {
    GameAccess& g = *sys.game;
    if (g.isDungeonType1()) {
        if (g.testFlag(0x25)) return 0xf3c;
        if (g.testFlag(0x24)) return 3000;
        if (g.testFlag(0x23)) return 0x898;
        if (g.testFlag(0x22)) return 0x640;
        return 400;
    }
    if (g.isHubFloor()) return 700;
    if (g.isDungeonType3()) return 0x26ac;
    return 0;
}
void printCubits(ItemSystem& sys) {
    const int v = cubitValue(sys);
    if (v == 0) sys.game->message(0x245); else sys.game->message(0x244, v);
}

// orig FUN_001d4c90 (work/decompiled/001d4c90.c)
bool diskSpend(Item& item, ItemSystem& sys) {
    item.count--;
    if (item.count != 0) return false;
    GameAccess& g = *sys.game;
    g.effect(0xe9, item.pos);
    g.se3d(0xd6, item.pos);
    if (item.flags & kItemPlacedA) g.setFlag(0x60);
    g.removeFromWorld(&item);
    return true;
}

// orig FUN_001d9040 (work/decompiled/001d9040.c): "DM"/"DD" actions refuse, Collector 'C23' needs bag room
bool npcAcceptsItem(CharacterAccess& npc, ItemSystem& sys) {
    if (npc.refusesItems()) return false;
    if (npc.currentActionIs("DM") || npc.currentActionIs("DD")) return false;
    if ((npc.charId() & 0xffffff) == (itemId("C23\0") & 0xffffff)) {
        if (sys.bag->isFull(*sys.game)) return false;
        return !npc.isHoldingItem();
    }
    return !npc.isHoldingItem();
}

// orig FUN_001d91a0 (work/decompiled/001d91a0.c)
bool giveItemToNpc(Item& item, CharacterAccess& npc, ItemSystem& sys) {
    if ((npc.charId() & 0xffffff) == (itemId("C23\0") & 0xffffff)) {
        if (!npc.takeItem(&item)) return false;
        sys.bag->put(&item, false, sys);
        if (item.cls == kClsGN) sys.bag->take(sys.bag->indexOf(&item), sys);   // FUN_001dafa0: a gun is not kept in the bag
    } else if (!npc.takeItem(&item)) {
        return false;
    }
    sys.game->itemLeaveWorld(item);   // FUN_001d3b30
    return true;
}

// ---- menu actions ------------------------------------------------------------------------------

bool MenuItemAction::canThrow(ItemSystem& sys, int idx) const {   // orig FUN_001d8cc0
    Inventory& v = *sys.inventory;
    if (v.equippedSlotOfIndex(idx) == -1) return true;
    return v.equipLegal(idx, false, sys);
}
bool MenuItemAction::canPlace(ItemSystem& sys, int idx) const { return canThrow(sys, idx); }   // orig FUN_001d8bb0 (identical)
CharacterAccess* MenuItemAction::giveTarget(ItemSystem& sys) const {
    GameAccess& g = *sys.game;
    const float r = g.isWideGiveRange() ? 150.0f : 100.0f;   // FUN_001d8d20
    return g.nearest(r, 30.0f, &g.player(), 1, nullptr);
}
bool MenuItemAction::canGive(ItemSystem& sys, int idx) {   // orig FUN_001d8e40
    if (!canThrow(sys, idx)) return false;
    CharacterAccess* t = giveTarget(sys);
    if (!t) return false;
    const uint32_t id = t->charId(), id3 = id & 0xffffff;
    if (id3 == (itemId("C18\0") & 0xffffff) || id3 == (itemId("C04\0") & 0xffffff)) return false;
    if (id == itemId("C19B") || id == itemId("C19C") || id == itemId("C19D")) return false;
    return npcAcceptsItem(*t, sys);
}
bool MenuItemAction::beginThrow(ItemSystem& sys, int idx) {   // orig FUN_001d8c10
    if (!canThrow(sys, idx)) return false;
    sys.inventory->usingItem = sys.inventory->slots[idx];
    kind = Throw;
    sys.game->playerActionRequest("TH00", 0);
    return true;
}
bool MenuItemAction::beginPlace(ItemSystem& sys, int idx, bool headsUp) {   // orig FUN_001d8a40
    if (!canPlace(sys, idx)) return false;
    Item* it = sys.inventory->slots[idx];
    sys.inventory->usingItem = it;
    kind = Place;
    heads = headsUp;
    sys.game->playerActionRequest("TH06", 0);
    randomAxes(*it, sys);                                // FUN_001286c0 + FUN_00128ae0
    if (it->cls == kClsTP && !headsUp) rotateAboutRight(*it, kPi);   // FUN_00128c00(pi)
    return true;
}
bool MenuItemAction::beginGive(ItemSystem& sys, int idx) {   // orig FUN_001d8d60
    if (!canGive(sys, idx)) return false;
    sys.inventory->usingItem = sys.inventory->slots[idx];
    sys.inventory->placeTarget = giveTarget(sys);
    kind = Give;
    sys.game->playerActionRequest("TH07", 0);
    return true;
}
void MenuItemAction::tick(ItemSystem& sys, bool trigger, const Vec3& handPos) {
    if (kind == None) return;
    Inventory& v = *sys.inventory;
    Item* it = v.usingItem;
    CharacterAccess& p = sys.game->player();
    if (!trigger || !it) return;
    switch (kind) {
        case Throw: throwHeldItem(*it, handPos, p.forward(), p, sys); break;                 // FUN_001ce2f0 -> FUN_001d5640
        case Place: { Vec3 f = p.forward(); tossFromMenu(*it, handPos, {f.x * 10.0f, f.y * 10.0f, f.z * 10.0f}, sys); break; }  // FUN_001ce3f0
        case Give:                                                                          // FUN_001ce520
            if (v.placeTarget && v.remove(v.indexOf(it), sys)) giveItemToNpc(*it, *v.placeTarget, sys);
            break;
        default: break;
    }
    kind = None;
    v.usingItem = nullptr;
}

// ---- traps -----------------------------------------------------------------------------------------

// orig FUN_001e57d0 (work/decompiled/001e57d0.c)
void trapInit(TrapObject& t, const Vec3& pos, int kind, uint32_t power, uint32_t statusMask, uint32_t reactMask,
              int effectId, CharacterAccess* owner, uint32_t tag, ItemSystem& sys) {
    GameAccess& g = *sys.game;
    t.pos = pos; t.power = power; t.statusMask = statusMask; t.reactMask = reactMask; t.effectId = effectId;
    t.owner = owner; t.tag = tag; t.ownerHit = 0;
    t.room = owner ? owner->roomIndex() : g.roomAt(pos);
    t.radius = -1.0f;
    if (kind == 3) {
        t.room = -1;
    } else if (kind == 2) {
        if (t.room < 0) t.radius = 600.0f;
        if (power & 1) g.se3d(0xba, pos);
        if (power & 2) g.se3d(0xc0, pos);
        if (power & 4) g.se3d(0xbd, pos);
        t.power |= 0x40000;
    } else if (kind == 1) {
        t.radius = 600.0f;
        if (power & 1) { g.effect(0x6f, pos); g.se3d(0xb9, pos); }
        if (power & 2) { g.effect(0x7e, pos); g.se3d(0xbf, pos); }
        if (power & 4) { g.effect(0x81, pos); g.se3d(0xbc, pos); }
        if (statusMask == 0x7f) g.effect(0x8b, pos);
        else {
            if (statusMask & 1) g.effect(0x71, pos);
            if (statusMask & 2) g.effect(0xcf, pos);
            if (statusMask & 4) g.effect(0x72, pos);
            if (statusMask & 8) g.effect(0x73, pos);
            if (statusMask & 0x40) g.effect(0x74, pos);
            if (statusMask & 0x10) g.effect(0x75, pos);
            if (statusMask & 0x20) g.effect(0x76, pos);
        }
        if (reactMask & 0x800) g.effect(0x78, pos);
    } else if (kind == 0) {
        t.radius = 200.0f;
        if (power & 1) { g.effect(0x6e, pos); g.se3d(0xb8, pos); }
        if (power & 2) { g.effect(0x7d, pos); g.se3d(0xbe, pos); }
        if (power & 4) { g.effect(0x80, pos); g.se3d(0xbb, pos); }
    }
}

// orig FUN_001e5e70 (work/decompiled/001e5e70.c)
bool trapHitsCharacter(const TrapObject& t, CharacterAccess& c, ItemSystem&) {
    if (t.ownerHit == 0 && &c == t.owner) return false;
    if (c.actorType() == 1 || !c.aiAlertable() || c.isRemoved()) return false;
    if (!aliveCombatant(c)) return false;
    if (t.radius < 0.0f) {
        if (t.room < 0) return true;
        return c.roomIndex() == t.room;
    }
    const Vec3 d = sub(c.position(), t.pos);
    return length(d) - c.radius() <= t.radius;
}

// orig FUN_001e6190 (work/decompiled/001e6190.c)
void trapApplyCharacter(TrapObject& t, CharacterAccess& c, ItemSystem& sys) {
    GameAccess& g = *sys.game;
    c.hitElementFx(t.power);                             // FUN_0014c580(chr, 0, +0x2c, 0)
    if (t.effectId == 0) {
        if (t.reactMask != 0 && (t.reactMask & 0x800)) c.react(t.reactMask, t.pos, false);
    } else {
        const bool diskTag = (t.tag & 0xff) == 'T' && ((t.tag >> 8) & 0xff) == 'P';
        CharacterAccess* attacker = diskTag ? nullptr : t.owner;
        // FUN_00136170(status, attacker, +0x38, +0x2c), FUN_00136580(status, +0x3c, dmg, +0x34, 0, 0)
        const int dmg = c.projectileDamage(attacker, t.effectId, t.power);
        if (c.applyDamage(t.owner, dmg, t.reactMask)) {
            if (t.owner && t.owner->isPlayer()) {
                sys.inventory->bx24Tick();
                g.addCounter(4, 1);
                g.addCounter(0x16, 1);
                if ((t.tag & 0xff) == 'T' && ((t.tag >> 8) & 0xff) == 'L') g.addCounter(9, 1);
                if (t.tag == itemId("ST17")) g.addCounter(0xf, 1);
            } else if (c.isPlayer() && diskTag) {
                g.setFlag(799);
                g.setGuardedFlag(1099);
            }
            c.die();
            return;
        }
        c.react(t.reactMask, t.pos, false);
    }
    if (t.statusMask != 0) c.inflict(t.statusMask);
}

// orig FUN_001e6070 (work/decompiled/001e6070.c)
bool trapHitsItem(const TrapObject& t, const Item& it, ItemSystem& sys) {
    if (t.radius < 0.0f) {
        if (t.room < 0) return true;
        return sys.game->roomAt(it.pos) == t.room;
    }
    return length(sub(it.pos, t.pos)) <= t.radius;
}

// orig FUN_001e6410 (work/decompiled/001e6410.c)
void trapApplyItem(TrapObject& t, Item& it, ItemSystem& sys) {
    if (t.power & 1) {
        // FUN_001d4860(item, id) with a1 = the compared id word (0x1e6458..0x1e647c / 0x1e64b8..0x1e64dc [V]):
        // "HT02" (0x464758) / "MT03" (0x464760)
        if (it.cls == kClsHT && it.id != kIdHT02) changeItemId(it, kIdHT02, sys);
        else if (it.cls == kClsMT && it.id != kIdMT03) changeItemId(it, kIdMT03, sys);
    }
    if (t.statusMask & 0x1000) {
        Vec3 out;
        if (sys.game->randomCellInRoom(t.room, out)) it.pos = out;
    }
}

// orig FUN_001e5ca0 (work/decompiled/001e5ca0.c): applies once and removes itself
void trapApply(TrapObject& t, ItemSystem& sys) {
    GameAccess& g = *sys.game;
    if (t.owner && !aliveCombatant(*t.owner)) t.owner = nullptr;
    const int nc = g.characterCount();
    for (int i = 0; i < nc; ++i) {
        CharacterAccess* c = g.character(i);
        if (c && trapHitsCharacter(t, *c, sys)) trapApplyCharacter(t, *c, sys);
    }
    if ((t.power & 1) || (t.statusMask & 0x1000)) {
        const int ni = g.floorItemCount();
        for (int i = 0; i < ni; ++i) {
            Item* it = g.floorItem(i);
            if (it && (it->taskFlags & 1) && !it->removed() && trapHitsItem(t, *it, sys)) trapApplyItem(t, *it, sys);
        }
    }
}

// orig FUN_0013a150 (work/decompiled/0013a150.c)
void aimArc(Item& item, const Vec3& start, const Vec3& dir, const Vec3& side, ItemSystem& sys, Vec3 verts[60],
            uint32_t colours[60], float& phase, float& angle) {
    phase -= 0.05f;
    if (phase < 0.0f) phase = 1.0f;
    angle += 7.0f;
    if (angle >= 360.0f) angle -= 360.0f;
    float t = phase;
    float speed = throwSpeed(sys, true);
    float vy = -dir.y * speed;
    const int cls = item.cls < kItemClassCountAll ? item.cls : 0;
    const float decel = kItemClassPhysics[cls].decel;
    const float gscale = kItemClassPhysics[cls].gravityScale;
    Vec3 p = start;
    int every = 1;
    int n = 0;
    while (n < 0x3c) {
        vy = vy + gscale * 3.0f;             // FUN_00141950 with the thrown gravity scale
        if (vy > 100.0f) vy = 100.0f;
        const float dx = dir.x * speed, dz = dir.z * speed;
        speed -= decel;
        if (speed < 0.0f) speed = 0.0f;
        if (--every == 0) {
            every = 2;
            float cf = (t * 0.75f + 0.25f) * 255.0f;
            if (cf >= 2.1474836e+09f) cf -= 2.1474836e+09f;
            const uint32_t col = (static_cast<uint32_t>(static_cast<int>(cf)) << 8) | 0xff000000u;
            colours[n] = col; colours[n + 1] = col;
            const Vec3 o{side.x * t, side.y * t, side.z * t};
            if (n == 0) { verts[0] = p; verts[1] = p; }
            verts[n] = {p.x + o.x, p.y + o.y, p.z + o.z};
            verts[n + 1] = {p.x - o.x, p.y - o.y, p.z - o.z};
            n += 2;
            t += 0.05f;
            if (t > 1.0f) t = 0.0f;
        }
        p.x += dx; p.y -= vy; p.z += dz;
    }
}

}  // namespace baroque::game
