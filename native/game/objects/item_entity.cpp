// Item entities of the character task list (type 3, class 0x4cd960 = item 0x4ced90 on the actor base 0x4ccc70).
//
// In the original the item object IS the task: linked into 0x50b134 while it lies / flies in the world (ticked by
// vt+0xc FUN_001d5180), unlinked while it sits in an inventory, a box or the Collector's bag, deleted by the task walk
// when flagged 0x20000000. The port splits it in two:
//   * the Item (game/items/item.hpp), owned by GameAdapter (game/items/game_adapter.cpp) for its whole life, with the
//     item list 0x50a830 and the task word +0x10 (bit 0 linked, 0x10000000 unlink, 0x20000000 free);
//   * while the item is linked, a `Chr` record of the task list (`Chr::item` = the Item) that ChrManager walks, reaps
//     and draws. It carries the item's physics object (+0x30, vtable 0x4cdb90 body: `Chr::phys` / `Chr::shape`), the
//     model holder (+0x2c, `Chr::model`) and the item transform (`Chr::xf`).
// GameAdapter reaches the records through the ItemEntityHooks installed here; GameAdapter::purgeItems (once per engine
// tick) performs the item half of the walk (FUN_00207380 unlink / FUN_00101200 delete) once a record is gone, and
// keeps freed Items allocated while a record still points at them.
#include "alchemy/ps2_float.hpp"
#include "game/character/combat.hpp"
#include "game/hud/hud_api.hpp"
#include "game/items/chr_adapter.hpp"
#include "game/items/game_adapter.hpp"
#include "game/items/item_world.hpp"
#include "game/objects/objects.hpp"
#include "platform/platform.hpp"

#include <algorithm>

namespace baroque::game {

namespace {

// 0x464150 [V ELF data]: model of each item class (FUN_001d3a30)
const char* const kItemModelPath[kItemClassCountAll] = {
    "DATA/OBJ/OBJ001.IGB", "DATA/OBJ/OBJ003.IGB", "DATA/OBJ/OBJ004.IGB", "DATA/OBJ/OBJ005.IGB", "DATA/OBJ/OBJ006.IGB",
    "DATA/OBJ/OBJ007.IGB", "DATA/OBJ/OBJ008.IGB", "DATA/OBJ/OBJ009.IGB", "DATA/OBJ/OBJ010.IGB", "DATA/OBJ/OBJ011.IGB",
    "DATA/OBJ/OBJ012.IGB", "DATA/OBJ/OBJ013.IGB", "DATA/OBJ/OBJ002.IGB", "DATA/OBJ/OBJ014.IGB", "DATA/OBJ/OBJ015.IGB"};

Vec3f toF(const Vec3& v) { return {v.x, v.y, v.z}; }
Vec3 toV(const Vec3f& v) { return {v[0], v[1], v[2]}; }
int classRow(const Item& it) { return it.cls >= 0 && it.cls < kItemClassCountAll ? it.cls : 0; }

Item* gLastInit = nullptr;   // the item of the last itemEntityInit (ItemHooks::create)

// the task-list record standing for `it` (there is at most one: link() revives a record that is still listed)
Chr* recordOf(const Item* it) {
    if (it == nullptr) return nullptr;
    for (Chr* c : characters().list())
        if (c->item == it) return c;
    return nullptr;
}

// FUN_00142e10(0x499490[cls], phys, item) on the physics object FUN_0012dcc0 -> FUN_00142d50 (body 0x4cdb90): position
// = transform +0xc, move = +0x70, radius and centre (0, r, 0). The shape (FUN_001432c0) has no owner (+0 = 0), so
// FUN_00142e60 skips the Lethargy scaling and FUN_00142290 never pushes the item.
void bodyInit(Chr& c, const Item& it) {
    const float r = kItemClassPhysics[classRow(it)].radius;
    c.shape = Shape{};
    physInit(c.phys, &c.xf.m.pos, &c.shape, r);
    c.phys.ownerIsCharacter = false;
}

// item transform -> record (Chr::xf is the item's matrix; the model is drawn at the last FUN_00109d40 position)
void writeRecord(Chr& c, const Item& it) {
    c.id = it.id;
    c.xf.m.pos = toF(it.pos);
    c.xf.m.right = toF(it.right);
    c.xf.m.up = toF(it.up);
    c.xf.m.fwd = toF(it.fwd);
    c.xf.scale = {it.scale, it.scale, it.scale};
    const Item::ModelXf& m = it.modelXf;
    c.jitter = m.valid && (m.pos.x != it.pos.x || m.pos.y != it.pos.y || m.pos.z != it.pos.z);
    if (c.jitter) c.jitterPos = toF(m.pos);
}

void releaseRecordModel(Chr& c) {
    c.model.parts.clear();
    c.model.clips.clear();
    c.model.pose.clear();
    c.model.flags2c &= ~0x80000000u;
}

// FUN_0012db80 + FUN_001099e0(holder, 0x464150[cls], 1, 0x44000001): the class model (synchronous in the port)
void loadRecordModel(Chr& c, Item& it) {
    releaseRecordModel(c);
    it.modelXf.publishes = 0;   // publishes of the previous holder (none: no fader on a new model)
    c.model.flags2c |= 0x44000001u;
    ChrModelPart part;
    if (!chrLoadPart(part, characters().assetRoot / kItemModelPath[classRow(it)], "")) return;
    c.model.parts.push_back(std::move(part));
    if (const alchemy::Skeleton* sk = &c.model.parts[0].model->skeleton(); sk != nullptr && sk->valid) {
        std::vector<std::string> log;
        c.model.clips = alchemy::loadClips(*c.model.parts[0].file, alchemy::primarySkeleton(*c.model.parts[0].file), &log);
        c.model.anim.init(&c.model.clips, c.model.skeleton());
    }
    c.model.lit = true;               // mode 1: lit branch; 0x4000000: no own light
    c.model.flags2c |= 0x80000000u;   // FUN_00109c80 load callback
}

// FUN_002073e0(item, 0x50b134, 0x50b138) for an item without a record: a new record appended to the task list
Chr* newRecord(Item& it) {
    auto owned = std::make_unique<Chr>();
    Chr* c = owned.get();
    c->type = kChrItem;
    c->item = &it;
    c->flags24 = 0;
    bodyInit(*c, it);
    writeRecord(*c, it);
    c->prevPos = c->xf.m.pos;
    characters().adopt(std::move(owned));
    characters().link(c);
    c->taskFlags = 1u;
    it.serial = c->serial;
    if (it.hasModel) loadRecordModel(*c, it);
    return c;
}

struct ItemHooks final : ItemEntityHooks {
    Item* create(uint32_t id, bool link) override {
        gLastInit = nullptr;
        Chr* c = characters().createTag(id, link);   // FUN_0012ce70 -> itemEntityConstruct / itemEntityInit
        Item* it = c != nullptr ? gLastInit : nullptr;
        gLastInit = nullptr;
        return it;
    }
    void link(Item& it) override {
        if (Chr* c = recordOf(&it)) {
            c->taskFlags = (c->taskFlags & 0xcfffffffu) | 1u;   // still listed (0x10000000): revived in place
            return;
        }
        newRecord(it);
    }
    void taskFlagsChanged(Item& it) override {
        if (Chr* c = recordOf(&it)) c->taskFlags |= it.taskFlags & 0x30000000u;
    }
    void loadModel(Item& it) override {
        if (Chr* c = recordOf(&it)) loadRecordModel(*c, it);
    }
    void releaseModel(Item& it) override {
        if (Chr* c = recordOf(&it)) releaseRecordModel(*c);
    }
    void destroyed(Item& it) override {
        if (Chr* c = recordOf(&it)) {
            c->taskFlags |= 0x30000000u;
            releaseRecordModel(*c);
        }
    }
    void referenced(std::vector<const Item*>& out) override {
        for (Chr* c : characters().list()) {
            if (c->item == nullptr) continue;
            out.push_back(c->item);
            // remove bits written on the record outside the item code (ChrManager::dropFarthestItem, FUN_00207350)
            if ((c->taskFlags & 0x10000000u) != 0) c->item->taskFlags |= c->taskFlags & 0x30000000u;
        }
    }
    bool damageWall(const void* face) const override { return chrDamageWall(static_cast<const CollFace*>(face)); }
};

ItemHooks gHooks;

// CollisionQuery of the game over the record's physics object, the CLS ray and the hit manager 0x50c780
struct EntityCollision final : CollisionQuery {
    explicit EntityCollision(Chr& c) : chr(c) {}
    Chr& chr;

    // orig FUN_00142e60 on item +0x30 (the item's fields mirror the physics object around the call)
    uint32_t moveAndCollide(Item& it) override {
        Phys& p = chr.phys;
        const float r = kItemClassPhysics[classRow(it)].radius;   // FUN_00142e10 radius of the current class
        chr.xf.m.pos = toF(it.pos);
        p.pos = &chr.xf.m.pos;
        p.move = &p.vel;
        p.shape = &chr.shape;
        p.radius = r;
        p.centreOffset = {0.0f, r, 0.0f};
        p.moveVel = toF(it.moveVel);
        p.gravity = it.gravityScale;
        p.flags = (p.flags & ~3u) | (it.physFlags & 3u);
        p.vy = it.vy;
        p.ownerIsCharacter = false;
        const uint32_t res = physTick(p);
        it.pos = toV(chr.xf.m.pos);
        it.moveVel = toV(p.moveVel);
        it.physFlags = p.flags;
        it.vy = p.vy;
        const GroundInfo& g = p.ground;
        it.onFloor = g.nWalls != 0;
        it.floorAttr = g.nWalls != 0 ? g.walls[0] : nullptr;
        if (g.nWalls != 0 && g.walls[0] != nullptr) it.wallNormal = toV(g.walls[0]->normal);
        it.hasFloorNormal = g.nFloors != 0 && g.floors[0] != nullptr;
        if (it.hasFloorNormal) it.floorNormal = toV(g.floors[0]->normal);
        return res;
    }
    // orig FUN_00143150(from, (0, -maxDist, 0), 1.0) + FUN_001431e0 (radius 0x463d38 / 0x463d28 = 1.0 at both callers)
    bool raycastDown(const Vec3& from, float maxDist, float& hitDist) override {
        const float f[3] = {from.x, from.y, from.z};
        const float d[3] = {0.0f, -maxDist, 0.0f};
        float dist = 0.0f;
        if (!raycast(f, d, 1.0f, &dist)) return false;
        hitDist = dist;
        return true;
    }
    // orig FUN_0014ad00(R, power, 0x50c780, item, 0, pos, m1, m2, m3, weaponTag, exclude) + FUN_0014df50(0x50c780, item)
    int hitTest(Item& it, float radius, float power, uint32_t m1, uint32_t m2, uint32_t m3, uint32_t weaponTag,
                CharacterAccess* exclude, CharacterAccess** out, int maxOut) override {
        const ChrAdapter* ex = dynamic_cast<const ChrAdapter*>(exclude);
        HitManager& hm = hitManager();
        if (!hm.testPoint(radius, power, &chr, 0, toF(it.pos), m1, m2, m3, weaponTag, ex != nullptr ? ex->chr() : nullptr))
            return -1;
        int n = 0;
        for (Chr* v : hm.victimsHitBy(&chr)) {
            if (n >= maxOut) break;
            out[n++] = &chrAdapter(v);
        }
        return n;
    }
    Vec3 worldUp() const override { return {0.0f, 1.0f, 0.0f}; }   // FUN_00206f10
};

// ThrowLock release (FUN_001ce200 -> FUN_001d5640(item, ACT +0x50, aim direction, state owner)) when the character
// module has not installed its own. UNVERIFIED: ACT player +0x50 (the point an ACT command FUN_00148350 computes from a
// bone matrix plus an offset) is not modelled by ActRunner; the world position of the "Right_WEAPON00_Attach" joint
// (string 0x463cf0, the full name) the
// ThrowLock state attaches the item to stands in for it (player position when the joint is missing), and the thrower
// is the player (the only ThrowLock user in the port).
Vec3 throwPoint(Chr& c) {
    const alchemy::Skeleton* sk = c.model.skeleton();
    const std::vector<alchemy::Mat4>& pose = chrTickPose(c);
    if (sk != nullptr) {
        for (size_t b = 0; b < sk->bones.size() && b < pose.size(); ++b) {
            if (sk->bones[b].name != "Right_WEAPON00_Attach") continue;   // 0x463cf0
            const alchemy::Mat4 w = alchemy::mul(pose[b], chrActorMatrix(c));
            return {w.m[12], w.m[13], w.m[14]};
        }
    }
    return toV(c.xf.m.pos);
}

bool install() {
    itemEntityHooks() = &gHooks;
    ThrowHooks& th = throwHooks();
    if (!th.throwItem) {
        th.throwItem = [](const void* item, const Vec3f& dir) {
            Chr* p = characters().player();
            if (p == nullptr || item == nullptr) return;
            throwHeldItem(*const_cast<Item*>(static_cast<const Item*>(item)), throwPoint(*p), toV(dir), chrAdapter(p),
                          itemSystem());
        };
    }
    return true;
}
const bool gInstalled = install();

}  // namespace

// orig FUN_0012e810 (work/decompiled/0012e810.c): FUN_00132b00 storage, FUN_001d3960 ctor (actor base FUN_00128590:
// transform scale 1 / axes reset; +0x2c..+0x70, +0x88 zero; state slot FUN_001db660), vtable 0x4cd960
std::unique_ptr<Chr> itemEntityConstruct(uint32_t tag) {
    (void)gInstalled;
    // the walk-side release of items whose record is gone (e.g. FUN_0012bfd0 at the floor exit deletes the linked items
    // at once, so the model count 0x53d8f0 and the list 0x50a830 are clean before the next floor creates its items)
    gameAdapter().purgeItems();
    Item* it = gameAdapter().allocItem();
    it->id = tag;
    auto c = std::make_unique<Chr>();
    c->item = it;
    c->type = kChrItem;
    c->id = tag;
    c->flags24 = 0;
    return c;
}

// vt+0x1c = orig FUN_001d3d40 (work/decompiled/001d3d40.c) through FUN_00128690, then FUN_00128160 (list 0x50a830).
// createTag has linked the record when addToList == 1 (FUN_002073e0 sets the task bit 0); an unlinked item keeps no
// record (the factory's record is retired at once).
void itemEntityInit(Chr& c) {
    Item* it = c.item;
    if (it == nullptr) return;
    const auto& list = characters().list();
    const bool linked = std::find(list.begin(), list.end(), &c) != list.end();
    it->taskFlags = linked ? 1u : 0u;
    it->type = kChrItem;   // FUN_00128690: +0x18 = 3, +0x1c = id
    it->id = c.id;
    it->serial = c.serial;
    bodyInit(c, *it);
    initItemById(*it, itemSystem());   // FUN_001d3d40 (FUN_001d3e30 class init; FUN_001d3a30 model when linked)
    bodyInit(c, *it);                  // radius of the final class (FUN_001d3e30 tail)
    writeRecord(c, *it);
    c.prevPos = c.xf.m.pos;
    gameAdapter().registerItem(it);    // FUN_00128160
    gLastInit = it;
    if (!linked) {
        c.item = nullptr;
        c.taskFlags = 0x10000000u;
    }
}

// vt+0xc = orig FUN_001d5180 (work/decompiled/001d5180.c) = worldItemTick; the record follows the item's transform
void itemEntityTick(Chr& c) {
    Item* it = c.item;
    if (it == nullptr || (it->taskFlags & 0x10000000u) != 0) return;
    alchemy::ps2::Ps2FloatScope fp;
    EntityCollision cq(c);
    worldItemTick(*it, itemSystem(), cq);
    if (c.item == it) {
        writeRecord(c, *it);   // a destroyed item stays allocated while this record references it
        // every FUN_00109d40 on the holder (+0x2c bit 31 set) steps its material fader first (FUN_0010a160)
        for (; it->modelXf.publishes != 0; --it->modelXf.publishes)
            if (c.model.loaded()) modelPublishFx(c.model);
    }
}

Chr* itemEntityOf(const Item* item) { return recordOf(item); }

// the spawn functions' transform writes (entity hooks setPos / resetAxes / rotate) made on the record
void itemEntitySyncFromChr(Chr& c) {
    Item* it = c.item;
    if (it == nullptr) return;
    it->pos = toV(c.xf.m.pos);
    it->right = toV(c.xf.m.right);
    it->up = toV(c.xf.m.up);
    it->fwd = toV(c.xf.m.fwd);
}

// orig FUN_001d3b30 (work/decompiled/001d3b30.c)
void itemEntityUnlink(Item& it) { gameAdapter().itemLeaveWorld(it); }

// orig FUN_001d5620 (work/decompiled/001d5620.c) on an item entity: what the floor spawn routines FUN_00120a70 /
// FUN_00120f70 / FUN_00121c50 / FUN_00121f80 / FUN_001229d0 call after writing the transform of an item tag
void itemEntityPlace(Chr& c) {
    if (c.item == nullptr) return;
    itemEntitySyncFromChr(c);
    placeItem(*c.item, itemSystem());
    writeRecord(c, *c.item);
    c.prevPos = c.xf.m.pos;
}

}  // namespace baroque::game
