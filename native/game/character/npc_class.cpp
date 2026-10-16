// NPC character classes (see npc_class.hpp).
#include "game/character/npc_class.hpp"

#include "game/character/character.hpp"
#include "game/character/model_fx.hpp"
#include "game/dungeon.hpp"
#include "game/effects/exi_math.hpp"
#include "game/event/event_manager.hpp"
#include "game/event/event_services.hpp"
#include "game/flag_manager.hpp"
#include "game/game_world.hpp"
#include "game/hud/hud_api.hpp"
#include "game/items/item.hpp"
#include "game/items/item_world.hpp"
#include "game/objects/objects.hpp"
#include "game/player_move.hpp"
#include "game/scenes/game_scene.hpp"
#include "game/talk/talk_manager.hpp"
#include "platform/platform.hpp"

#include <cctype>
#include <cmath>
#include <cstring>
#include <memory>
#include <string>
#include <unordered_map>

namespace baroque::game {

namespace {

constexpr uint32_t id4(const char (&s)[5]) { return data::tag4(s); }

// The extra actor of a class (FUN_0012dd10 actor wrapper at +0x40 / +0x44): its model is a part of the body
// (Chr::model.parts[part], drawn on its joint), its animation list and combiner live here.
struct ExtraActor {
    int part = -1;
    std::vector<std::unique_ptr<alchemy::AnimClip>> clips;
    ActorAnim anim;
    std::vector<alchemy::Mat4> pose;
};

// Class fields +0x40.. (per class; see the comments of each class)
struct NpcData {
    uint32_t serial = 0;                // owner check (Chr::serial)
    ExtraActor head[2];                 // Npc / C03A / C12A: +0x40 head; C08: +0x40 / +0x44 heads
    // C03A (FUN_001b3b90): +0x48 mode, +0x4c timer, +0x50..+0x58 target, +0x5c target speed, +0x60 speed
    int mode = 0;
    int timer = 0;
    float target[3] = {};
    float speedTarget = 0.0f;
    float speed = 0.0f;
    // C12A (FUN_001b5400): +0x48 transform node of the weapon, +0x4c its joint attach
    int weaponPart = -1;
    // C18A (FUN_001b5b60): +0x40 joint modifier "HeadRote", +0x44 its matrix, +0x48 angle, +0x4c rate, +0x50 orbit
    // C27A (FUN_001b7030): +0x40 joint modifier "Battery", +0x48 joint, +0x4c player joint "Spine1", +0x50 target,
    //      +0x5c..+0x88 four angle triples (newest first), +0x8c direction
    int mod = -1;
    bool modEnabled = false;            // modifier +0x10
    alchemy::Mat4 modM = alchemy::Mat4::identity();   // modifier +0xc matrix (applied to the pose only when enabled)
    float angle = 0.0f;
    float rate = 0.0f;
    float orbit = 0.0f;
    int joint = -1;
    int playerJoint = -1;
    float tgt[3] = {};
    float hist[4][3] = {};
    float dir[3] = {};
};

std::unordered_map<const Chr*, NpcData> gData;

// J074 (battery shot) launch direction: the C27A owner's +0x8c..+0x94 (written by FUN_001b7160, c27Tick)
const bool gOwnerDirHook = (projectileHooks().ownerDirection = [](const Chr& owner, float out[3]) {
    if (owner.cls != ChrClass::NpcC27A) return false;
    const auto it = gData.find(&owner);
    if (it == gData.end() || it->second.serial != owner.serial) return false;
    for (int k = 0; k < 3; ++k) out[k] = it->second.dir[k];
    return true;
}, true);

NpcData& data(Chr& c) {
    NpcData& d = gData[&c];
    if (d.serial != c.serial) {
        d = NpcData{};
        d.serial = c.serial;
    }
    return d;
}

bool sameNameNoCase(const std::string& a, const char* b) {
    const size_t n = std::strlen(b);
    if (a.size() != n) return false;
    for (size_t i = 0; i < n; ++i)
        if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i]))) return false;
    return true;
}

// skeleton vt+0x70 (FUN_003f6990 + FUN_0023f068): bone index by name, case-insensitive
int jointOf(const ChrModel& m, const char* name) {
    const alchemy::Skeleton* sk = m.skeleton();
    if (sk == nullptr) return -1;
    for (size_t i = 0; i < sk->bones.size(); ++i)
        if (sameNameNoCase(sk->bones[i].name, name)) return static_cast<int>(i);
    platform::log(std::string("npc: joint ") + name + " not in skeleton " + sk->name);
    return -1;
}

int clipOf(const std::vector<std::unique_ptr<alchemy::AnimClip>>& clips, const char* name) {
    for (size_t i = 0; i < clips.size(); ++i)
        if (clips[i]->name == name) return static_cast<int>(i);
    return -1;
}

std::string idText(uint32_t id) {
    std::string s(4, ' ');
    for (int k = 0; k < 4; ++k) s[size_t(k)] = static_cast<char>((id >> (8 * k)) & 0xffu);
    return s;
}

// FUN_001099e0(actor, path, 1, 0x11) + joint attach FUN_003b1480 (+0x20 = joint) + FUN_003ded10 + FUN_003c7cf0:
// a separately animated actor shown on a joint of the body
bool loadExtraActor(Chr& c, ExtraActor& x, const std::string& rel, const char* joint) {
    ChrModelPart part;
    if (!chrLoadPart(part, characters().assetRoot / rel, "")) return false;
    part.attachBoneIndex = jointOf(c.model, joint);
    x.clips = alchemy::loadClips(*part.file, alchemy::primarySkeleton(*part.file), nullptr);
    x.anim.init(&x.clips, &part.model->skeleton());
    actorLoadDefaultAnim(x.anim, x.clips, *part.file);        // FUN_0010a4d0 (load callback): first animation
    c.model.parts.push_back(std::move(part));
    x.part = static_cast<int>(c.model.parts.size()) - 1;
    return true;
}

ExiMat toExi(const alchemy::Mat4& m) {
    ExiMat e;
    for (int i = 0; i < 16; ++i) e.m[i] = m.m[size_t(i)];
    return e;
}
alchemy::Mat4 fromExi(const ExiMat& e) {
    alchemy::Mat4 m;
    for (int i = 0; i < 16; ++i) m.m[size_t(i)] = e.m[i];
    return m;
}

float rand01(int n) { return static_cast<float>(gameWorld().rng.rand(static_cast<uint32_t>(n))); }   // FUN_002071e0(0x53eb20, n)

// orig FUN_00206ab0 (normalize in place)
void normalize3f(float v[3]) {
    const float sq = v[1] * v[1] + v[0] * v[0] + v[2] * v[2];
    if (sq == 0.0f) return;
    const float l = std::sqrt(sq);
    v[0] /= l;
    v[1] /= l;
    v[2] /= l;
}

// ---------------------------------------------------------------------------------------------------------------
// Npc (FUN_001b39f0)
// ---------------------------------------------------------------------------------------------------------------
// orig FUN_001b39f0 (work/decompiled/001b39f0.c): "DATA/CHR/%c%c%c/%c%c%c%c_HEAD.IGB" (0x462b40) on joint "Head"
// (0x462b68), active bit, FUN_001b3220
void npcActivate(Chr& c) {
    NpcData& d = data(c);
    const std::string id = idText(c.id);
    loadExtraActor(c, d.head[0], "DATA/CHR/" + id.substr(0, 3) + "/" + id + "_HEAD.IGB", "Head");
    c.flags24 |= 1u;
    npcTypeActivate(c);
}

// orig FUN_001b3b90 (work/decompiled/001b3b90.c): C03A, wander mode by floor
void c03Activate(Chr& c) {
    npcActivate(c);                                           // FUN_001b39f0
    NpcData& d = data(c);
    const int f = gameProgress().floor;                       // iRam0050a2a8
    if ((0x28 <= f && f <= 0x3b) || f == 0xd6 || f == 0x72) d.mode = 2;
    else if ((10 <= f && f <= 0x13) || f == 0x68) d.mode = 1;
    else d.mode = 0;
    d.timer = 0;
    d.speedTarget = 0.0f;
    d.speed = 0.0f;
    c.flags24 |= 1u;
}

// orig FUN_001b5190 (work/decompiled/001b5190.c): two head actors FUN_001b5030(id, i) ("DATA/CHR/C08/C08A_HEAD.IGB"
// 0x462bd0 / "..._HEAD2.IGB" 0x462bf0) on the joints of table 0x462c18 {"Head", "Head_B"}
void c08Activate(Chr& c) {
    NpcData& d = data(c);
    static const char* const kPath[2] = {"DATA/CHR/C08/C08A_HEAD.IGB", "DATA/CHR/C08/C08A_HEAD2.IGB"};
    static const char* const kJoint[2] = {"Head", "Head_B"};
    for (int i = 0; i < 2; ++i) loadExtraActor(c, d.head[i], kPath[i], kJoint[i]);
    c.flags24 |= 1u;
}

// orig FUN_001b5400 (work/decompiled/001b5400.c): C12A, head + "DATA/OBJ/OBJ007.IGB" (0x462c58) under a transform
// node on "Left_WEAPON00_Attach" (0x462c20); flag 0xe6: animation "M09" and the weapon scaled to 0.001
// (FUN_001b5680), else "M01" and scale 0.8 with two quarter turns (FUN_001b56c0)
void c12Activate(Chr& c) {
    npcActivate(c);                                           // FUN_001b39f0
    NpcData& d = data(c);
    ChrModelPart part;
    if (chrLoadPart(part, characters().assetRoot / "DATA/OBJ/OBJ007.IGB", "")) {
        part.attachBoneIndex = jointOf(c.model, "Left_WEAPON00_Attach");
        c.model.parts.push_back(std::move(part));
        d.weaponPart = static_cast<int>(c.model.parts.size()) - 1;
    }
    ExiMat t;
    const bool hidden = flags().test(0xe6);
    const int clip = clipOf(c.model.clips, hidden ? "M09" : "M01");   // 0x462c70 / 0x462c78
    if (clip >= 0) c.model.anim.set(clip);                    // FUN_0010a7f0 (missing name -> anim 0, UNVERIFIED)
    if (hidden) {
        const float s[3] = {0.001f, 0.001f, 0.001f};          // 0x3a83126f
        exiScaleMatrix(t, s);                                 // FUN_001b5680
    } else {
        const float s[3] = {0.8f, 0.8f, 0.8f};                // 0x3f4ccccd
        exiScaleMatrix(t, s);                                 // FUN_001b56c0
        const float ax[3] = {1.0f, 0.0f, 0.0f};
        const float az[3] = {0.0f, 0.0f, 1.0f};
        ExiMat r;
        exiAxisAngle(1.5707964f, r, ax);                      // FUN_0039e4e0(pi/2, m, (1,0,0)): m = m * R
        exiMul(t, t, r);
        exiAxisAngle(1.5707964f, r, az);                      // FUN_0039e4e0(pi/2, m, (0,0,1))
        exiMul(t, t, r);
    }
    if (d.weaponPart >= 0) c.model.parts[size_t(d.weaponPart)].local = fromExi(t);
    c.flags24 |= 1u;
}

// orig FUN_001b5b60 (work/decompiled/001b5b60.c): C18A, enabled joint modifier on "HeadRote" (0x462c80)
void c18Activate(Chr& c) {
    NpcData& d = data(c);
    BoneMod b;
    b.joint = jointOf(c.model, "HeadRote");
    c.model.boneMods.push_back(b);
    d.mod = static_cast<int>(c.model.boneMods.size()) - 1;
    d.modEnabled = true;                                      // +0x10 = 1
    d.angle = 0.0f;
    d.rate = 0.0f;
    c.flags24 |= 1u;
}

// orig FUN_001b7030 (work/decompiled/001b7030.c): C27A, disabled joint modifier on "Battery" (0x462d88), the
// player's joint "Spine1" (0x462d90), angle history cleared
void c27Activate(Chr& c) {
    NpcData& d = data(c);
    d.joint = jointOf(c.model, "Battery");
    BoneMod b;
    b.joint = d.joint;
    c.model.boneMods.push_back(b);
    d.mod = static_cast<int>(c.model.boneMods.size()) - 1;
    d.modEnabled = false;                                     // +0x10 = 0: the joint keeps its animated pose (the
                                                              // list entry stays identity; the tick's matrix is kept
                                                              // in modM). UNVERIFIED: no code enabling it was found
    if (Chr* pl = characters().player()) d.playerJoint = jointOf(pl->model, "Spine1");
    for (auto& h : d.hist)
        for (float& v : h) v = 0.0f;
    c.flags24 |= 1u;
}

// ---------------------------------------------------------------------------------------------------------------
// C03A tick (FUN_001b3d80)
// ---------------------------------------------------------------------------------------------------------------
bool actionIsIdleOrWalk(const char* a) {
    // first word of the action name against "WT01" "FN01" "FN03" "MV01" "MV02" (0x462b78..0x462b98)
    return std::strncmp(a, "WT01", 4) == 0 || std::strncmp(a, "FN01", 4) == 0 || std::strncmp(a, "FN03", 4) == 0 ||
           std::strncmp(a, "MV01", 4) == 0 || std::strncmp(a, "MV02", 4) == 0;
}
void setAction(Chr& c, const char* cur, const char* name) {
    if (cur != nullptr && std::strncmp(name, cur, 8) == 0) return;   // FUN_0023fae8(name, cur, 8)
    c.act.start(name);                                        // FUN_00143a40(act, name)
}
void speedStep(NpcData& d) {
    if (d.speed < d.speedTarget) {
        d.speed = d.speed + 0.1f;
        if (d.speedTarget < d.speed) d.speed = d.speedTarget;
    } else if (d.speedTarget < d.speed) {
        d.speed = d.speed - 0.1f;
        if (d.speed < d.speedTarget) d.speed = d.speedTarget;
    }
}

// orig FUN_001b3d80 (work/decompiled/001b3d80.c)
void c03Tick(Chr& c) {
    NpcData& d = data(c);
    Chr* pl = characters().player();
    if (d.mode == 0) return;
    int room = -1;
    if (c.cell != nullptr && (*c.cell & 0xf000u) == 0x1000u) room = static_cast<int>(*c.cell & 0xfu);
    if (pl == nullptr) {
        d.timer = 0;
        d.speed = 0.0f;
        return;
    }
    const float dz = pl->xf.m.pos[2] - c.xf.m.pos[2];
    const float dy = pl->xf.m.pos[1] - c.xf.m.pos[1];
    const float dx = pl->xf.m.pos[0] - c.xf.m.pos[0];
    if (talkManager().actorById(talkManager().speaker) == &c) {   // FUN_001a9810(0x53c6e0, uRam0053c6f8) == this
        d.timer = 0;
        d.speed = 0.0f;
        return;
    }
    const Dungeon* dg = gameWorld().dungeon;
    if (eventActive() || room == -1 || dg == nullptr || room >= dg->roomCount() || dg->room(room).rdt.roomId != 0x1e) {
        d.timer = 0;
        d.speed = 0.0f;
        return;
    }
    const char* act = c.act.currentName();                   // FUN_00143d30(act)
    const float ox = eventMapOrigin(room, 0);                 // FUN_0011e6c0
    const float oz = eventMapOrigin(room, 2);                 // FUN_0011e740
    if (d.mode == 1) {
        const float cz = oz + 1600.0f;
        const float cx = ox + 1600.0f;
        auto distToCentre = [&](const Vec3f& p) {
            const float a = 0.0f - p[1], b = cx - p[0], e = cz - p[2];
            const float s = e * e + b * b + a * a;
            return s == 0.0f ? 0.0f : std::sqrt(s);
        };
        const float plDist = distToCentre(pl->xf.m.pos);
        const float npcDist = distToCentre(c.xf.m.pos);
        if (plDist < 420.0f) {
            d.timer = 0x1e;
            const float s = dz * dz + dx * dx + dy * dy;
            const float dist = s == 0.0f ? 0.0f : std::sqrt(s);
            d.target[0] = pl->xf.m.pos[0];
            d.target[1] = 0.0f;
            d.target[2] = pl->xf.m.pos[2];
            if (dist < 250.0f) d.speedTarget = 0.0f;
            else if (dist < 300.0f) d.speedTarget = 2.0f;
            else if (dist < 350.0f) d.speedTarget = 3.0f;
            else d.speedTarget = 4.0f;
        }
        if (d.timer == 0) {
            if (c.cell != nullptr) {
                const float r1 = rand01(0x8000) / 32768.0f;
                float v[3] = {rand01(0x8000) / 32768.0f, 0.0f, r1};
                normalize3f(v);                               // FUN_00206ab0
                const float r = (rand01(0x8000) / 32768.0f) * 120.0f;
                v[2] *= r;
                v[0] *= r;
                d.target[0] = cx + v[0];
                d.target[1] = 0.0f;
                d.target[2] = cz + v[2];
            }
            d.timer = static_cast<int>(gameWorld().rng.rand(0x3c)) + 0x3c;
            d.speedTarget = (rand01(0x8000) / 32768.0f) * 3.0f + 1.0f;
        }
        if (static_cast<uint32_t>(d.timer) < 10u) {
            d.speedTarget *= 0.95f;
            d.speed *= 0.95f;
        }
        d.timer = d.timer - 1;
        float dir[3] = {d.target[0] - c.xf.m.pos[0], 0.0f, d.target[2] - c.xf.m.pos[2]};
        normalize3f(dir);
        turnToward(d.speed * 0.5f + 1.5f, c.xf.m, Vec3f{dir[0], dir[1], dir[2]}, true);   // FUN_00128fc0(.., this, dir, 1)
        speedStep(d);
        if (d.speed != 0.0f)
            for (int k = 0; k < 3; ++k) c.xf.m.pos[size_t(k)] += c.xf.m.fwd[size_t(k)] * d.speed;
        if (170.0f < npcDist) {                               // kept within 170 (DAT_00462b70) of the centre
            const float y = c.xf.m.pos[1];
            float v[3] = {c.xf.m.pos[0] - cx, y - 0.0f, c.xf.m.pos[2] - cz};
            normalize3f(v);
            c.xf.m.pos = {cx + v[0] * 170.0f, y, cz + v[2] * 170.0f};
        }
        if (act != nullptr && !actionIsIdleOrWalk(act)) return;
        setAction(c, act, d.speed != 0.0f ? "MV01_03A" : "WT01_03A");   // 0x462ba0 / 0x462bb0
        return;
    }
    // mode 2
    const float s = dz * dz + dx * dx + dy * dy;
    const float dist = s == 0.0f ? 0.0f : std::sqrt(s);
    if (gameProgress().floor == 0xd6) {
        const float px = pl->xf.m.pos[0];
        const float pz = pl->xf.m.pos[2];
        if (dist < 600.0f && ox + 900.0f < px && px < ox + 1800.0f && oz + 600.0f < pz && pz < oz + 1800.0f) {
            d.timer = 0x1e;
            d.target[0] = px;
            d.target[1] = 0.0f;
            d.target[2] = pz;
            if (dist < 150.0f) d.speedTarget = 0.0f;
            else if (dist < 200.0f) d.speedTarget = 2.0f;
            else if (dist < 400.0f) d.speedTarget = 3.0f;
            else d.speedTarget = 4.0f;
        }
    } else if (dist < 300.0f) {
        d.timer = 0;
        d.speed = 0.0f;
        return;
    }
    if (d.timer == 0) {
        if (c.cell != nullptr) {
            d.target[0] = (rand01(0x8000) / 32768.0f) * 300.0f + 1200.0f + ox;
            d.target[1] = 0.0f;
            d.target[2] = (rand01(0x8000) / 32768.0f) * 600.0f + 900.0f + oz;
        }
        d.timer = static_cast<int>(gameWorld().rng.rand(0x3c)) + 0x3c;
        d.speedTarget = (rand01(0x8000) / 32768.0f) * 6.0f + 1.0f;
    }
    if (static_cast<uint32_t>(d.timer) < 10u) {
        d.speedTarget *= 0.95f;
        d.speed *= 0.95f;
    }
    d.timer = d.timer - 1;
    float dir[3] = {d.target[0] - c.xf.m.pos[0], 0.0f, d.target[2] - c.xf.m.pos[2]};
    normalize3f(dir);
    turnToward(d.speed * 0.5f + 1.5f, c.xf.m, Vec3f{dir[0], dir[1], dir[2]}, true);
    speedStep(d);
    if (d.speed != 0.0f)
        for (int k = 0; k < 3; ++k) c.xf.m.pos[size_t(k)] += c.xf.m.fwd[size_t(k)] * d.speed;
    float x = c.xf.m.pos[0];
    float z = c.xf.m.pos[2];
    if (x < ox + 1050.0f) x = ox + 1050.0f;
    else if (ox + 1650.0f < x) x = ox + 1650.0f;
    if (z < oz + 750.0f) z = oz + 750.0f;
    else if (oz + 1650.0f < z) z = oz + 1650.0f;
    c.xf.m.pos[0] = x;
    c.xf.m.pos[2] = z;
    if (act != nullptr && !actionIsIdleOrWalk(act)) return;
    if (gameProgress().floor != 0xd7) setAction(c, act, "MV02_03A");   // 0x462bc0
    else setAction(c, act, d.speed == 0.0f ? "WT01_03A" : "MV01_03A");
}

// ---------------------------------------------------------------------------------------------------------------
// C18A tick (FUN_001b5c10): HeadRote spin, the carried item's curse / conversion, three orbiting items 0x53d7d0
// ---------------------------------------------------------------------------------------------------------------
Item* gOrbit[3] = {};    // 0x53d7d0 [3]

Item* orbitFirst() { return gOrbit[0]; }                      // FUN_001b57a0
int orbitCount12() {                                          // FUN_001b5760: slots 1 and 2 in use
    int n = 0;
    for (int i = 1; i < 3; ++i)
        if (gOrbit[i] != nullptr) ++n;
    return n;
}
void orbitFree(int i) {                                       // FUN_001b57b0
    if (gOrbit[i] != nullptr) {
        gameAdapter().freeItem(gOrbit[i]);                    // FUN_001b7570
        gOrbit[i] = nullptr;
    }
}
// FUN_001d30c0 / FUN_001d2f30 = Dungeon::pickItem / pickItemCategory (game/dungeon.hpp)
uint32_t pickItem(int category) {                             // FUN_001d30c0
    Dungeon* dg = gameWorld().dungeon;
    return dg != nullptr ? dg->pickItem(category) : 0u;
}
int pickItemCategory() {                                      // FUN_001d2f30
    Dungeon* dg = gameWorld().dungeon;
    return dg != nullptr ? dg->pickItemCategory() : -1;
}

// orig FUN_001b6880 (work/decompiled/001b6880.c; table 0x499110 {a, b-or-category, result} x 16 written by the static
// initialiser 0x4c8350, emulated): converts `item` by the recipe of item + orbit slot 0
int c18Convert(Item& item) {
    struct Recipe { uint32_t a, b, r; };
    static const Recipe kRecipes[16] = {
        {id4("MT03"), id4("MT03"), id4("MEAT")}, {id4("HT02"), id4("HT02"), id4("HERT")},
        {id4("LQ08"), id4("LQ09"), id4("LQ10")}, {id4("LQ10"), id4("LQ10"), id4("LQ11")},
        {id4("LQ01"), id4("LQ02"), id4("LQ03")}, {id4("WM01"), id4("SWRD"), id4("SD04")},
        {id4("WM02"), id4("SWRD"), id4("SD05")}, {id4("WM03"), id4("SWRD"), id4("SD06")},
        {id4("WM01"), id4("COAT"), id4("CT03")}, {id4("WM02"), id4("COAT"), id4("CT04")},
        {id4("WM03"), id4("COAT"), id4("CT05")}, {id4("SD17"), id4("ITEM"), id4("SWRD")},
        {id4("CT12"), id4("ITEM"), id4("COAT")}, {id4("BX00"), id4("EQUI"), id4("BX16")},
        {id4("BX00"), id4("BOXX"), id4("BX09")}, {id4("BX00"), id4("TRAP"), 0u}};   // row 15 result never written
    Item* o = orbitFirst();
    if (o == nullptr) return 1;   // UNVERIFIED: the original dereferences slot 0 (the caller checks it first)
    for (int i = 0; i < 16; ++i) {
        const Recipe& r = kRecipes[i];
        bool mine = false, other = false;   // bVar1 (item is of b), bVar2 (orbit item is of b)
        if (r.b == id4("SWRD")) { mine = item.cls == 0; other = o->cls == 0; }
        else if (r.b == id4("COAT")) { mine = item.cls == 1; other = o->cls == 1; }
        else if (r.b == id4("EQUI")) {
            mine = item.cls == 0 || item.cls == 1 || item.cls == 2;
            other = o->cls == 0 || o->cls == 1 || o->cls == 2;
        } else if (r.b == id4("BOXX")) { mine = item.cls == 5; other = o->cls == 5; }
        else if (r.b == id4("TRAP")) { mine = item.cls == 9; other = o->cls == 9; }
        else if (r.b == id4("ITEM")) { mine = true; other = true; }
        else { mine = r.b == item.id; other = r.b == o->id; }
        if ((r.a == item.id && other) || (r.a == o->id && mine)) {
            ItemSystem& sys = itemSystem();
            uint32_t res;
            if (r.r == id4("MEAT")) res = pickItem(4);
            else if (r.r == id4("HERT")) res = pickItem(3);
            else if (r.r == id4("SWRD")) res = pickItem(0);
            else if (r.r == id4("COAT")) res = pickItem(1);
            else {
                changeItemId(item, r.r, sys);                 // FUN_001d4860(item, result)
                return 1;
            }
            if (res != 0) changeItemId(item, res, sys);
            return 1;
        }
    }
    // no recipe: a random item of a random category (FUN_001d2f30 -> FUN_001d30c0)
    const uint32_t res = pickItem(pickItemCategory());
    if (res != 0) changeItemId(item, res, itemSystem());
    return 1;
}

// orig FUN_001b5c10 (work/decompiled/001b5c10.c; the HT02 / MT03 conversion arguments from the disasm 0x1b6030 /
// 0x1b6090)
void c18Tick(Chr& c) {
    NpcData& d = data(c);
    if (d.mod >= 0) {
        const char* a = c.act.currentName();                 // FUN_00143d30(act, -1)
        float goal = 4.0f;
        if (a != nullptr) {
            if (std::strncmp(a, "DM01_18A", 8) == 0) goal = 48.0f;          // 0x462c98
            else if (std::strncmp(a, "DD01_18A", 8) == 0) goal = 0.0f;      // 0x462ca8: rate -= rate / 4
            else if (std::strncmp(a, "TK01_18A", 8) == 0) goal = 8.0f;      // 0x462cb8
        }
        if (a != nullptr && std::strncmp(a, "DD01_18A", 8) == 0) d.rate = d.rate - d.rate / 4.0f;
        else d.rate = d.rate - (d.rate - goal) / 16.0f;
        d.angle = d.angle + d.rate;
        if (180.0f < d.angle) d.angle = d.angle - 360.0f;
        ExiMat m;
        exiEuler(0.0f, d.angle * 0.017453292f, 0.0f, m);     // FUN_0039de50(0, angle, 0, modifier matrix)
        c.model.boneMods[size_t(d.mod)].m = fromExi(m);
    }
    Item* item = static_cast<Item*>(c.status.block.carried); // FUN_001395c0
    if (item != nullptr) {
        ItemSystem& sys = itemSystem();
        int res = -1;   // lVar17
        const int cls = item->cls;
        if (cls == 1) {
            if (item->defMod < 0x1e) {
                if (item->defMod < 0) {
                    addDefMod(*item, -item->defMod);          // FUN_001d5030(item, -mod)
                    res = 10;
                }
            } else {
                res = 10;
            }
        } else if (cls == 0) {
            if (item->atkMod < 0x1e) {
                if (item->atkMod < 0) {
                    addAtkMod(*item, -item->atkMod);          // FUN_001d4f70(item, -mod)
                    res = 10;
                }
            } else {
                res = 10;
            }
        } else if (cls == 10) {
            if (item->id == id4("BN20")) {                    // 0x462d30
                changeItemId(*item, id4("BX25"), sys);        // 0x462d38
                res = 10;
            } else if (item->id == id4("BN19")) {             // 0x462d40
                const int n = orbitCount12();
                res = 2;
                if (n == 2) {
                    changeItemId(*item, id4("BX25"), sys);
                    orbitFree(1);
                    orbitFree(2);
                    res = 10;
                } else if (n != 1) {
                    res = n == 0 ? 1 : -1;
                }
            }
        } else if (cls == 0xd) {
            const uint32_t id = item->id;
            if (id == id4("IS00")) { changeItemId(*item, id4("HT00"), sys); res = 10; }
            else if (id == id4("IS02")) { changeItemId(*item, id4("LQ21"), sys); res = 10; }
            else if (id == id4("IS03")) { changeItemId(*item, id4("TL22"), sys); res = 10; }
            else if (id == id4("IS04")) {
                if (!flags().test(0x1e)) { changeItemId(*item, id4("GN00"), sys); res = 10; }
            } else if (id == id4("IS05")) { changeItemId(*item, id4("BN22"), sys); res = 10; }
            else if (id == id4("IS06")) {
                const uint32_t r = pickItem(5);
                if (r != 0) changeItemId(*item, r, sys);
                res = 10;
            }
        } else if (cls == 4 || cls == 3) {
            if ((item->flags & 4u) == 0) {
                if (cls == 3 && item->id != id4("HT02")) { changeItemId(*item, id4("HT02"), sys); res = 10; }
                else if (cls == 4 && item->id != id4("MT03")) { changeItemId(*item, id4("MT03"), sys); res = 10; }
            } else {
                item->flags &= 0xfffffffbu;
                res = 10;
            }
        }
        // LAB_001b655c
        if (res < 0) {
            if (orbitFirst() == nullptr) res = 0;
            else if (c18Convert(*item) != 0) {               // FUN_001b6880
                orbitFree(0);
                res = 10;
            }
        }
        if (res == 10) {
            float dir[3];
            if (Chr* pl = characters().player()) {
                dir[2] = pl->xf.m.pos[2] - c.xf.m.pos[2];
                dir[1] = pl->xf.m.pos[1] - c.xf.m.pos[1];
                dir[0] = pl->xf.m.pos[0] - c.xf.m.pos[0];
                normalize3f(dir);
                dir[1] = 2.0f;
                normalize3f(dir);
            } else {
                dir[0] = c.xf.m.fwd[0];
                dir[1] = 2.0f;
                dir[2] = c.xf.m.fwd[2];
                normalize3f(dir);
            }
            for (float& v : dir) v *= 16.0f;                  // DAT_00462c90
            const Vec3 pos{c.xf.m.pos[0], c.xf.m.pos[1] + 100.0f, c.xf.m.pos[2]};
            // FUN_001d5780(item, pos, dir, this). UNVERIFIED: the thrower (this NPC) is not passed as a
            // CharacterAccess (no adapter for NPCs in the items module)
            launchItem(*item, pos, Vec3{dir[0], dir[1], dir[2]}, nullptr);
            c.status.block.carried = nullptr;                 // FUN_00139540
            counters().add(0x18, 1);                          // FUN_00179570(0x53c5c0, 0x18, 1)
        } else {
            if (res >= 0) {
                // the item joins the orbit slot `res`: its state slot (+0x78) is cleared (0x1b66c0..0x1b6700)
                ItemStateSlot& ss = item->state;
                if ((ss.factory == 0 && ss.pending == 0) || ss.param0 < 2) {   // +0x14, +0x18, +0xc
                    ss.pending = -1;                          // +0x18
                    // +8 = 0: the slot's owner argument (not modelled by ItemStateSlot)
                    ss.param0 = 1;                            // +0xc
                    ss.param1 = 1;                            // +0x10
                }
                gameAdapter().itemEnterWorld(*item);          // FUN_001d3a30(item)
                gOrbit[res] = item;
            }
            c.status.block.carried = nullptr;                 // FUN_00139540
        }
    }
    // LAB_001b6734: the orbit (three items 120 degrees apart, radius 50 at height 100)
    d.orbit = d.orbit + 10.0f;
    if (360.0f <= d.orbit) d.orbit = d.orbit - 360.0f;
    float a = d.orbit;
    for (int i = 0; i < 3; ++i) {
        Item* it = gOrbit[i];
        if (it == nullptr) continue;
        gameAdapter().itemEnterWorld(*it);                    // FUN_001d3a30(item)
        float o[3];
        const float base[3] = {0.0f, 100.0f, 50.0f};
        const float s = std::sin((a * 3.1415927f) / 180.0f), co = std::cos((a * 3.1415927f) / 180.0f);
        o[0] = base[0] * co + base[2] * s;                    // FUN_00206ca0(angle, out, (0, 100, 50))
        o[1] = base[1];
        o[2] = base[0] * -s + base[2] * co;
        it->pos = Vec3{c.xf.m.pos[0] + o[0], c.xf.m.pos[1] + o[1], c.xf.m.pos[2] + o[2]};
        // FUN_00109d40(model, pos, scale, fwd, up, right): the item model published at its transform
        it->modelXf.pos = it->pos;
        it->modelXf.right = it->right;
        it->modelXf.up = it->up;
        it->modelXf.fwd = it->fwd;
        it->modelXf.scale = it->scale;
        it->modelXf.valid = true;
        a = a + 120.0f;
        if (360.0f <= a) a = a - 360.0f;
    }
}

// ---------------------------------------------------------------------------------------------------------------
// C27A tick (FUN_001b7160): the "Battery" joint turns toward the player's "Spine1", limited to +-90 degrees per
// axis (table 0x4991d0), smoothed by 1/16 and averaged over the last four ticks
// ---------------------------------------------------------------------------------------------------------------
void c27Tick(Chr& c) {
    NpcData& d = data(c);
    Chr* pl = characters().player();
    if (d.mod < 0 || pl == nullptr || d.joint < 0) return;
    const std::vector<alchemy::Mat4>& pose = chrTickPose(c);   // actor +0x30 joint matrices (vt+0x6c / +0x24)
    if (static_cast<size_t>(d.joint) >= pose.size()) return;
    ExiMat j = toExi(pose[size_t(d.joint)]);                  // FUN_0039d860(m90, joints + joint*0x40)
    ExiMat mod = toExi(d.modM);                               // FUN_0039d860(md0, modifier +0x20)
    exiInvert(mod, mod);                                      // FUN_0039f1d0(r, md0, md0)
    exiPreMul(j, mod);                                        // FUN_0039d640(m90, md0): m90 = md0 * m90
    const ExiMat actor = toExi(chrActorMatrix(c));
    exiMul(j, j, actor);                                      // FUN_0039d450(m90, m90, actor matrix)
    float self[3];
    exiGetTranslation(j, self);                               // FUN_0039d9a0(m90, afStack_18)
    const std::vector<alchemy::Mat4>& pp = chrTickPose(*pl);
    if (d.playerJoint >= 0 && static_cast<size_t>(d.playerJoint) < pp.size()) {
        ExiMat pj = toExi(pp[size_t(d.playerJoint)]);
        exiMul(pj, pj, toExi(chrActorMatrix(*pl)));
        exiGetTranslation(pj, d.tgt);                         // +0x50
    }
    float v[3] = {d.tgt[0] - self[0], 0.0f, d.tgt[2] - self[2]};
    d.dir[0] = v[0];
    d.dir[1] = 0.0f;
    d.dir[2] = v[2];
    // orig FUN_00206dc0(out, v): yaw = atan2(v.x, v.z); v rotated by -yaw (FUN_00206ca0); out = (-atan2(r.y, r.z), yaw, 0)
    const float yaw = std::atan2(v[0], v[2]);
    const float sy = std::sin(-yaw), cy = std::cos(-yaw);
    const float rz = v[0] * -sy + v[2] * cy;
    const float look[3] = {-std::atan2(v[1], rz), yaw, 0.0f};
    exiInvert(j, j);                                          // FUN_0039f1d0(r, m90, m90)
    float e[3];
    exiExtractEuler(j, &e[0], &e[1], &e[2]);                  // FUN_0039e160
    exiEuler(e[0], e[1], e[2], j);                            // FUN_0039de50(e, m90)
    ExiMat l;
    exiEuler(look[0], look[1], look[2], l);                   // FUN_0039de50(look, md0)
    exiPreMul(j, l);                                          // FUN_0039d640(m90, md0)
    exiExtractEuler(j, &e[0], &e[1], &e[2]);
    static const int kLimits[3][2] = {{-90, 90}, {-90, 90}, {-90, 90}};   // 0x4991d0
    int clamped = 0;
    for (int k = 0; k < 3; ++k) {
        const float lo = (static_cast<float>(kLimits[k][0]) * 3.1415927f) / 180.0f;
        if (e[k] < lo) {
            e[k] = lo;
            ++clamped;
        } else {
            const float hi = (static_cast<float>(kLimits[k][1]) * 3.1415927f) / 180.0f;
            if (hi < e[k]) {
                e[k] = hi;
                ++clamped;
            }
        }
    }
    for (int k = 0; k < 3; ++k) {
        d.hist[3][k] = d.hist[2][k];                          // +0x80 = +0x74
        d.hist[2][k] = d.hist[1][k];                          // +0x74 = +0x68
        d.hist[1][k] = d.hist[0][k];                          // +0x68 = +0x5c
    }
    for (int k = 0; k < 3; ++k) {
        if (clamped == 0) d.hist[0][k] = d.hist[0][k] - (d.hist[0][k] - e[k]) * 0.0625f;
        else d.hist[0][k] = d.hist[0][k] - d.hist[0][k] * 0.0625f;
    }
    float avg[3];
    for (int k = 0; k < 3; ++k) avg[k] = (d.hist[0][k] + d.hist[1][k] + d.hist[2][k] + d.hist[3][k]) * 0.25f;
    ExiMat out;
    exiEuler(avg[0], avg[1], avg[2], out);                    // FUN_0039de50(avg, modifier +0x20)
    d.modM = fromExi(out);
    if (d.modEnabled) c.model.boneMods[size_t(d.mod)].m = d.modM;
}

// PORT: the extra actors' combiners are updated and posed once per tick (the original's 3-D pass FUN_00108d20
// updates every actor wrapper); their models are drawn by chrDraw on their joints.
void poseExtraActors(Chr& c) {
    NpcData& d = data(c);
    for (ExtraActor& x : d.head) {
        if (x.part < 0 || static_cast<size_t>(x.part) >= c.model.parts.size()) continue;
        x.anim.update();
        x.anim.pose(x.pose);
        ChrModelPart& p = c.model.parts[size_t(x.part)];
        if (!x.pose.empty() && p.model) p.model->setPose(x.pose);
    }
}

}  // namespace

// PORT: presentation read-back of poseExtraActors' actors (see npc_class.hpp)
const ActorAnim* npcExtraActorAnim(const Chr& c, int partIndex) {
    if (!npcIsClass(c) || partIndex < 0) return nullptr;
    const auto it = gData.find(&c);
    if (it == gData.end() || it->second.serial != c.serial) return nullptr;
    for (const ExtraActor& x : it->second.head)
        if (x.part == partIndex) return &x.anim;
    return nullptr;
}

// orig FUN_001b56c0 (show: scale 0.8 + two quarter turns about x then z) / FUN_001b5680 (hide: scale 0.001) on the
// C12A box transform node +0x48 (ACT ShowBox FUN_00149b90 / HideBox FUN_00149c00)
void npcC12Box(Chr& c, bool show) {
    if (c.cls != ChrClass::NpcC12A) return;
    NpcData& d = data(c);
    ExiMat t;
    if (!show) {
        const float s[3] = {0.001f, 0.001f, 0.001f};          // 0x3a83126f
        exiScaleMatrix(t, s);
    } else {
        const float s[3] = {0.8f, 0.8f, 0.8f};                // 0x3f4ccccd
        exiScaleMatrix(t, s);
        const float ax[3] = {1.0f, 0.0f, 0.0f};
        const float az[3] = {0.0f, 0.0f, 1.0f};
        ExiMat r;
        exiAxisAngle(1.5707964f, r, ax);                      // FUN_0039e4e0(0x3fc90fdb, m, (1,0,0)): m = m * R
        exiMul(t, t, r);
        exiAxisAngle(1.5707964f, r, az);                      // FUN_0039e4e0(0x3fc90fdb, m, (0,0,1))
        exiMul(t, t, r);
    }
    if (d.weaponPart >= 0 && static_cast<size_t>(d.weaponPart) < c.model.parts.size())
        c.model.parts[size_t(d.weaponPart)].local = fromExi(t);
}

bool npcIsClass(const Chr& c) {
    switch (c.cls) {
    case ChrClass::Npc: case ChrClass::NpcC03A: case ChrClass::NpcC08: case ChrClass::NpcC12A:
    case ChrClass::NpcC18A: case ChrClass::NpcC27A: return true;
    default: return false;
    }
}

// orig FUN_001b3220 (work/decompiled/001b3220.c): type-1 activation by id
void npcTypeActivate(Chr& c) {
    const uint32_t id = c.id;
    const uint32_t id3 = id & 0xffffffu;
    // FUN_0010a0a0(model, mode, frames, a, b) translucency fades (model_fx.hpp); FUN_00109f70(model, 1) model shadow
    // flag "never" (the port keeps the flag; it has no shadow pass).
    auto fade = [&c](uint32_t mode, int frames) {
        const float col[4] = {0.8f, 0.8f, 0.8f, 0.7f};        // 0x3f4ccccd x3, 0x3f333333
        modelFadeStart(c.model, mode, frames, col, col);
    };
    auto shadowOff = [&c] { modelShadowOff(c.model, true); };
    if (id == id4("C22B")) {                                   // 0x462ad8
        fade(3u, 2);
        shadowOff();
    } else if (id3 == (id4("C05\0") & 0xffffffu)) {           // 0x462ae0
        shadowOff();
    } else if (id == id4("C19B") || id == id4("C19C") || id == id4("C19D")) {   // 0x462ae8 / 0x462af0 / 0x462af8
        shadowOff();
    }
    if (id == id4("C00B")) {                                   // 0x462b00
        shadowOff();
    } else if (id == id4("C04A")) {                           // 0x462b08
        fade(0x43u, 0);
        shadowOff();
    } else if (id == id4("C04B")) {                           // 0x462b10
        shadowOff();
    } else if (id == id4("C25B")) {                           // 0x462b18
        fade(3u, 0);
        shadowOff();
    } else if (id == id4("C26B")) {                           // 0x462b20
        fade(3u, 0);
        shadowOff();
    } else if (id == id4("C19A") || id == id4("C19D")) {      // 0x462b28 / 0x462af8
        shadowOff();
        // the TypeId state (factory LAB_001293e0) requested with priority 0
        StateMachine& sm = c.sm;
        if ((sm.curFactory == StateId::None && sm.next == StateId::None) || sm.priority < 1) {
            sm.next = StateId::TypeId;
            sm.popRequested = false;
            sm.arg = 0;                                        // +8 = this
            sm.priority = 0;
            sm.priorityCopy = 0;
        }
    } else if (id3 == (id4("C23\0") & 0xffffffu)) {
        // the Collector: every gun (class 0xc) of the database 0x53dc00 leaves it; after the game is cleared (flag
        // 0x1e) it is destroyed, else it lies 100 units (DAT_00462ad0) in front of him
        CollectorBag& bag = collectorBag();
        ItemSystem& sys = itemSystem();
        for (uint32_t i = 0; i < 0x32; ++i) {
            Item* it = bag.get(static_cast<int>(i));           // FUN_001daff0
            if (it != nullptr && it->cls == 0xc) {
                Item* t = bag.take(static_cast<int>(i), sys);  // FUN_001daeb0
                if (t != nullptr) {
                    if (t->cls == 0xc && flags().test(0x1e)) {
                        gameAdapter().freeItem(t);             // FUN_001b7570
                    } else {
                        t->pos = Vec3{c.xf.m.fwd[0] * 100.0f + c.xf.m.pos[0] + 0.0f,
                                      c.xf.m.fwd[1] * 100.0f + c.xf.m.pos[1] + 0.0f,
                                      c.xf.m.fwd[2] * 100.0f + c.xf.m.pos[2] + 0.0f};
                        dropItemAtActor(*t, -1.0f, nullptr, sys);   // FUN_001d5410(-1.0, item, 0)
                    }
                }
                i = 0xffffffffu;                               // restart the walk
            }
        }
    }
}

bool npcClassActivate(Chr& c) {
    switch (c.cls) {
    case ChrClass::Npc: npcActivate(c); return true;           // FUN_001b39f0
    case ChrClass::NpcC03A: c03Activate(c); return true;       // FUN_001b3b90
    case ChrClass::NpcC08: c08Activate(c); return true;        // FUN_001b5190
    case ChrClass::NpcC12A: c12Activate(c); return true;       // FUN_001b5400
    case ChrClass::NpcC18A: c18Activate(c); return true;       // FUN_001b5b60
    case ChrClass::NpcC27A: c27Activate(c); return true;       // FUN_001b7030
    default: return false;
    }
}

void npcClassTick(Chr& c) {
    switch (c.cls) {
    case ChrClass::NpcC03A: c03Tick(c); break;                 // FUN_001b3d80
    case ChrClass::NpcC18A: c18Tick(c); break;                 // FUN_001b5c10
    case ChrClass::NpcC27A: c27Tick(c); break;                 // FUN_001b7160
    default: break;                                            // FUN_00129c50 (empty)
    }
    if (npcIsClass(c)) poseExtraActors(c);
}

void npcClassDestroy(Chr& c) {
    if (c.cls == ChrClass::NpcC18A) {
        // orig FUN_001b59e0: before the Neuro Tower is cleared (flag 5) the orbiting items are freed (FUN_001b5800)
        // and flags 0x104..0x107, 0x10a, 0x10b cleared; else their models are released (FUN_001b5840 -> FUN_001d3b30)
        if (!flags().test(5)) {
            for (int i = 0; i < 3; ++i) orbitFree(i);
            for (uint32_t f : {0x104u, 0x105u, 0x106u, 0x107u, 0x10au, 0x10bu}) flags().clear(f);
        } else {
            // UNVERIFIED: FUN_001d3b30 (item model release) is internal to the items module; the items stay
            // referenced by 0x53d7d0 as in the original
        }
    }
    gData.erase(&c);
}

bool npcPartAnim(Chr& c, int part, const char* name, float blend, float speed) {
    int slot = -1;
    switch (c.cls) {
    case ChrClass::Npc: case ChrClass::NpcC03A: case ChrClass::NpcC12A:
        if (part == 1) slot = 0;                                // vt+0x28 FUN_00130800 (+0x40); vt+0x2c FUN_0012d3f0 = 0
        break;
    case ChrClass::NpcC08:
        slot = part == 1 ? 0 : 1;                               // FUN_001308f0 (+0x40) / FUN_00130900 (+0x44)
        break;
    default:
        break;                                                  // FUN_0012d3e0 / FUN_0012d3f0: no actor
    }
    if (slot < 0) return false;
    ExtraActor& x = data(c).head[slot];
    if (x.part < 0) return false;
    const int clip = clipOf(x.clips, name);
    // UNVERIFIED: a name missing from the list hands FUN_0010a8e0 a null animation; the port skips the change
    if (clip >= 0) x.anim.change(blend, speed, clip, 0);
    return true;
}

}  // namespace baroque::game
