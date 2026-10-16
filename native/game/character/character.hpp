#pragma once
// Character objects (PLAYER.md §1-2): class vtable 0x4cd920 (base 0x4ccc30 -> 0x4ccc70), created by the factory
// FUN_0012d2f0 / FUN_0012ce70 (manager 0x50b1a0), listed in the character task list 0x50b134 (manager 0x50b130),
// per-tick FUN_00129a30 -> FUN_00129c60. Components (+0x2c, FUN_0012dd60): model/anim (+0), physics (+4),
// status (+8), state machine (+0xc), ACT player (+0x10), AI (+0x14).
//
// This header is the contract shared by the character core (character.cpp), the player controller
// (player_control.cpp, FUN_0013b0d0 and callees) and the Game scene. Field comments give original offsets.
#include "alchemy/animation.hpp"
#include "alchemy/igb.hpp"
#include "alchemy/scene_render.hpp"
#include "alchemy/texture.hpp"
#include "data/act.hpp"
#include "data/prm.hpp"
#include "game/act_runner.hpp"
#include "game/actor_anim.hpp"
#include "game/character/ai.hpp"
#include "game/character/model_fx.hpp"
#include "game/collision.hpp"
#include "game/player_move.hpp"

#include <array>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace baroque::game {

struct Chr;

// Character types (+0x18)
enum ChrType : int { kChrPlayerType = 0, kChrNpc = 1, kChrEnemy = 2, kChrItem = 3, kChrObject = 4, kChrEffect = 5 };

// Transform object (+0x20, 0x3c bytes, FUN_0012ddf0)
struct Transform {
    Vec3f scale{1.0f, 1.0f, 1.0f};   // +0x00 (0.5 for E03B/E09B, FUN_001295e0)
    ChrTransform m;                   // +0x0c pos, +0x18 right, +0x24 up, +0x30 forward
};

// StatusBlock (FUN_00139ea0; saved as 0x448 bytes, PLAYER.md §1.4/1.7)
struct StatusBlock {
    int32_t hpMax = 0;        // +0x00
    int32_t hp = 0;           // +0x04 (0 = dead)
    int32_t vtMax = 0;        // +0x08
    int32_t vt = 0;           // +0x0c
    int32_t atkBase = 0;      // +0x10
    int32_t atkMax = 0;       // +0x14
    int32_t atkLevel = 0;     // +0x18
    int32_t defBase = 0;      // +0x1c
    int32_t defMax = 0;       // +0x20
    int32_t defLevel = 0;     // +0x24
    int32_t critical = 0;     // +0x28
    int32_t expValue = 0;     // +0x2c
    std::array<int32_t, 3> element{};      // *(+0x30)
    std::array<int32_t, 3> directional{};  // *(+0x34)
    std::array<int32_t, 19> resist{};      // *(+0x38)
    uint32_t statusMask = 0;               // +0x3c
    std::array<int32_t, 19> timers{};      // *(+0x40)
    std::array<int32_t, 19> effects{};     // *(+0x44) (-1)
    int32_t level = 0;        // +0x48
    int32_t exp = 0;          // +0x4c
    int32_t expNext = 0;      // +0x50
    uint32_t brand = 0;       // +0x54
    std::array<int32_t, 5> hidden{};       // *(+0x58)
    void* parasite = nullptr; // +0x5c
    void* carried = nullptr;  // +0x60
    float lastExpMul = 0.0f;  // +0x64
};

// Status object (vtable 0x4cdb40, ctor FUN_001336c0)
struct StatusObj {
    Chr* owner = nullptr;                 // +0x04
    const data::PrmChar* prm = nullptr;   // +0x08
    StatusBlock block;                    // *(+0x0c)
    uint32_t flags10 = 0;                 // +0x10 bit 0: ACT GetItem 0 (FUN_0014a0b0) -> the NPC refuses items
                                          //   (FUN_001d9040); cleared by every ACT reset (FUN_00143bf0)
};

// State machine (comp+0xc, vtable 0x4cdab0, ctor FUN_0012e060). States are identified by their factory label.
enum class StateId : int {
    None = 0, Idle, HitStagger, Knockback, Pull, DeathA, DeathB, ThrowLock, Event, TypeId,
    MenuThrow,   // factory FUN_001db970 -> vtable 0x4cede0 (item screen Throw, "TH00")
    MenuPlace,   // factory FUN_001db900 -> vtable 0x4cee00 (item screen Place, "TH06")
    MenuGive,    // factory FUN_001db9e0 -> vtable 0x4cedc0 (item screen Give, "TH07")
};
struct StateMachine {
    StateId cur = StateId::None;     // +0x04 State* (identified by its factory, +0x14 copy)
    int arg = 0;                     // +0x08
    int priority = 0;                // +0x0c
    int priorityCopy = 0;            // +0x10
    StateId curFactory = StateId::None;   // +0x14
    StateId next = StateId::None;    // +0x18 (StateId::None = no request; the original's -1 "pop" is `popRequested`)
    bool popRequested = false;
    StateId lastFactory = StateId::None;  // +0x1c
    int phase = 0;                   // state-private coroutine position (PTMF at state +8..+0x10)
    // state-private data of the reaction / death states (state object +8..+0x24)
    int timer = 0;                   // flinch +8 / pull +0x14 / death fade counter +0x14 (starts -3)
    Vec3f dir{};                     // knockback/pull +8..+0x10
    float speed = 0.0f;              // knockback +0x14
    int vanish = 0;                  // death +0x20 (vanish variant)
    int noDrop = 0;                  // death +0x24
    float spinA = 0.0f, spinB = 0.0f;   // death +0x18 / +0x1c (vanish spin)
    bool gameOver = false;           // player death: PTMF FUN_001cd9d0 reached without the ST05 brand (fade-out and
                                     // FUN_001cdcd0 -> FUN_0011ce60 run by the Game scene, gameOverTick)
    int deathCont = 0;               // player death PTMF: 1 = FUN_001cd9d0 next tick, 2 = FUN_001cdbe0 (ST05 revival)
    uint32_t eventPtmf = 0;          // player Event state PTMF (+8..+0x10): original member address, 0 = null
    bool throwHeld = false;          // ThrowLock +0xc: thrown item's model attached to "Right_WEAPON00_A"
    const void* throwItem = nullptr; // ThrowLock: inventory item selected at entry
    // Request (pattern of FUN_0012a5e0 etc.): accepted when no state is pending/current or prio >= current prio.
    bool request(StateId s, int arg, int prio);
};

// Animation of a part that is its own actor (player HEAD: FUN_001b1350 +0x40 wrapper, 24-bone face skeleton):
// heap-held so the part can move inside the parts vector.
struct PartActor {
    std::vector<std::unique_ptr<alchemy::AnimClip>> clips;
    ActorAnim anim;
    std::vector<alchemy::Mat4> pose;
};

// Model/anim component (comp+0, FUN_0012dd10 -> FUN_0010a370 actor wrapper; FUN_001099e0 loads the IGB)
struct ChrModelPart {
    std::unique_ptr<alchemy::IgbFile> file;
    std::unique_ptr<alchemy::TextureLibrary> lib;
    std::unique_ptr<alchemy::SceneModel> model;
    std::string attachBone;          // non-empty: drawn at that bone of the main skeleton (HEAD part)
    int attachBoneIndex = -1;        // joint-attach node (FUN_003b1480, +0x20 = joint index) when >= 0
    alchemy::Mat4 local = alchemy::Mat4::identity();   // transform node between the attach point and the part
                                     // (FUN_003a4140 matrix, e.g. the E22 bubble scale)
    bool visible = true;             // node flag bit0 (igNode visibility toggled by the class ticks)
    bool hasOverride = false;        // igOverrideAttrSet directly above the part (E02 OBJ067 FUN_001c1140)
    alchemy::AttrOverride ovr;
    bool hasGlow = false;            // E09 FUN_001c1f70: igSwitch -> igOverrideAttrSet -> the part's graph again
    bool glowOn = false;             //   the switch's child-mask bit 0 (FUN_001c2450)
    alchemy::AttrOverride glow;
    std::shared_ptr<PartActor> actor; // the part is a separate actor with its own animations (player HEAD)
    // the actor file's igSkin named "lod" (animation database _skinList, 0x42c918), drawn in place of the appearance
    // skin while ChrModel::lodDraw / lodCaster say so (FUN_00108d20 / FUN_001092a0); null: the file has none. Only the
    // body part (parts[0]) of a character gets one (chrLoadModel).
    std::unique_ptr<alchemy::SceneModel> lodModel;
};
// Joint modifier (FUN_003bf580 object: +8 joint index, +0xc matrix FUN_003a4140, +0x10 enabled), kept in the actor's
// modifier list (actor+0x30)+0x34. UNVERIFIED (Alchemy internal): the matrix is applied in the joint's local space
// on top of the animated pose (world_j' = M * world_j, children follow).
struct BoneMod {
    int joint = -1;
    alchemy::Mat4 m = alchemy::Mat4::identity();
};
struct ChrModel {
    std::vector<ChrModelPart> parts; // [0] = DATA/CHR/%c%c%c/%c%c%c%c.IGB (FUN_0012b620 path 0x44a9c0)
    std::vector<std::unique_ptr<alchemy::AnimClip>> clips;
    ActorAnim anim;
    uint32_t flags2c = 0;            // +0x2c (0x80000000 = loaded, FUN_00109c80 callback; 0x20000000 shadow
                                     //   caster, 0x10000000 never a shadow caster: FUN_00109f10 / FUN_00109f70)
    MaterialFader fader;             // +0x24 material fader (FUN_0010a0a0 / FUN_0010a130, model_fx.hpp)
    bool lit = false;                // FUN_001099e0 mode 1 (no 0x8000000): parent 0x4e78f4 = the lit branch
    ModelLight light;                // +0x14..+0x20 own light (mode 1 without 0x4000000)
    std::vector<alchemy::Mat4> pose; // last evaluated bone world matrices (presentation)
    std::vector<BoneMod> boneMods;   // (actor+0x30)+0x34 joint modifier list
    // player +0x6c: the coat's shadow skin (first skin of DATA/OBJ/CT01_KAGE.IGB / CT06_KAGE.IGB, FUN_001b2230) on the
    // actor's skeleton; drawn only by the shadow pre-pass FUN_001113d0 (attached for the pass with FUN_003c7cb0)
    std::shared_ptr<ChrModelPart> coatShadow;
    // Distance LOD (FUN_00108d20 / FUN_001092a0, chrSkinLodTick). lodSkin: the actor's appearance _skin
    // ((actor+0x30)+0x2c +0xc) is the "lod" skin, else _skinList[0]. PORT (presentation, read-only): lodCaster / lodDraw
    // = the skin the tick's shadow pre-pass / scene pass traversed.
    bool lodSkin = false;
    bool lodCaster = false;
    bool lodDraw = false;
    // igActor igNode _flags bit 0x10 ((actor+0x30) vt+0x70 = FUN_00109270 set/clear; FUN_0010a370 clears it, FUN_00108d20
    // sets it within 600 of the eye). UNVERIFIED: the Alchemy traversal that reads the bit was not located; the port
    // keeps the value only (nothing consumes it).
    bool actorFlag10 = false;
    bool loaded() const { return (flags2c & 0x80000000u) != 0; }
    const alchemy::Skeleton* skeleton() const;
};

// Character class = factory table 0x44aa10 entry (ctor wrapper -> class vtable). Enemies not in the table and every
// untabled C id use the generic class FUN_0012e880 (vtable 0x4cd920).
enum class ChrClass : uint8_t {
    Generic,   // FUN_0012e880 -> 0x4cd920 (Activate FUN_001299f0, ClassTick FUN_00129c50 nop)
    Player,    // FUN_00130370 -> 0x4cccb0 (C00A, C01A; Activate FUN_001b1350)
    Npc,       // FUN_00130300 -> 0x4cccf0 (Activate FUN_001b39f0)
    NpcC03A,   // FUN_00130290 -> 0x4ccd30
    NpcC08,    // FUN_00130220 -> 0x4ccd70 (C08A/B/C)
    NpcC12A,   // FUN_001301b0 -> 0x4ccdb0
    NpcC18A,   // FUN_00130140 -> 0x4ccdf0
    NpcC27A,   // FUN_001300d0 -> 0x4cce30
    E01,       // FUN_00130060 -> 0x4cce70 (ctor FUN_001c0bb0, Activate FUN_001c0cf0, ClassTick FUN_001c0db0)
    E02,       // FUN_0012fff0 -> 0x4cceb0 (ctor FUN_001c0f20, Activate FUN_001c1140, ClassTick FUN_001c14e0)
    E05,       // FUN_0012ff80 -> 0x4ccef0 (ctor FUN_001c1800, Activate FUN_001c18b0, ClassTick nop)
    E07,       // FUN_0012ff10 -> 0x4ccf30 (ctor FUN_001c19b0, Activate FUN_001c1a70, ClassTick FUN_001c1b00)
    E09,       // FUN_0012fea0 -> 0x4ccf70 (ctor FUN_001c1d30, Activate FUN_001c1f70, ClassTick FUN_001c2450)
    E19,       // FUN_0012fe30 -> 0x4ccfb0 (ctor FUN_001c27c0, Activate FUN_001c28a0, ClassTick FUN_001c28f0)
    E22,       // FUN_0012fdc0 -> 0x4ccff0 (ctor FUN_001c2cc0, Activate FUN_001c3020, ClassTick FUN_001c3190)
    E23,       // FUN_0012fd50 -> 0x4cd030 (ctor FUN_001c3ad0, Activate FUN_001299f0, ClassTick FUN_001c3b90)
    E27,       // FUN_0012fce0 -> 0x4cd070 (ctor FUN_001c3ef0, Activate FUN_001c3fa0, ClassTick nop)
};

// Per-class object fields (+0x40.. of the derived classes). Only the members of the object's own class are used.
struct ChrClassData {
    // E01 (FUN_001c0bb0 / FUN_001c0cf0): +0x40 joint modifier on "Dummy", +0x44 its matrix, +0x48 x scale, +0x4c step
    // E09 (FUN_001c1d30 / FUN_001c1f70): +0x40 head transform, +0x44 joint attach "Head_Joint", +0x48 modifier on
    //     "Head_joint", +0x4c its matrix, +0x50 swell scale, +0x54 toggle
    // E22 (FUN_001c2cc0 / FUN_001c3020): +0x40 bubble matrix, +0x44 joint attach "Bubble", +0x48 phase, +0x4c bubble
    //     scale, +0x50 limb growth time
    // E23 (FUN_001c3ad0): +0x40 collision shape cleared, +0x44 (Chr::deathVariant)
    // E02 (FUN_001c0f20 / FUN_001c1140): +0x40 OBJ067 attribute set, +0x44 group (part index below); f4c = the
    //     overlay's material diffuse value
    // E07 (FUN_001c19b0 / FUN_001c1a70): +0x40 texture matrix attribute of the tire
    int modIndex = -1;               // index of the class's first joint modifier in ChrModel::boneMods (E01/E09/E22)
    int partIndex = -1;              // index of the class's extra model part (E02 OBJ067, E09 head, E22 bubble)
    int i40 = 0;                     // E23 +0x40 (shape cleared), E07 +0x40 (attribute found)
    int i48 = 0;                     // E22 +0x48 phase (0, 5000, 100, 200, 300, 400)
    int i54 = 0;                     // E09 +0x54
    float f48 = 0.0f;                // E01 +0x48
    float f4c = 0.0f;                // E01 +0x4c, E22 +0x4c
    float f50 = 0.0f;                // E09 +0x50, E22 +0x50
    float texV = 0.0f;               // E07 texture-matrix v offset (FUN_001c1b00 result)
    // E19 (FUN_001c27c0): +0x40 second actor (OBJ068%c), +0x44 scale, +0x50 pos, +0x5c right, +0x68 up, +0x74 fwd,
    //     +0x80 load flags 0x40000011, +0x84 last animation index (-1), +0x88 phase, +0x8c y-scale speed, +0x90 y speed
    std::unique_ptr<ChrModel> shell;
    Vec3f shellScale{1.0f, 1.0f, 1.0f};
    ChrTransform shellXf;
    uint32_t shellFlags = 0x40000011;
    int shellAnim = -1;
    int shellPhase = 0;
    float shellVs = 0.0f;
    float shellVy = 0.0f;
    bool shellDrawTiny = false;      // last publish used the 0.01 scale (player status 0x10)
};

// ACT turn object (ACT player +0x68, pool 0x53ecb8 FUN_00127e20): RotYAct target / position, rate, yaw offset
struct ActTurn {
    bool active = false;             // +0x68 != 0
    Chr* target = nullptr;           // +0x00
    Vec3f pos{};                     // +0x04..+0x0c
    float rate = 0.0f;               // +0x10
    float ofs = 0.0f;                // +0x14 degrees
};

struct ObjData;   // game/objects/objects.hpp: object classes (J/O tags, type 4) and their type-5 helpers
struct Item;      // game/items/item.hpp: the world item an item entity (type 3) stands for

// Character object (FUN_001293f0 ctor + vtable 0x4cd920). The task list 0x50b134 also holds the item entities (type 3,
// class 0x4cd960) and the dungeon objects (type 4/5): they share this record (their own data in `obj` / `item`).
struct Chr {
    uint32_t taskFlags = 0;          // +0x10 (0x10000000 remove, 0x20000000 free)
    uint32_t serial = 0;             // +0x0c
    int type = 0;                    // +0x18
    uint32_t id = 0;                 // +0x1c 4 chars (e.g. 'C00A' as little-endian u32)
    Transform xf;                    // *(+0x20)
    uint32_t flags24 = 0;            // +0x24 bit0 active, bit1 updated this tick, bit2 skip matrix publish,
                                     //       bit16 fall SE played, bit17 hazard SE played, 0x10000000 skip update
    uint32_t f28 = 0x40000010;       // +0x28 (Game Init writes 0x11: bit0 = synchronous ACT load)
    ChrClass cls = ChrClass::Generic;   // vtable (factory table 0x44aa10)
    ChrClassData cd;                 // derived-class fields (+0x40..)
    // components (*(+0x2c))
    ChrModel model;                  // +0x00
    Phys phys;                       // +0x04
    Shape shape;                     // phys +0x5c
    StatusObj status;                // +0x08
    StateMachine sm;                 // +0x0c
    ActRunner act;                   // +0x10
    ActTurn actTurn;                 // act +0x68
    Ai ai;                           // +0x14 (FUN_001f05c0)
    data::ActFile actFile;           // ACT cache slot (0x50c6e0 cache, per character here)
    bool actLoaded = false;          // slot valid (`*(slot*0xc + 0x50c6e0) != 0`)
    // grid cell (+0x30..+0x38) via FUN_00124c70
    uint32_t* cell = nullptr;
    int cellX = 0, cellY = 0;
    int effectSlot = -1;             // +0x3c hurt-volume index in the hit manager 0x50c780
    uint8_t deathVariant = 0;        // +0x44 (E23: DD02 death without drop)
    bool jitter = false;             // flinch: model published at jitterPos this tick (flags24 bit2)
    Vec3f jitterPos{};
    std::vector<alchemy::Mat4> tickPose;   // chrTickPose cache
    uint64_t tickPoseStamp = ~0ull;
    Vec3f prevPos{};                 // presentation interpolation (port)
    std::shared_ptr<ObjData> obj;    // object classes: class data (null for characters)
    Item* item = nullptr;            // item entities: the item (owned by the items module)

    std::string idString() const;    // 4 chars
    bool active() const { return (flags24 & 1u) != 0; }   // vt+0x20 FUN_0012d400
};

class ChrManager {
public:
    // orig FUN_0012d2f0 (upper-case, default 4th char 'A') -> FUN_0012ce70: item ids (FUN_001d38f0) -> item entity,
    // 'J'/'O' -> object classes (game/objects), 'C'/'E' -> characters (refused while 0x20 of types 0..2 exist).
    Chr* create(const char* id, bool addToList);
    Chr* createTag(uint32_t tag, bool addToList);    // orig FUN_0012ce70(0x50b1a0, tag, addToList)
    // FUN_002073e0(ent, 0x50b134, 0x50b138). PORT: capacity is kept ahead so an entity linked while the Game task walks
    // the list (range-for) never reallocates it; the walk's end iterator is fixed, so it is first ticked next tick.
    void link(Chr* c) {
        if (list_.capacity() < list_.size() + 64) list_.reserve(list_.size() + 1024);
        list_.push_back(c);
    }
    Chr* adopt(std::unique_ptr<Chr> c);              // takes ownership of an entity constructed outside create()
    void destroyAll();
    void removeAllButPlayer();                       // orig FUN_0012bfd0 (floor exit)
    void reap();                                     // unlink + delete characters flagged 0x10000000
    Chr* player() const { return player_; }          // *0x50b144 (mgr 0x50b130 +0x14)
    void setPlayer(Chr* c) { player_ = c; }           // FUN_0012be80(0x50b130, chr)
    const std::vector<Chr*>& list() const { return list_; }   // task list 0x50b134 (insertion order)
    // shared data
    const data::PrmCharFile* prm = nullptr;           // PRMCHAR.PRM (FUN_00133620)
    std::filesystem::path assetRoot;
private:
    std::vector<std::unique_ptr<Chr>> owned_;
    std::vector<Chr*> list_;
    Chr* player_ = nullptr;
    uint32_t nextSerial_ = 1;
};
ChrManager& characters();

// Per-character entry points (character.cpp)
void chrInitById(Chr& c);                 // orig FUN_001295e0
void chrUpdate(Chr& c);                   // orig FUN_00129a30
void chrCommonTick(Chr& c);               // orig FUN_00129c60
bool chrDamageWall(const CollFace* f);    // orig FUN_00140050 ('W' face, attr 'D', flag 0x49)
int chrContact(Chr& self, Chr* other, uint32_t m1, uint32_t m2, uint32_t m3);   // vt+0x24 of the entity's class
bool chrContactDefault(Chr& self, const Chr* other);   // orig FUN_00129140 (returns type == 1)
void chrActivate(Chr& c);                 // orig FUN_001299f0 (vt+0x30)
bool chrLoadModel(Chr& c);                // FUN_001cc480 -> FUN_001099e0 (synchronous in the port)
bool chrLoadAct(Chr& c);                  // orig FUN_001442a0
void chrDraw(const Chr& c, alchemy::SceneRenderer& r, int pass, float alpha);   // presentation
void chrPose(Chr& c);                     // presentation: evaluate the actor animation and skin the parts
// Skin/LOD state changes of one 3-D pass (FUN_001075a0) over the task list, in the original order: the shadow pre-pass
// FUN_001113d0 restores _skinList[0] after its traversal (when the pass is on), FUN_00108d20 (600 / 1200 from `eye`,
// the tick camera position), the scene traversal (records lodDraw), FUN_001092a0. Call once per tick.
void chrSkinLodTick(const float eye[3], bool shadowPrePassOn);
void chrReap();                           // task-list unlink of removed characters (FUN_00207380 + delete)
bool chrPush(Phys& p);                    // orig FUN_00142290 (installed as CollisionScene::characterPush)
bool chrSolidObstacle(const Chr& c);      // FUN_00140250 / FUN_00142290 test: NPC except C18A, E19 in MODE 0
bool chrLoadPart(ChrModelPart& part, const std::filesystem::path& path, const std::string& bone);   // FUN_001099e0 core
// vt+0x28 sub-actor (player HEAD FUN_001b1350 +0x40, NPC heads) + FUN_0010a8e0(blend, speed, actor, anim by exact
// name (FUN_0023f3fc) or null, 0): used by EVT op 105 (FUN_00170a90). False when the class has no sub-actor.
bool chrSubActorAnim(Chr& c, const char* name, float blend, float speed);
void chrApplyBoneMods(const ChrModel& m, std::vector<alchemy::Mat4>& world);   // joint modifiers (BoneMod)

// Character classes (enemy_classes.cpp)
ChrClass chrClassOf(uint32_t id);         // factory table 0x44aa10 (FUN_004c3340 static init) as used by FUN_0012ce70
std::string chrModelPath(uint32_t id);    // orig FUN_0012b620 ("DATA/CHR/%c%c%c/%c%c%c%c.IGB" with the E03B/E09B/E18B remap)
void chrClassActivate(Chr& c);            // vt+0x30 of the object's class
void chrClassTick(Chr& c);                // vt+0x34 of the object's class
void chrClassPose(Chr& c);                // presentation: class-owned actors (E19 shell)
void chrClassDraw(const Chr& c, alchemy::SceneRenderer& r, int pass, float alpha);   // presentation
float ps2Sin(float a);                    // orig FUN_00206900 (table 0x469830)
float ps2Cos(float a);                    // orig FUN_00206980 (table 0x469c30)

// ThrowLock state (LAB_0013cf80) services of the item system (inventory 0x53d910); unset = nothing held.
struct ThrowHooks {
    std::function<int(const void* item)> itemClass;                     // item +0x24 (0xc = gun)
    std::function<void(const void* item, const Vec3f& dir)> throwItem;  // FUN_001d5640(item, dir)
};
ThrowHooks& throwHooks();

// Status (status.cpp portion of character.cpp)
void statusInit(StatusObj& s, Chr& owner);   // orig FUN_001339e0
void statusCopyPrm(StatusObj& s);           // orig FUN_00133840
void statusPlayerNew(StatusObj& s);         // orig FUN_00133a60 -> FUN_00133b10
void statusHiddenStats(StatusObj& s);       // orig FUN_001395d0
int statusHpMaxTotal(const StatusObj& s);   // orig FUN_001343f0 (base + equipment / inventory terms, cap 999)
int statusExpForLevel(int n);               // orig FUN_00138dc0
int statusVtMaxTotal(const StatusObj& s);   // orig FUN_00134470
// Request tail of FUN_001d8c10 / FUN_001d8a40 / FUN_001d8d60 (item screen Throw / Place / Give): the player's menu
// action state for act4 "TH00" / "TH06" / "TH07" ((+0x14 == 0 && +0x18 == 0) || +0xc < 1 -> +0x18 = factory,
// +0xc = +0x10 = 0). False for any other name.
bool playerMenuActionRequest(const char* act4);
void groundEffect(Chr& c, int fx);          // orig FUN_0012ad50 (effect on the floor under the character)

// Tick-time model state used by the hit volumes (FUN_0014e3e0..): actor world matrix as published by FUN_00109d40
// and the bone matrices of the current animation time (actor vt+0x6c matrix array).
alchemy::Mat4 chrActorMatrix(const Chr& c);
const std::vector<alchemy::Mat4>& chrTickPose(Chr& c);
// Player-control object 0x50b620 (owned by the Game scene)
PlayerControl* playerCtl();
void setPlayerCtl(PlayerControl* ctl);

// Hook for the player branch of FUN_00129a30 (FUN_0013b0d0(0x50b620, chr)); installed by the Game scene.
extern std::function<void(Chr&)> playerControlHook;

// Player Event state (player_event.cpp, game build): LAB_001d1de0 -> FUN_001d1df0 / FUN_001d21d0 / FUN_001d2130
void playerEventEnter(Chr& c);
void playerEventUpdate(Chr& c);
void playerEventExit(Chr& c);
// TypeId state of C19A / C19D (player_event.cpp): FUN_001b2f40 enter, FUN_001b2ff0 update, FUN_001b2e10 (J030 contact)
void typeIdEnter(Chr& c);
void typeIdUpdate(Chr& c);
void typeIdShot(Chr& c);
void installCharacterRuntimeHooks();      // player_control_wiring.cpp (game build): event hooks of the character module

// Player controller (player_control.cpp): orig FUN_0013b0d0(0x50b620, chr)
struct PlayerControlCtx;
void playerControl(PlayerControl& ctl, Chr& chr);

}  // namespace baroque::game
