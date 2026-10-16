#pragma once
// Dungeon objects: the J/O entries of the factory table 0x44aa10 and the 4th-letter classes of FUN_0012ce70 (type 4,
// object list 0x50b000), the type-5 helper objects they spawn, and the item entities of the task list (type 3, class
// 0x4cd960, item list 0x50a830). All of them live in the character task list 0x50b134 (characters().list()) as `Chr`
// records with `obj` / `item` set; chrUpdate / chrDraw dispatch to the functions below (the vtable +0xc / +0x1c of
// the original classes). Report: work/reports/OBJECTS_NATIVE.md.
//
// Object fields are kept by their original byte offset (ObjData::I / F / P): the class translations read like the
// pseudocode (`o.I(0x28)` = `*(int *)(obj + 0x28)`); each class documents its fields next to its functions.
#include "game/character/character.hpp"

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace baroque::game {

// Object classes. Comments: factory wrapper -> final vtable, ctor / init (vt+0x1c) / tick (vt+0xc) / class id (the
// last vtable slot's return value, compared by FUN_00140250).
enum class ObjClass : uint8_t {
    None,
    OA,     // O??A           FUN_0012e8f0 -> 0x4cd8f0, ctor FUN_001b7680, init FUN_001b7740, tick FUN_001b77a0, id 0x132bb0
    OC,     // O??C n !in 70..92  FUN_0012e960 -> 0x4cd8c0, ctor FUN_001b9ac0, init FUN_001b9bc0, tick FUN_001b9ca0, 0x132bc0
    OC70,   // O??C n in 70..92   FUN_0012e9d0 -> 0x4cd890, ctor FUN_001bf980, tick FUN_001bfa30, 0x132bd0
    OD,     // O??D           FUN_0012ea40 -> 0x4cd830 (base FUN_0012ae60; tick / init nop), 0x132be0
    OE,     // O??E           FUN_0012eb40 -> 0x4cd800, ctor FUN_001bbfc0, init FUN_001bc100, tick FUN_001bc170, 0x132bf0
    OFGI,   // O??F/G/I       FUN_0012ebb0 -> 0x4cd7a0 (base FUN_0012ae60; tick / init nop), 0x132c00
    OH,     // O??H           FUN_0012ecb0 -> 0x4cd770, ctor FUN_001bc9e0, tick FUN_001bcf40, 0x132c10
    OK,     // O??K           FUN_0012ed20 -> 0x4cd740, ctor FUN_001bd090, init nop FUN_001bd1f0, tick FUN_001bd200, 0x132c20
    OM,     // O??M           FUN_0012ed90 -> 0x4cd710, ctor FUN_001bd860, init FUN_001bd9d0, tick FUN_001bdae0, contact FUN_001bdbb0, 0x132c30
    ON40,   // O??N n in 40..42   FUN_0012ee00 -> 0x4cd6e0, ctor FUN_001bfe20, init FUN_001bff50, tick FUN_001c0010, 0x132c40
    ON,     // O??N otherwise FUN_0012ee70 -> 0x4cd6b0, ctor FUN_001bdec0, init FUN_001be030, tick FUN_001be0e0, contact FUN_001be180, 0x132c50
    OL,     // O??L           FUN_0012eee0 -> 0x4cd680, ctor FUN_001bec50, init FUN_001bed70, tick FUN_001bedc0, 0x132c60
    OO,     // O??O           FUN_0012ef50 -> 0x4cd650, ctor FUN_001bf850, tick FUN_001bf900, 0x132c70
    OP,     // O??P           FUN_0012efc0 -> 0x4cd620, ctor FUN_001bf260, tick FUN_001bf330, 0x132c80
    OQ,     // O??Q           FUN_0012f030 -> 0x4cd5f0, ctor FUN_001c0110, tick FUN_001c03e0, 0x132c90 (boss child spawner)
    O50N,   // table O50N     FUN_0012f0a0 -> 0x4cd5c0, ctor FUN_001be820, init FUN_001be960, tick FUN_001be9e0, 0x12d410
    O50J,   // table O50J     FUN_0012f110 -> 0x4cd590, ctor FUN_001b96e0, tick FUN_001b97e0, 0x12d420
    O00J,   // table O00J     FUN_0012f180 -> 0x4cd560, ctor FUN_001b91d0, tick FUN_001b9340, 0x12d430
    O02B,   // table O02B     FUN_0012f1f0 -> 0x4cd530, ctor FUN_001b8e20, init FUN_001b8f90, tick FUN_001b8fc0, 0x12d440
    O00B,   // table O00B/O01B/O03B/O04B FUN_0012f260 -> 0x4cd500, ctor FUN_001b85b0, init FUN_001b8920, tick FUN_001b89d0, contact FUN_001b8aa0, 0x12d450
    J017,   // table J017..J023, J027, J028, J039, J046, J047  FUN_0012f2d0 -> 0x4cd4d0, ctor FUN_0012af10, init FUN_0012afe0, tick FUN_0012b600, 0x12d460
    Projectile,   // table J030, J050..J074 (wrappers 0x12f340..0x12fc70, projectiles.cpp), launched by ACT Object
    Helper, // type-5 helper objects created by the classes above (see objects_helpers.cpp)
};

// Pointer-to-member state slot (PTMF, 12 bytes {delta, index, fn} in the original): `fn` = original address of the
// member function, 0 = null (uRam0055fdc8.. / FUN_0040caf0 false). FUN_0040ca60 calls it.
struct ObjData {
    ObjClass cls = ObjClass::None;
    uint32_t classId = 0;                 // vt last slot (FUN_00140250: 0x12d440 / 0x132c50 / 0x12d410 phys +0x34,
                                          // 0x132c30 / 0x132c40 phys +0x28)
    uint32_t helperKind = 0;              // Helper: address of the helper's ctor (identifies its vtable)
    std::array<int32_t, 0x80> iw{};       // int words at +off (index off / 4)
    std::array<float, 0x80> fw{};         // float words
    std::array<Chr*, 0x80> pw{};          // entity pointers
    std::array<uint32_t, 0x80> ptmf{};    // PTMF fn addresses at their base offset
    int32_t& I(uint32_t off) { return iw[off >> 2]; }
    float& F(uint32_t off) { return fw[off >> 2]; }
    Chr*& P(uint32_t off) { return pw[off >> 2]; }
    uint32_t& M(uint32_t off) { return ptmf[off >> 2]; }
    int32_t I(uint32_t off) const { return iw[off >> 2]; }
    float F(uint32_t off) const { return fw[off >> 2]; }
    Chr* P(uint32_t off) const { return pw[off >> 2]; }
    uint32_t M(uint32_t off) const { return ptmf[off >> 2]; }
    // model holder +0x24 (FUN_0012db80 -> FUN_0010a1b0) published matrix (FUN_00109d40)
    bool hasHolder = false;
    bool published = false;
    Vec3f pubScale{1.0f, 1.0f, 1.0f};
    ChrTransform pubXf;
    bool hidden = false;                  // FUN_00109f70 / node visibility off
    int64_t clock = 0;                    // O00J +0x28 (int64 animation clock in ns)
    int64_t animTimeNs = -1;              // O00J: model animation time set through vt+0x9c (-1 = actor clock)
    std::shared_ptr<void> extra;          // port: record owned by the class (the item trap object of FUN_001e7630)
};

// ---- creation / dispatch (called by the character core) ----
std::unique_ptr<Chr> objectConstruct(uint32_t tag);   // FUN_0012ce70 J/O branch: class table / 4th letter + ctor
void objectInit(Chr& c);                              // vt+0x1c (via FUN_00128690)
void objectTick(Chr& c);                              // vt+0xc
bool objectActive(const Chr& c);                      // vt+0x20
int objectContact(Chr& c, Chr* other, uint32_t m1, uint32_t m2, uint32_t m3);   // vt+0x24 (hit resolution FUN_0014c970)
void objectDraw(const Chr& c, alchemy::SceneRenderer& r, int pass, float alpha);
void objectPose(Chr& c);
// List 0x50b000 bodies for FUN_00140250 (classes with a physics object), installed as the collision scene default.
void objectCollisionBodies(std::vector<CollBody>& out);

// Item entities (type 3): game/objects/item_entity.cpp
std::unique_ptr<Chr> itemEntityConstruct(uint32_t tag);   // FUN_0012e810 + FUN_001d3960 (class 0x4cd960)
void itemEntityInit(Chr& c);                              // vt+0x1c = FUN_001d3d40 (init by id)
void itemEntityTick(Chr& c);                              // vt+0xc = FUN_001d5180
Chr* itemEntityOf(const Item* item);                      // the task-list entity standing for `item` (nullptr none)
void itemEntitySyncFromChr(Chr& c);                       // spawn-time transform writes made through the Chr -> the Item
void itemEntityPlace(Chr& c);                             // orig FUN_001d5620 (after a spawn routine's transform writes)
void itemEntityUnlink(Item& it);                          // orig FUN_001d3b30: out of the task list, model dropped, item kept

// ---- shared helpers of the object classes (objects.cpp) ----
uint32_t objTag(const char* s4);                        // 4 chars as the little-endian word
std::string tagString(uint32_t tag);
int tagNumber(uint32_t tag);                          // FUN_002388f0 on tag chars 1..2 (atoi base 10)
uint32_t floorFlagWord();                             // uRam0050a610
int floorIdNow();                                     // iRam0050a2a8
// FUN_001099e0(holder +0x24, path, mode, flags): load the IGB as the object's model (synchronous)
bool objLoadModel(Chr& c, const std::string& path, int mode, uint32_t flags);
void objPublish(Chr& c);                              // FUN_00109d40(+0x24, pos, scale, fwd, up, right)
void objPublishAt(Chr& c, const Vec3f& pos);        // FUN_00109d40 with an explicit position
Chr* findEntity(uint32_t tag, int type);              // FUN_0012c780(0x50b130, tag, type): live, non-dead, id == tag
bool sameRoomAsPlayer(const Vec3f& pos);              // FUN_001b75d0
int roomOfCell(const uint32_t* cell);                 // (*cell & 0xf000) == 0x1000 ? *cell & 0xf : -1
void entityRemove(Chr& c);                            // FUN_00207350: active task -> flags |= 0x30000000
void rotateAboutRight(ChrTransform& t, float rad);    // FUN_00128c00
void rotateAboutForward(ChrTransform& t, float rad);  // FUN_001289c0
void setAxes(ChrTransform& t, const Vec3f& fwd, const Vec3f& up, const Vec3f& right);   // FUN_00128740
// FUN_00112190(0, fx, pos, rotDeg, scale) and friends through the effect runtime (objects_fx.cpp); handle 0 = none.
int fxSpawn(int fx, const Vec3f* pos, const Vec3f* rotDeg, const Vec3f* scale);
bool fxExists(int h);                                 // FUN_00112500
void fxKill(int h);                                   // FUN_00112530
void fxStop(int h);                                   // FUN_00112560
void fxSetMatrix(int h, const float m[16]);           // FUN_00112770
void fxSetPosition(int h, const Vec3f& pos);          // FUN_001122d0
void fxAddRotation(int h, const Vec3f& rotDeg);       // FUN_001123b0
void fxLoopOut(int h, int16_t count, int16_t end, int restart);   // FUN_001126e0
// orig FUN_0012adb0 -> FUN_0012ab10: effect `fx` on the floor face under `pos` (probe radius 1 from pos + 10 down
// 300), its matrix built from the face normal and a random yaw; -1 when no floor was hit (item_tasks.cpp)
int fxOnGround(int fx, const Vec3f& pos);
// Item-module task records (item_tasks.cpp): the trap FUN_001e7630 (vtable 0x4cf120) and the BN18 bone field
// FUN_001e4400 (vtable 0x4cef40), type-5 helpers. True when `c` is one of them (tick / destructor handled).
bool itemTaskTick(Chr& c);
bool itemTaskDestroy(Chr& c);
int objSe3d(int id, const Vec3f& pos, float vol = 1.0f);   // FUN_00115470(vol, 0x4e9790, id, pos) -> handle
void objSeStop(int handle);                           // FUN_00115820(0x4e9790, handle, 2)
void objSeMove(int handle, const Vec3f& pos);         // FUN_00116260(0x4e9790, handle, pos)
void objectDestroy(Chr& c);                           // class destructor (vtable +8) when the record is deleted
// vt+0x28 of the projectile classes (FUN_00148350 -> launch(owner, spawn position, owner bone)); no-op for others
void objectLaunch(Chr& c, Chr& owner, const Vec3f& pos, int bone);
// Services of other modules the projectile classes read.
struct ProjectileHooks {
    // C27A battery direction (NPC class field +0x8c..+0x94, npc_class.cpp); false = not available
    std::function<bool(const Chr& owner, float out[3])> ownerDirection;
};
ProjectileHooks& projectileHooks();

}  // namespace baroque::game
