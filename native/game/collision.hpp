#pragma once
// Engine sphere-sweep collision against CLS meshes (engine glue 0x209b20..0x20b5f0) and the game's physics
// object (vtable 0x4cdbb0, PLAYER.md §1.3 / §5). Translated from work/decompiled; see work/reports/PLAYER.md.
//
// Runtime layout notes (pointers in the original are indices/values here):
//   CLS face (0x44 B): +0 type 'W'/'F'/'L', +1 attr char[7], +8 stamp (int), +0xc normal*, +0x10 vertex*[3],
//     +0x1c edge[3] {dirVec*, outwardVec*, neighbourFace*}  (data::ClsFace, vecA = edge direction, vecB = edge-plane
//     normal pointing out of the triangle -- roles established by FUN_0020adb0).
//   Collision object (FUN_00209fa0, at room entry 0x50ba50+i*0x50 +4): +0 CLS header, +4 cells (0x14 B each:
//     +0 face list {face*[], count}, +4 dynamic list A, +0xc dynamic list B), +0xc cell count, +0x10 stamp,
//     +0x14 hit record {found, dist, L0{ptr,cap 8,count}, L1, L2, L3} (FUN_0013fa60 allocates cap 8).
//   Global hit list 0x50b730: 0x20 entries x 0x18 B {face*, edge*, vec3 (room offset or push normal), owner*};
//     count 0x50ba30, nearest distance 0x50ba38 (FUN_0013f870).
// All float code here must run inside an alchemy::ps2::Ps2FloatScope (the game tick opens it).
#include "data/map_formats.hpp"

#include <array>
#include <functional>
#include <cstdint>
#include <string>
#include <vector>

namespace baroque::game {

using Vec3f = std::array<float, 3>;

struct CollEdge {
    Vec3f dir{};          // edge+0 (vecA)
    Vec3f outward{};      // edge+4 (vecB)
    int neighbour = -1;   // edge+8 face index (-1 = null)
};

struct CollFace {
    char type = 0;        // +0
    char attr[8] = {};    // +1..+7, NUL-terminated
    int stamp = 0;        // +8
    Vec3f normal{};       // *(+0xc)
    Vec3f v[3]{};         // *(+0x10), *(+0x14), *(+0x18)
    CollEdge edge[3];     // +0x1c, +0x28, +0x34
};

struct FaceHit {          // hit record list entry (8 B): face*, edge*
    int face = -1;
    int edgeFace = -1;    // face owning the edge (same as face), -1 when no edge
    int edge = -1;        // 0..2, -1 = none
};

struct HitList {          // {ptr, cap, count}
    std::vector<FaceHit> items;
    uint32_t cap = 8;
    uint32_t count = 0;
};

struct HitRecord {        // collision object +0x14
    int found = 0;        // +0x14
    float dist = 0.0f;    // +0x18
    HitList l0;           // +0x1c face hits
    HitList l1, l2, l3;   // dynamic-object lists (not produced by static CLS)
};

// Runtime CLS collision object (FUN_00209fa0 / FUN_0020b6e0 relocation).
struct CollisionMesh {
    // header fields used at runtime (+0x40..+0x60)
    float cellSize[3] = {0, 0, 0};   // +0x40
    int32_t n[3] = {0, 0, 0};        // +0x4c nx, ny, nz
    int32_t origin[3] = {0, 0, 0};   // +0x58
    std::vector<CollFace> faces;
    std::vector<std::vector<int>> cells;  // face lists, index ix + iy*nx*nz + iz*nx (FUN_0020a9d0)
    int stamp = 0;                   // +0x10
    HitRecord rec;                   // +0x14

    static CollisionMesh fromCls(const data::Cls& cls);
    void resetStamps();              // FUN_0020a050
};

// A collision piece placed in the floor (room entry): mesh + world offset (FUN_00127510 returns the offset).
struct CollisionPiece {
    const CollisionMesh* meshConst = nullptr;
    CollisionMesh* mesh = nullptr;
    Vec3f offset{};
    bool mtPiece = false;            // id & 0xf000 == 0x1000 -> table 0x50c050 (corridor pieces)
};

struct GlobalHit {                   // 0x50b730 entry
    const CollisionMesh* mesh = nullptr;
    int face = -1;                   // -1 = character/object hit (FUN_0013f990 writes face = edge = 0)
    int edge = -1;
    Vec3f vec{};                     // room offset (face hits) or push normal (character hits)
    const void* owner = nullptr;     // +0x14 (character hits): the other body's physics object
    char ownerId0 = 0;               // **(owner+0x5c)+0x1c: first id character of that phys's shape owner
};

struct GlobalHits {
    std::vector<GlobalHit> list;     // 0x50b730 (0x20 entries in the original)
    int count = 0;                   // 0x50ba30
    float nearest = 0.0f;            // 0x50ba38
};
GlobalHits& globalHits();

// Ground info (phys+0x58): {u32 n, face*[4] walls, u32 nFloor(+0x14), face*[4] floors(+0x18)}.
struct GroundInfo {
    uint32_t nWalls = 0;
    std::array<const CollFace*, 4> walls{};
    uint32_t nFloors = 0;
    std::array<const CollFace*, 4> floors{};
};

// Collision shape (phys+0x5c): {owner, radius(+4), height(+8), a(+0xc), b(+0x10), top(+0x14)}.
struct Shape {
    const void* owner = nullptr;
    float radius = 0.0f;
    float height = 0.0f;
    float a = 0.0f, b = 0.0f;
    float top = 0.0f;
};

// The physics object (vtable 0x4cdbb0, ctor FUN_00140140). Offsets as in the original.
struct Phys {
    CollisionMesh* world = nullptr;  // +0x04 collision object currently tested
    Vec3f* pos = nullptr;            // +0x0c (= Transform+0xc)
    Vec3f* move = nullptr;           // +0x10 (= &vel)
    float radius = 0.0f;             // +0x14
    Vec3f centreOffset{};            // +0x18 (0, radius, 0)
    Vec3f centre{};                  // +0x24 swept-sphere centre
    Vec3f dir{};                     // +0x30 unit move direction
    Vec3f negDir{};                  // +0x3c
    float length = 0.0f;             // +0x48 move length
    uint32_t flags = 0;              // +0x4c bit0 airborne, bit1 wall hit, bit16 ground-probe mode
    float gravity = 1.0f;            // +0x50
    float vy = 0.0f;                 // +0x54
    GroundInfo ground;               // *(+0x58)
    Shape* shape = nullptr;          // +0x5c
    uint32_t airTicks = 0;           // +0x60
    uint32_t result = 0;             // +0x6c
    Vec3f vel{};                     // +0x70
    Vec3f moveVel{};                 // +0x7c
    Vec3f knock{};                   // +0x88
    float actMoveSpeed = 0.0f;       // +0x94
    float actMoveAngle = 0.0f;       // +0x98
    float actMoveAcc = 0.0f;         // +0xa0

    // Owner state the physics reads (PLAYER.md §5): char type < 3, Lethargy status bit 0x20, Knockback/Pull state.
    bool ownerIsCharacter = true;
    bool ownerLethargic = false;
    bool ownerInKnockbackOrPull = false;
    const float* ownerForward = nullptr;  // owner matrix row +0x30 (FUN_00143060)
};

// A character (list 0x50a7b0 of manager 0x50b130: types 0..2) or a dungeon object (list 0x50b000) as FUN_00140250
// and FUN_00142290 read it. Characters: phys = *(chr+0x2c)+4, transform = *(chr+0x20).
struct CollBody {
    const void* owner = nullptr;     // the list entry (== phys->shape->owner for characters)
    Phys* phys = nullptr;            // its physics object
    const Vec3f* pos = nullptr;      // transform +0x0c (== *phys->pos)
    const Vec3f* right = nullptr;    // transform +0x18
    const Vec3f* fwd = nullptr;      // transform +0x30
    char id0 = 0;                    // first id character (+0x1c): 'O' objects resolve as walls in FUN_00141e40
    bool active = false;             // vtable +0x20 returned 1
    bool removed = false;            // chr +0x24 / object +0x10 bit 0x10000000
    // FUN_00140250 / FUN_00142290 predicate: type 2 with (id & 0xffffff) == "E19" and FUN_001f1730(ai, 0x1000008)
    // == 0, or type 1 with id != "C18A" (strings at 0x44b2d8 / 0x44b2d0). Such characters block the sweep
    // (FUN_00140a10) and are never pushed themselves.
    bool solid = false;
    const uint32_t* cell = nullptr;  // chr +0x30 (FUN_00124c70 result); room = *cell & 0xf when (& 0xf000) == 0x1000
    int cellX = 0, cellY = 0;        // chr +0x34 / +0x38
};

// Scene of collision pieces and other characters' shapes the physics tests against (FUN_00127210/FUN_00127510 and
// the character list 0x50b130). UNVERIFIED stand-in: the test world registers its pieces directly; the floor
// runtime that selects rooms by AABB (FUN_00127210) is not translated yet.
struct CollisionScene {
    std::vector<CollisionPiece> pieces;
    // List 0x50a7b0 in list order (FUN_0012c1c0 count / FUN_0012c1d0 array), including the moving character.
    std::function<void(std::vector<CollBody>&)> characters;
    // List 0x50b000 (FUN_0012c210 / FUN_0012c220) restricted to the object classes FUN_00140250 tests, with phys set
    // per class: 0x12d440 / 0x132c50 / 0x12d410 -> obj+0x34, 0x132c30 / 0x132c40 -> obj+0x28. `removed` = obj+0x10.
    std::function<void(std::vector<CollBody>&)> objects;
    // orig FUN_00124c70: cell word of the dungeon grid under a position (nullptr outside the grid).
    std::function<const uint32_t*(const Vec3f&)> cellAt;
    const Vec3f* playerPos = nullptr;     // *(0x50b144)+0x20 -> +0xc (ground-probe LOD distance)
    // FUN_00142290 character push, called by FUN_001416b0: writes *move = push target - pos, true when pushed.
    // game::characterPush below is the translation over `characters`.
    std::function<bool(Phys&)> characterPush;
};
void setCollisionScene(CollisionScene* scene);
CollisionScene* collisionScene();
// Provider of list 0x50b000 used when a scene without its own `objects` is set (the dungeon object module registers
// it; FUN_00140250 tests that list on every floor).
void setDefaultCollisionObjects(std::function<void(std::vector<CollBody>&)> f);

// orig FUN_00142290 (work/decompiled/00142290.c) over CollisionScene::characters.
bool characterPush(Phys& p);

void physInit(Phys& p, Vec3f* pos, Shape* shape, float radius);  // FUN_00140140 + FUN_0020a860 (+FUN_00142e10)
uint32_t physTick(Phys& p);                                      // FUN_00142e60
uint32_t physMove(Phys& p);                                      // FUN_001416b0
bool collide(Phys& p, int mode);                                 // FUN_00140250 (mode 7 sweep, 1 probe)
// orig FUN_00143150 (+FUN_001431e0): sphere ray from `from` along `delta`; nearest hit distance on a hit.
bool raycast(const float from[3], const float delta[3], float radius, float* distOut);

// Helpers from the engine library.
float normalize(Vec3f& v);                                       // FUN_00206ab0 (returns length)
const Vec3f& upVector();                                         // FUN_00206f10
const Vec3f& downVector();                                        // FUN_00206f50
void rotateY(float rad, Vec3f& out, const Vec3f& in);            // FUN_00206ca0
bool slideDirection(Vec3f& out, const Vec3f& v, const Vec3f& n); // FUN_0020b3d0

}  // namespace baroque::game
