#pragma once
// Item instance (class vtable 0x4cd960, ITEMS_HUD_MENUS.md 1), initialisation by id (FUN_001d3d40 / FUN_001d3e30
// and the per-class inits), identification / curse / variant rolls, display name, save record (FUN_001d5a50 /
// FUN_001d5bc0). Field comments give the original offsets.
#include "data/prm.hpp"
#include "game/items/char_access.hpp"
#include "game/rng.hpp"
#include <cstdint>
#include <string>

namespace baroque::game {

// 4-char id as the game compares it: the little-endian word of the characters ("SD00" -> 0x30304453).
constexpr uint32_t itemId(const char (&s)[5]) {
    return static_cast<uint32_t>(static_cast<uint8_t>(s[0])) | (static_cast<uint32_t>(static_cast<uint8_t>(s[1])) << 8) |
           (static_cast<uint32_t>(static_cast<uint8_t>(s[2])) << 16) | (static_cast<uint32_t>(static_cast<uint8_t>(s[3])) << 24);
}
uint32_t makeItemId(int cls, int idx);           // sprintf("%s%02d", prefix[cls], idx) (0x4641a0)
std::string itemIdString(uint32_t id);
int itemClassOf(uint32_t id);                   // orig FUN_001d2bf0: prefix index 0..14, 0xf when unknown
bool isItemId(uint32_t id);                     // orig FUN_001d38f0
int itemIndexOf(uint32_t id);                   // orig FUN_001d2c60: atoi of the two digits

// Item classes (prefix table 0x463eb0): 0 SD 1 CT 2 WG 3 HT 4 MT 5 BX 6 LQ 7 ST 8 TL 9 TP 10 BN 11 WM 12 GN 13 IS,
// 14 = flesh ("CC" prefix, creature id in idx).
enum : int { kClsSD = 0, kClsCT, kClsWG, kClsHT, kClsMT, kClsBX, kClsLQ, kClsST, kClsTL, kClsTP, kClsBN, kClsWM, kClsGN, kClsIS, kClsFlesh = 0xe, kClsNone = 0xf };
inline constexpr int kItemClassCountAll = 15;
extern const char* const kItemClassPrefix[kItemClassCountAll];   // 0x463eb0

// Item flags +0x70
enum : uint32_t {
    kItemIdentified = 1,
    kItemSealed = 2,            // "Prevent deterioration"
    kItemRotten = 4,            // HT/MT
    kItemCursedSealed = 8,      // '!' effect slot: cannot unequip
    kItemReversed = 0x10,       // face-down disk / reversed torturer
    kItemDisarmed = 0x20,       // box trap disarmed
    kItemInFlight = 0x1000000,
    kItemThrownBack = 0x2000000,
    kItemPlacedA = 0x40000000,
    kItemPlacedB = 0x80000000,
};

// Table 0x499490 {radius, gravity scale when thrown, speed decel per tick} per class (15 rows) [V data].
struct ItemClassPhysics { float radius, gravityScale, decel; };
extern const ItemClassPhysics kItemClassPhysics[kItemClassCountAll];

// State-machine slot of the world item (+0x78) plus the per-state object fields (see item_world.hpp).
struct ItemStateSlot {
    int current = 0;            // +0x4 (active state object: ItemState value, 0 = none)
    int pending = 0;            // +0x18 (requested factory: ItemState value, -1 = clear)
    int factory = 0;            // +0x14
    int param0 = 0, param1 = 0; // +0xc / +0x10
    int lastFactory = 0;        // +0x1c
    // lying / settled state object (0x4cee60 / LAB_001d5400): +8 flags (bit0 pickup message shown, bit1 ST16 reveal),
    // +0xc actor standing on the disk, +0x10 its serial, +0x14 ring effect, +0x18 rest height, +0x1c crystal ring,
    // +0x20 flesh timer
    float enterY = 0;           // lying +8 (height at enter)
    uint32_t lflags = 0;        // settled +8
    CharacterAccess* standing = nullptr;  // +0xc
    uint32_t standingSerial = 0;          // +0x10
    int ringEffect = -1;        // +0x14
    float restY = 0;            // +0x18
    int crystalRing = -1;       // +0x1c
    int fleshTimer = 0;         // +0x20
    // thrown state object (0x4cee40): +8 thrower, +0xc thrower id, +0x10 thrower serial, +0x14 speed, +0x18 decel,
    // +0x1c dir, +0x28 TH value
    float speed = 0, decel = 0, thv = 0;
    Vec3 dir;
};

struct Item {
    uint32_t taskFlags = 0;     // +0x10 (bit0 active in world, 0x10000000 removed)
    int type = 3;               // +0x18 (3 item, 5 trap / field object)
    uint32_t id = 0;            // +0x1c
    Vec3 pos;                   // xform +0xc
    Vec3 right{1, 0, 0};        // xform +0x18 (matrix row 0, = up x fwd)
    Vec3 up{0, 1, 0};           // xform +0x24 (matrix row 1)
    Vec3 fwd{0, 0, 1};          // xform +0x30 (matrix row 2)
    float scale = 1.0f;         // xform +0 (flesh shrink, FUN_001ced50)
    int cls = kClsNone;         // +0x24
    int idx = 0;                // +0x28 (PRM row; flesh: creature id)
    bool hasModel = false;      // +0x2c (OBJ%03d.IGB node present)
    Item* contained = nullptr;  // +0x34 (Hungry Chest meal, BX content)
    Item* parasite = nullptr;   // +0x38
    uint32_t savedId = 0;       // +0x3c
    uint32_t brand = 0;         // +0x40 ("ST10" = cursed)
    uint32_t crystalId = 0;     // +0x44
    uint32_t crystalFlags = 0;  // +0x48
    int hpMaxBonus = 0;         // +0x4c
    int vtMaxBonus = 0;         // +0x50
    int atkBase = 0;            // +0x54 (SD base ATK / WG01 bonus)
    int defBase = 0;            // +0x58 (CT base DEF / WG02 bonus)
    int wg03Bonus = 0;          // +0x5c
    int atkMod = 0;             // +0x60
    int defMod = 0;             // +0x64
    int unused68 = 0;           // +0x68 (saved)
    int count = 0;              // +0x6c (stack / uses / charges / value)
    uint32_t flags = 0;         // +0x70
    ItemStateSlot state;        // +0x78
    Vec3 velocity;              // +0x7c (launch velocity)
    CharacterAccess* thrower = nullptr;  // +0x88
    uint32_t throwerId = 0;     // +0x8c (thrower +0x1c)
    uint32_t throwerSerial = 0; // +0x90 (thrower +0xc actor serial)
    uint32_t serial = 0;        // +0xc actor serial (item side)
    bool shadow = true;         // model shadow flag (FUN_00109f70)
    // physics object (+0x30 -> vtable 0x4cdb90 body, FUN_00142d50; radius / centre (0, r, 0) by FUN_00142e10). The
    // game keeps the Phys itself on the task-list entity (game/objects/item_entity.cpp) and mirrors these fields
    // around every FUN_00142e60 (CollisionQuery::moveAndCollide).
    Vec3 moveVel;               // phys +0x7c
    float gravityScale = 1.0f;  // phys +0x50
    uint32_t physFlags = 0;     // phys +0x4c (bit0 airborne)
    float vy = 0;               // phys +0x54 (fall speed)
    bool onFloor = false;       // GroundInfo (phys +0x58) +0: contact face count != 0 (walls / steep faces)
    const void* floorAttr = nullptr;  // GroundInfo +4: first contact face (CollFace*, FUN_00140050 argument)
    Vec3 wallNormal{0, 1, 0};   // GroundInfo +4 -> +0xc (normal of that face)
    bool hasFloorNormal = false;// GroundInfo +0x14: floor face count != 0
    Vec3 floorNormal{0, 1, 0};  // GroundInfo +0x18 -> +0xc
    // model node matrix as last published by FUN_00109d40(+0x2c, pos, scale, fwd, up, right) (FUN_001cec60 publishes
    // the settled item raised along its up vector by kItemRestOffset)
    struct ModelXf {
        Vec3 pos, right{1, 0, 0}, up{0, 1, 0}, fwd{0, 0, 1};
        float scale = 1.0f;
        bool valid = false;
        // PORT: FUN_00109d40 calls since the item entity's last tick; each one starts with the material fader step
        // FUN_0010a160 of the holder, which the record (item_entity.cpp) performs on its model
        uint32_t publishes = 0;
    } modelXf;

    bool removed() const { return (taskFlags & 0x10000000u) != 0; }
    bool identified() const { return (flags & kItemIdentified) != 0; }
};

// Everything the item code needs from its surroundings. One instance per game.
struct ItemSystem {
    const data::PrmSet* prm = nullptr;   // PRMITEM00..13 (iRam0053d860.. file objects)
    Rng* rng = nullptr;                  // 0x53eb20
    GameAccess* game = nullptr;
    struct Inventory* inventory = nullptr;   // 0x53d910
    struct CollectorBag* bag = nullptr;      // 0x53dc00
    uint32_t rand(uint32_t n) const { return rng->rand(n); }
    const data::PrmItem* row(int cls, int idx) const;            // orig FUN_001d2c90 (nullptr past the table)
    const data::PrmItem* row(const Item& it) const { return row(it.cls, it.idx); }
    int32_t word(int cls, int idx, uint32_t offset) const;       // raw record word at +offset (class stride checked)
    int32_t word(const Item& it, uint32_t offset) const { return word(it.cls, it.idx, offset); }
    int rowCount(int cls) const;                                 // orig FUN_001d2ba0
    const char* name(int cls, int idx) const;                    // PRM +4 (Shift-JIS bytes)
    const char* desc(int cls, int idx) const;                    // PRM +8
};

// Floor band for the PRM per-band columns: orig FUN_001d37d0 / FUN_001d3840 (floor <5 -> 0, <13 -> 1, <22 -> 2, else 3).
int floorBand(const GameAccess& game);
int32_t dropWeight(const ItemSystem& sys, const data::PrmItem& row);      // orig FUN_001d37d0 (+0xc[band])
int32_t identifyChance(const ItemSystem& sys, const data::PrmItem& row);  // orig FUN_001d3840 (+0x1c[band])
int32_t rollVariant(ItemSystem& sys, const data::PrmItem& row);           // orig FUN_001d38b0 (+0x2c[rand(15)])

// orig FUN_001d3d40 (vtable +0x1c init by id): "WM22" dead parasite rerolls to WM01..WM07 then dies; then
// FUN_001d3e30. `item.id` must be set.
void initItemById(Item& item, ItemSystem& sys);
// orig FUN_001d3e30: class / index / identification roll / curse roll / per-class init / physics radius.
void initItemClass(Item& item, ItemSystem& sys);
void rollIdentified(Item& item, ItemSystem& sys);     // orig FUN_001d3f90
void rollCurse(Item& item, ItemSystem& sys);          // orig FUN_001d4030
// orig FUN_001d4860: change the id keeping the identified flag and the model.
void changeItemId(Item& item, uint32_t newId, ItemSystem& sys);
void destructItem(Item& item, ItemSystem& sys);       // orig FUN_001d3c70 (destructor body)
void clearItemFields(Item& item);                     // orig FUN_001d3c20
void parasiteDie(Item& item);                         // orig FUN_001d4aa0
void parasiteRevive(Item& item);                      // orig FUN_001d4af0
// orig FUN_001d4b40: cannibal `eater` eats `victim` (level +1, +2 when the victim carried a parasite); true when it
// grew into the Guardian "WM06" (with a dead WM07 attached).
bool cannibalEat(Item& eater, const Item& victim, ItemSystem& sys);
bool canFuseCrystal(const Item& sword);               // orig FUN_001d4a20 (SD with idx > 0x1c)
uint32_t crystalOfSword(uint32_t swordId);            // orig FUN_001d4a50 (table 0x499340, 0 = none)
uint32_t swordOfCrystal(uint32_t crystalId);          // the other half of table 0x499340 (0 = none)
bool addAtkMod(Item& item, int delta);                // orig FUN_001d4f70 (base+mod clamped to [-99, 99])
bool addDefMod(Item& item, int delta);                // orig FUN_001d5030
// orig FUN_001d3ba0: unidentified -> class name string id 0x1b8+cls; identified -> PRM name; a WM host with an
// attached parasite composes "<parasite> <host>" (FUN_0015a410, buffer 0x5180a0).
const char* itemDisplayName(const Item& item, const ItemSystem& sys);
bool isParasiteHostWithParasite(const Item& item);    // orig FUN_0015a380
// bounded forms of the libc string calls the item code uses (FUN_0023f140 strcat, FUN_0023fca0 strncpy)
void appendStr(char* dst, size_t cap, const char* src);
void copyPrefix(char* dst, size_t cap, const char* src, size_t n);
const char* composedParasiteName(const Item& item, const ItemSystem& sys);  // orig FUN_0015a410

// Save record: 0x198 bytes = 6 x 0x44-byte sub-records {id, cls, idx, +0x3c..+0x70}: item, contained, contained's
// parasite, that one's parasite, parasite, parasite's parasite [V].
struct ItemSaveRecord {
    uint32_t w[0x66] = {};
};
void writeItemRecord(const Item* item, ItemSaveRecord& out);                 // orig FUN_001d5a50 (nullptr = zero, FUN_00116560)
void writeItemSubRecord(const Item& item, uint32_t* out17);                  // orig FUN_001d5b30
void readItemSubRecord(Item& item, const uint32_t* in17);                    // orig FUN_001d5d20
Item* readItemRecord(const ItemSaveRecord& rec, ItemSystem& sys);            // orig FUN_001d5bc0 (nullptr when empty)

}  // namespace baroque::game
