#pragma once
// World items (ITEMS_HUD_MENUS.md 3-4): the item state machine (lying / thrown / generic object / settled), throw
// launch and speeds, bounce and landing, BN02 homing, pickup, disk stepping, traps / field objects, NPC hand-over
// and the menu Throw / Place / Give player actions. Physics against the CLS goes through CollisionQuery (the
// character-physics module implements it; the items check uses a flat plane).
#include "game/items/inventory.hpp"

namespace baroque::game {

// FUN_001416b0 result bits (PLAYER.md 5)
enum : uint32_t { kContactWall = 1, kContactActor = 2, kContactLanded = 0x10, kContactSnapped = 0x20 };

struct CollisionQuery {
    virtual ~CollisionQuery() = default;
    // FUN_00142e60 on the item body: adds gravity when airborne (vy += gravityScale*3, max 100, vel.y -= vy),
    // sweeps `item.moveVel` against the CLS, updates pos / onFloor / hasFloorNormal / floorNormal / floorAttr,
    // clears the airborne flag on landing. Returns the kContact* mask.
    virtual uint32_t moveAndCollide(Item& item) = 0;
    // FUN_00143150(from, (0,-1000,0)) + FUN_001431e0: downward probe; true with the hit distance
    virtual bool raycastDown(const Vec3& from, float maxDist, float& hitDist) = 0;
    // FUN_0014ad00(radius, power, 0x50c780, item, 0, pos, m1, m2, m3, weaponTag, exclude) + FUN_0014df50(0x50c780,
    // item): the item sphere against the hurt volumes (each victim at most once per registration); fills `out` with the
    // characters the item hit, returns their count (-1 when FUN_0014ad00 returned 0)
    virtual int hitTest(Item& item, float radius, float power, uint32_t m1, uint32_t m2, uint32_t m3, uint32_t weaponTag,
                        CharacterAccess* exclude, CharacterAccess** out, int maxOut) = 0;
    virtual Vec3 worldUp() const = 0;   // FUN_00206f10
};

enum class ItemState : int {
    None = 0,
    Lying = 0x1db750,     // factory 0x1db750 -> vtable 0x4cee60 (FUN_001cfd90 enter, FUN_001cfea0 update)
    Thrown = 0x1db7e0,    // factory 0x1db7e0 -> 0x4cee40 (FUN_001cffc0 / FUN_001d0370)
    Generic = 0x1db870,   // factory 0x1db870 -> 0x4cee20 (FUN_001d1450 / FUN_001d1480)
    Settled = 0x1d5400,   // LAB_001d5400 -> factory 0x1db6c0 -> 0x4cee80 (FUN_001ceae0 enter, FUN_001ced50 update, FUN_001cec10 exit)
    Dropped = 0x1e2a70,   // LAB_001e2a70 -> factory 0x1e34c0 -> 0x4ceef0 (FUN_001d15f0 enter, FUN_001d17c0 update)
};

// The "if no state and no pending, or param0 < 1: pending = factory" idiom used everywhere.
void itemRequestState(Item& item, ItemState state);
void worldItemTick(Item& item, ItemSystem& sys, CollisionQuery& cq);     // orig FUN_001d5180 (vtable +0xc)

// 0x499270: per-class rest offset along the up vector while lying [V floats]
extern const float kItemRestOffset[kItemClassCountAll];

void dropItemAtActor(Item& item, float yawDeg, CharacterAccess* actor, ItemSystem& sys);   // orig FUN_001d5410 (yaw < 0 = random)
void placeItem(Item& item, ItemSystem& sys);                                                // orig FUN_001d5620 (flag 0x80000000 + FUN_001d5410(-1))
// orig FUN_001d5640: throw the held item (player: counter 10, BN stacks split one off, item leaves the inventory)
void throwHeldItem(Item& item, const Vec3& pos, const Vec3& dir, CharacterAccess& thrower, ItemSystem& sys);
void launchItem(Item& item, const Vec3& pos, const Vec3& dir, CharacterAccess* thrower);   // orig FUN_001d5780
float throwSpeed(ItemSystem& sys, bool byPlayer);                                           // orig FUN_001d5850
bool tossFromMenu(Item& item, const Vec3& pos, const Vec3& vel, ItemSystem& sys);           // orig FUN_001d5920
void tossItem(Item& item, const Vec3& pos, const Vec3& vel);                                // orig FUN_001d5990
void setDiskFace(Item& item, ItemSystem& sys);                                              // orig FUN_001d5250
void setDiskFace(Item& item, ItemSystem& sys, CollisionQuery& cq);
void updateDiskFace(Item& item);                                                            // orig FUN_001d4db0
void updateDiskFace(Item& item, CollisionQuery& cq);
void settleItem(Item& item, CollisionQuery& cq);                                            // orig FUN_001d52f0
void bounceItem(Item& item, ItemSystem& sys);                                               // orig FUN_001cea30
bool pickupTest(Item& item, bool quiet, ItemSystem& sys);                                   // orig FUN_001ce7e0
void obtainedItem(Item& item, ItemSystem& sys);                                             // orig FUN_001ce730
int cubitValue(ItemSystem& sys);                                                            // orig FUN_001d0220 (value only)
void printCubits(ItemSystem& sys);                                                          // orig FUN_001d0220
bool diskSpend(Item& item, ItemSystem& sys);                                                // orig FUN_001d4c90
bool npcAcceptsItem(CharacterAccess& npc, ItemSystem& sys);                                 // orig FUN_001d9040
bool giveItemToNpc(Item& item, CharacterAccess& npc, ItemSystem& sys);                      // orig FUN_001d91a0
// orig FUN_001d50f0: PRM +0x70 of the item | the WM +0x70 of its parasite and of the parasite's parasite
// (CombatHooks::heldElementMask = this on the coat word iRam0053d9bc = inventory equip[2], 0 when empty)
uint32_t itemElementMask(ItemSystem& sys, const Item& item);
// FUN_001b7e50 (receptacle objects O??A taking a thrown item) is translated by the object module:
// game/objects/objects_classes.cpp oaHandOver.

// Menu actions = player states 0x4cede0 (Throw, "TH00"), 0x4cee00 (Place, "TH06"), 0x4cedc0 (Give, "TH07").
struct MenuItemAction {
    enum Kind { None, Throw, Place, Give };
    Kind kind = None;
    bool heads = true;
    bool canThrow(ItemSystem& sys, int idx) const;     // orig FUN_001d8cc0
    bool canPlace(ItemSystem& sys, int idx) const;     // orig FUN_001d8bb0
    bool canGive(ItemSystem& sys, int idx);            // orig FUN_001d8e40 (also finds the target)
    bool beginThrow(ItemSystem& sys, int idx);         // orig FUN_001d8c10
    bool beginPlace(ItemSystem& sys, int idx, bool heads);   // orig FUN_001d8a40
    bool beginGive(ItemSystem& sys, int idx);          // orig FUN_001d8d60
    // per tick while the player action runs: FUN_001ce520 turns toward the target; when the ACT's 0x100000
    // trigger fires (`trigger`) the exit functions FUN_001ce2f0 / FUN_001ce3f0 / FUN_001ce520 release the item.
    void tick(ItemSystem& sys, bool trigger, const Vec3& handPos);
    CharacterAccess* giveTarget(ItemSystem& sys) const;   // FUN_0012c520(radius, 30deg, list, player, 1, 0)
};

// Trap / field object (type 5, FUN_001e5610 -> FUN_001e57d0)
struct TrapObject {
    Vec3 pos;                       // xform +0xc
    float radius = -1.0f;           // +0x24 (200 kind 0, 600 kind 1 / unroomed kind 2, -1 room / everywhere)
    int room = -1;                  // +0x28
    uint32_t power = 0;             // +0x2c (element bits for the burst effects / damage power)
    uint32_t statusMask = 0;        // +0x30
    uint32_t reactMask = 0;         // +0x34
    int effectId = 0;               // +0x38 (damage kind; 0 = status only)
    CharacterAccess* owner = nullptr;  // +0x3c
    uint32_t tag = 0;               // +0x40 ("TLxx"/"TPxx"/0)
    int ownerHit = 0;               // +0x44 (0 = the owner is never hit)
};
void trapInit(TrapObject& t, const Vec3& pos, int kind, uint32_t power, uint32_t statusMask, uint32_t reactMask,
              int effectId, CharacterAccess* owner, uint32_t tag, ItemSystem& sys);          // orig FUN_001e57d0
void trapApply(TrapObject& t, ItemSystem& sys);                                             // orig FUN_001e5ca0
bool trapHitsCharacter(const TrapObject& t, CharacterAccess& c, ItemSystem& sys);           // orig FUN_001e5e70
void trapApplyCharacter(TrapObject& t, CharacterAccess& c, ItemSystem& sys);                // orig FUN_001e6190
bool trapHitsItem(const TrapObject& t, const Item& it, ItemSystem& sys);                    // orig FUN_001e6070
void trapApplyItem(TrapObject& t, Item& it, ItemSystem& sys);                               // orig FUN_001e6410

// Aim arc (FUN_0013a150): 30 segments as 60 vertices (pairs across `side`) and 60 colour words; `phase` and
// `angle` are the statics 0x44b118 / 0x44b110.
void aimArc(Item& item, const Vec3& start, const Vec3& dir, const Vec3& side, ItemSystem& sys, Vec3 verts[60],
            uint32_t colours[60], float& phase, float& angle);

}  // namespace baroque::game
