// Task records the item module creates in the character task list 0x50b134 (game/items/game_adapter.hpp
// ItemTaskHooks): the one-shot trap object FUN_001e5610 and the BN18 bone field FUN_001e35f0. Both are type-5 helper
// records (ObjClass::Helper) ticked by the Game task's character walk (objectTick -> itemTaskTick) and destroyed by
// the reaper (objectDestroy -> itemTaskDestroy). Report: work/reports/ITEMS_HUD_NATIVE.md section 12.
#include "game/objects/objects.hpp"
#include "game/objects/objects_internal.hpp"

#include "game/collision.hpp"
#include "game/effects/effects.hpp"
#include "game/game_world.hpp"
#include "game/hud/hud_api.hpp"
#include "game/items/game_adapter.hpp"
#include "game/items/item_world.hpp"

#include <array>
#include <memory>

#pragma fenv_access(on)

namespace baroque::game {

namespace {

constexpr uint32_t kHelperTrap = 0x1e7630u;        // FUN_001e7630 (ctor FUN_001e5710, vtable 0x4cf120)
constexpr uint32_t kHelperBoneField = 0x1e4400u;   // FUN_001e4400 (ctor FUN_001e3780, vtable 0x4cef40)

// Bone-field list 0x53dce0[0x1e] and its count uRam0053dcd8. The port keeps a weak reference beside each entry:
// ChrManager::destroyAll (new game / title) frees the records without running the class destructors, so an
// expired entry stands for a record whose FUN_001e3820 never ran; it is dropped the way that destructor would.
struct FieldEntry {
    Chr* chr = nullptr;
    std::weak_ptr<ObjData> alive;
};
std::array<FieldEntry, 0x1e> gFields;
uint32_t gFieldCount = 0;

void purgeDestroyedFields() {   // PORT: the skipped FUN_001e3820 of records freed by destroyAll
    for (FieldEntry& e : gFields) {
        if (e.chr != nullptr && e.alive.expired()) {
            e = FieldEntry{};
            --gFieldCount;
        }
    }
}

// FUN_00128590 + FUN_00128690(obj, 5, 0) + FUN_002073e0(obj, 0x50b134, 0x50b138) (iRam0050b13c++)
Chr* linkRecord(std::unique_ptr<Chr> h, const Vec3f& pos) {
    Chr* c = characters().adopt(std::move(h));
    c->type = kChrEffect;
    c->id = 0;
    c->xf.m.pos = pos;
    c->prevPos = pos;
    c->taskFlags |= 1u;
    characters().link(c);
    return c;
}

std::unique_ptr<Chr> newRecord(uint32_t ctorAddr) {
    auto c = std::make_unique<Chr>();
    c->obj = std::make_shared<ObjData>();
    c->obj->cls = ObjClass::Helper;
    c->obj->helperKind = ctorAddr;
    c->xf = Transform{};   // FUN_00128590: scale 1, position 0, FUN_001286c0 axes
    resetAxes(c->xf.m);
    physInit(c->phys, &c->xf.m.pos, &c->shape, 0.0f);
    return c;
}

// orig FUN_001e3780 (work/decompiled/001e3780.c): the object enters the first free entry of 0x53dce0 (count + 1);
// with all 30 taken it is not listed.
void fieldRegister(Chr& c) {
    for (FieldEntry& e : gFields) {
        if (e.chr == nullptr) {
            e.chr = &c;
            e.alive = c.obj;
            ++gFieldCount;
            return;
        }
    }
}

// orig FUN_001e35f0 (work/decompiled/001e35f0.c): FUN_001e4400 (FUN_00132b50 pool alloc, FUN_001e3780, vtables
// 0x4cef40 / 0x4cef50), FUN_00128690(obj, 5, 0), FUN_001e3900, link; with more than 0x13 fields listed, the listed
// one farthest from the player (squared distance; the first of equal ones; -1.0 start) leaves the list (count - 1)
// and is removed (FUN_00207350; its destructor runs when the walk frees it).
bool spawnBoneField(const Vec3& at) {
    purgeDestroyedFields();
    const Vec3f pos{at.x, at.y, at.z};
    std::unique_ptr<Chr> h = newRecord(kHelperBoneField);
    Chr& rec = *h;
    fieldRegister(rec);
    // orig FUN_001e3900 (work/decompiled/001e3900.c): ground effect 0x91 -> +0x24, position, SE 0x74 at pos
    rec.obj->I(0x24) = fxOnGround(0x91, pos);   // FUN_0012adb0(0x91, pos)
    objSe3d(0x74, pos, 1.0f);                    // FUN_00115470(1.0, 0x4e9790, 0x74, pos)
    linkRecord(std::move(h), pos);
    if (0x13u < gFieldCount) {
        const Chr* pl = characters().player();   // iRam0050b144
        const Vec3f pp = pl != nullptr ? pl->xf.m.pos : Vec3f{};
        float best = -1.0f;
        Chr* far = nullptr;
        for (const FieldEntry& e : gFields) {
            if (e.chr == nullptr) continue;
            const Vec3f& q = e.chr->xf.m.pos;
            const float dy = q[1] - pp[1];
            const float dx = q[0] - pp[0];
            const float dz = q[2] - pp[2];
            const float d = dz * dz + dx * dx + dy * dy;
            if (best < d) {   // `fVar8 <= fVar11` keeps the earlier one
                best = d;
                far = e.chr;
            }
        }
        if (far != nullptr) {
            for (FieldEntry& e : gFields) {
                if (e.chr == far) {
                    e = FieldEntry{};
                    --gFieldCount;
                    break;
                }
            }
            entityRemove(*far);   // FUN_00207350(far)
        }
    }
    return true;
}

// orig FUN_001e5610 (work/decompiled/001e5610.c): FUN_001e7630 (FUN_001071c0 alloc, FUN_001e5710: FUN_00128590,
// vtables 0x4cf030 -> 0x4cf120), FUN_00128690(obj, 5, 0), FUN_001e57d0 (trapInit, run by GameAdapter::spawnTrap on the
// TrapObject handed over), link at the end of the task list.
bool linkTrapRecord(std::unique_ptr<TrapObject> t) {
    if (!t) return false;
    const Vec3f pos{t->pos.x, t->pos.y, t->pos.z};   // trapInit wrote the position (xform +0xc)
    std::unique_ptr<Chr> h = newRecord(kHelperTrap);
    h->obj->extra = std::shared_ptr<TrapObject>(std::move(t));
    linkRecord(std::move(h), pos);
    return true;
}

struct Hooks final : ItemTaskHooks {
    bool linkTrap(std::unique_ptr<TrapObject> t) override { return linkTrapRecord(std::move(t)); }
    bool spawnFieldObject(const Vec3& pos) override { return spawnBoneField(pos); }
};
Hooks gHooks;
const bool gInstalled = (itemTaskHooks() = &gHooks, true);

}  // namespace

// orig FUN_0012adb0 (work/decompiled/0012adb0.c): probe from (x, y + 10, z) along (0, -300, 0) with radius 1.0
// (0x44a760) through FUN_00143150; on a hit y = y + 10 - nearest distance (FUN_001431e0) and the face of the first hit
// record (FUN_00143200(0)) goes to FUN_0012ab10; else -1.
// orig FUN_0012ab10 (work/decompiled/0012ab10.c): FUN_00112190(0, fx, 0, 0, 0); yaw = rand(0x167) degrees
// (FUN_002071e0(0x53eb20, 0x167)); f = FUN_00206ca0(yaw, (0, 0, 1) of FUN_00207010); a = n x f, b = a x n (both
// FUN_00206ab0); matrix rows a * 100, n * 100, b * 100 (0x44a748 / 0x44a750 / 0x44a758 = 100.0 [V]),
// translation (x, y + 0.15, z) over FUN_0039d2e0's identity; FUN_00112770.
int fxOnGround(int fx, const Vec3f& pos) {
    const float from[3] = {pos[0], pos[1] + 10.0f, pos[2]};
    const float delta[3] = {0.0f, -300.0f, 0.0f};   // 0xc3960000
    float dist = 0.0f;
    if (!raycast(from, delta, 1.0f, &dist)) return -1;
    const Vec3f at{from[0], from[1] - dist, from[2]};
    const GlobalHits& hits = globalHits();
    const GlobalHit* first = hits.list.empty() ? nullptr : &hits.list[0];
    // PORT guard: a character / object hit (no face) cannot occur for this static probe; the original would read the
    // null face pointer.
    if (first == nullptr || first->mesh == nullptr || first->face < 0 ||
        static_cast<size_t>(first->face) >= first->mesh->faces.size())
        return -1;
    const Vec3f n = first->mesh->faces[size_t(first->face)].normal;   // face +0xc
    const int h = effects().spawn(0.0f, fx, nullptr, nullptr, nullptr);
    const uint32_t r = gameWorld().rng.rand(0x167);
    const float yawDeg = static_cast<float>(static_cast<int>(r));
    Vec3f f{};
    static const Vec3f kZ{0.0f, 0.0f, 1.0f};   // FUN_00207010 (0x53eaf0 = (0, 0, 1))
    rotateY((yawDeg * 3.1415927f) / 180.0f, f, kZ);
    Vec3f a{n[1] * f[2] - n[2] * f[1], n[2] * f[0] - n[0] * f[2], n[0] * f[1] - n[1] * f[0]};
    normalize(a);
    Vec3f b{a[1] * n[2] - a[2] * n[1], a[2] * n[0] - a[0] * n[2], a[0] * n[1] - a[1] * n[0]};
    normalize(b);
    float m[16] = {1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f};
    m[0] = a[0] * 100.0f;
    m[2] = a[2] * 100.0f;
    m[1] = a[1] * 100.0f;
    m[10] = b[2] * 100.0f;
    m[9] = b[1] * 100.0f;
    m[8] = b[0] * 100.0f;
    m[6] = n[2] * 100.0f;
    m[5] = n[1] * 100.0f;
    m[4] = n[0] * 100.0f;
    m[12] = at[0];
    m[13] = at[1] + 0.15f;
    m[14] = at[2];
    fxSetMatrix(h, m);
    return h;
}

bool itemTaskTick(Chr& c) {
    if (c.obj == nullptr || c.obj->cls != ObjClass::Helper) return false;
    ObjData& o = *c.obj;
    if (o.helperKind == kHelperTrap) {
        // orig FUN_001e5ca0 (vtable 0x4cf120 +0xc): trapApply, then FUN_00207350(obj)
        if (auto* t = static_cast<TrapObject*>(o.extra.get())) trapApply(*t, itemSystem());
        entityRemove(c);
        return true;
    }
    if (o.helperKind == kHelperBoneField) return true;   // vtable 0x4cef40 +0xc = FUN_001e3970 (`jr ra`, empty) [V]
    return false;
}

bool itemTaskDestroy(Chr& c) {
    if (c.obj == nullptr || c.obj->cls != ObjClass::Helper) return false;
    ObjData& o = *c.obj;
    if (o.helperKind == kHelperTrap) return true;   // FUN_001e7a20 -> FUN_001e5760 -> FUN_00128610 (no side effects)
    if (o.helperKind != kHelperBoneField) return false;
    // orig FUN_001e4470 -> FUN_001e3820 (work/decompiled/001e3820.c): out of 0x53dce0 (count - 1) when listed, the
    // ground effect ended (FUN_001126e0(+0x24, 0, -1, -1)) unless it is -1, FUN_00128610
    for (FieldEntry& e : gFields) {
        if (e.chr == &c) {
            e = FieldEntry{};
            --gFieldCount;
            break;
        }
    }
    if (o.I(0x24) != -1) fxLoopOut(o.I(0x24), 0, -1, -1);
    return true;
}

}  // namespace baroque::game
