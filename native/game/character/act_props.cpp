// ACT presentation / prop commands (see act_props.hpp).
#include "game/character/act_props.hpp"

#include "alchemy/scene_render.hpp"
#include "game/act_runner.hpp"
#include "game/character/act_trail.hpp"
#include "game/character/character.hpp"
#include "game/character/npc_class.hpp"
#include "game/character/player_control.hpp"
#include "game/effects/exi_math.hpp"
#include "game/game_world.hpp"
#include "game/hud/hud_api.hpp"
#include "game/items/chr_adapter.hpp"
#include "game/items/game_adapter.hpp"
#include "game/items/inventory.hpp"
#include "game/items/item.hpp"
#include "game/items/item_world.hpp"
#include "platform/platform.hpp"

#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

namespace baroque::game {

namespace {

constexpr float kPi = 3.1415927f;

uint32_t tag4(const std::string& s) {
    uint32_t v = 0;
    for (size_t i = 0; i < 4 && i < s.size(); ++i) v |= static_cast<uint32_t>(static_cast<unsigned char>(s[i])) << (8 * i);
    return v;
}

// skeleton (model +0x48) vt+0x70: joint index by name (FUN_003f6990 + FUN_0023f068, case-insensitive), -1 unknown
int jointIndex(const Chr& c, const std::string& name) {
    const alchemy::Skeleton* sk = c.model.skeleton();
    if (sk == nullptr) return -1;
    for (size_t b = 0; b < sk->bones.size(); ++b) {
        const std::string& n = sk->bones[b].name;
        if (n.size() != name.size()) continue;
        bool same = true;
        for (size_t i = 0; i < n.size() && same; ++i)
            same = std::tolower(static_cast<unsigned char>(n[i])) == std::tolower(static_cast<unsigned char>(name[i]));
        if (same) return static_cast<int>(b);
    }
    return -1;
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

// joint world point: FUN_0039d860(m, bones + joint * 0x40), FUN_0039d450(m, m, model +0x10 +0x20), FUN_0039d9a0;
// then + FUN_003a0330(offset, model matrix) (the offset turned by the published model matrix)
Vec3f jointPoint(Chr& c, int joint, float ox, float oy, float oz) {
    const alchemy::Mat4 am = chrActorMatrix(c);
    alchemy::Mat4 w = am;
    const std::vector<alchemy::Mat4>& pose = chrTickPose(c);
    if (joint >= 0 && static_cast<size_t>(joint) < pose.size()) w = alchemy::mul(pose[size_t(joint)], am);
    Vec3f at{w.m[12], w.m[13], w.m[14]};
    for (int k = 0; k < 3; ++k)
        at[size_t(k)] += ox * am.m[size_t(k)] + oy * am.m[size_t(4 + k)] + oz * am.m[size_t(8 + k)];
    return at;
}

int cellRoomOf(const Chr& c) {   // chr +0x30: word & 0xf when (word & 0xf000) == 0x1000
    if (c.cell == nullptr) return -1;
    const uint32_t w = *c.cell;
    return (w & 0xf000u) == 0x1000u ? static_cast<int>(w & 0xfu) : -1;
}

// ---------------------------------------------------------------------------------------------------------------
// NTurn look-at object (FUN_0014a5a0 pool object at ACT +0x64)
// ---------------------------------------------------------------------------------------------------------------
struct NTurn {
    uint32_t serial = 0;
    bool exists = false;       // ACT +0x64 != 0
    int mod = -1;              // +0x00 joint modifier (FUN_003bf580): index into ChrModel::boneMods
    int joint = -1;            // +0x08 "Head" (0x44b328)
    int targetJoint = -1;      // +0x0c (-1: the position +0x14)
    Chr* target = nullptr;     // +0x10
    uint32_t targetSerial = 0;
    float pos[3] = {};         // +0x14
    float hist[4][3] = {};     // +0x20 / +0x2c / +0x38 / +0x44 angle triples (newest first)
    int release = 0;           // +0x50
    int ending = 0;            // +0x54
};
std::unordered_map<const Chr*, NTurn> gNTurn;

NTurn& nturnOf(Chr& c) {
    NTurn& n = gNTurn[&c];
    if (n.serial != c.serial) n = NTurn{c.serial};
    return n;
}

// orig FUN_001456f0: create the object (modifier on "Head" with a fresh matrix, enabled, added to the actor's
// modifier list; history cleared) and mark it running (+0x54 = 0)
NTurn& nturnCreate(Chr& c) {
    NTurn& n = nturnOf(c);
    if (!n.exists) {
        n.exists = true;
        n.joint = jointIndex(c, "Head");                   // vt+0x70(0x44b328)
        BoneMod b;
        b.joint = n.joint;                                  // modifier +8; +0xc = matrix (FUN_003a4140); +0x10 = 1
        c.model.boneMods.push_back(b);                      // FUN_0031c3b0(actor +0x34, modifier)
        n.mod = static_cast<int>(c.model.boneMods.size()) - 1;
        for (auto& h : n.hist) h[0] = h[1] = h[2] = 0.0f;
        n.release = 0;
    }
    n.ending = 0;
    return n;
}

// orig FUN_00145900: the modifier leaves the actor's list (FUN_0031c650), modifier and matrix released, object freed
void nturnDelete(Chr& c, NTurn& n) {
    if (!n.exists) return;
    auto& mods = c.model.boneMods;
    if (n.mod >= 0 && static_cast<size_t>(n.mod) < mods.size()) {
        // PORT: other classes keep indices into the list (C18A / C27A / E01 / E09 / E22), so only a last entry is
        // erased; an inner one becomes inert (no joint)
        if (static_cast<size_t>(n.mod) + 1 == mods.size()) mods.pop_back();
        else mods[size_t(n.mod)] = BoneMod{};
    }
    n = NTurn{c.serial};
}

bool chrAlive(const Chr* c, uint32_t serial) {
    if (c == nullptr) return false;
    for (const Chr* x : characters().list())
        if (x == c) return x->serial == serial;
    return false;
}

// ---------------------------------------------------------------------------------------------------------------
// item props
// ---------------------------------------------------------------------------------------------------------------
Item* carried(Chr& c) { return static_cast<Item*>(c.status.block.carried); }   // status block +0x60

// FUN_0039ddd0 + FUN_003a0330: v turned about `axis` by `angle` radians (row-vector, 3x3)
void turn(float v[3], float angle, const float axis[3]) {
    ExiMat m;
    exiAxisAngle(angle, m, axis);
    exiTransformVector(v, v, m);
}

// orig FUN_001485a0 (ObjThrow): [0] what, [1..3] offset, [4] yaw, [5] pitch (floats), [6] joint
void objThrow(Chr& owner, ActRunner& act, const data::ActCmnd& c) {
    auto argF = [&](size_t i) { return i < c.args.size() ? c.args[i].f : 0.0f; };
    const std::string what = c.args.empty() ? std::string() : c.args[0].s;
    act.objectPos = jointPoint(owner, jointIndex(owner, c.args.size() > 6 ? c.args[6].s : std::string()), argF(1),
                               argF(2), argF(3));
    float dir[3] = {owner.xf.m.fwd[0], owner.xf.m.fwd[1], owner.xf.m.fwd[2]};   // transform +0x30
    turn(dir, (-argF(5) * kPi) / 180.0f, owner.xf.m.right.data());               // about +0x18
    const float up[3] = {0.0f, 1.0f, 0.0f};                                      // FUN_00206f10
    turn(dir, (argF(4) * kPi) / 180.0f, up);
    const float l = std::sqrt(dir[0] * dir[0] + dir[1] * dir[1] + dir[2] * dir[2]);   // FUN_00206ab0
    if (l > 0.0f)
        for (float& x : dir) x /= l;
    for (float& x : dir) x = x * 30.0f;                                          // DAT_0044b7a8
    const Vec3 pos{act.objectPos[0], act.objectPos[1], act.objectPos[2]};
    const Vec3 vel{dir[0], dir[1], dir[2]};
    if (tag4(what) == tag4("ITEM")) {   // 0x44b7a0
        if (owner.type != kChrEnemy && owner.type != kChrNpc) {
            act.orFlags(0x100000u);
            return;
        }
        if (Item* it = carried(owner)) throwHeldItem(*it, pos, vel, chrAdapter(&owner), itemSystem());   // FUN_001d5640
        return;
    }
    Chr* e = characters().createTag(tag4(what), true);   // FUN_0012ce70(0x50b1a0, tag, 1)
    if (e != nullptr && e->item != nullptr) launchItem(*e->item, pos, vel, &chrAdapter(&owner));   // FUN_001d5780
}

// orig FUN_00149000 (DropItem): [0] what, [1..3] offset, [4] joint
void dropItem(Chr& owner, ActRunner& act, const data::ActCmnd& c) {
    auto argF = [&](size_t i) { return i < c.args.size() ? c.args[i].f : 0.0f; };
    const std::string what = c.args.empty() ? std::string() : c.args[0].s;
    act.objectPos = jointPoint(owner, jointIndex(owner, c.args.size() > 4 ? c.args[4].s : std::string()), argF(1),
                               argF(2), argF(3));
    if (tag4(what) == tag4("ITEM")) {
        if (owner.type != kChrEnemy && owner.type != kChrNpc) {
            act.orFlags(0x100000u);
            return;
        }
        // FUN_001d5410(-1.0, item, 0): dropped where it is with a random yaw (the item stays in status +0x60)
        if (Item* it = carried(owner)) dropItemAtActor(*it, -1.0f, nullptr, itemSystem());
        return;
    }
    Chr* e = characters().createTag(tag4(what), true);
    if (e == nullptr || e->item == nullptr) return;
    e->item->pos = Vec3{act.objectPos[0], act.objectPos[1], act.objectPos[2]};   // transform +0xc = ACT +0x50
    e->xf.m.pos = act.objectPos;
    e->prevPos = act.objectPos;                                                  // PORT: presentation interpolation
    dropItemAtActor(*e->item, 0.0f, nullptr, itemSystem());                      // FUN_001d5410(0.0, item, 0)
}

// orig FUN_00149210 (ClearItm): the carried item is taken (FUN_00139540) and, unless the owner is a C23x, deleted
// (FUN_0012be90: FUN_00207350 for a linked task, else FUN_0012d6e0)
void clearItem(Chr& owner) {
    Item* it = carried(owner);
    owner.status.block.carried = nullptr;
    if (it == nullptr || (owner.id & 0xffffffu) == (tag4("C23") & 0xffffffu)) return;   // 0x44b7d0
    GameAccess* g = itemSystem().game;
    if (g == nullptr) return;
    if ((it->taskFlags & 1u) != 0) g->removeFromWorld(it);
    else g->freeItem(it);
}

// orig FUN_0014a100 (ShowItem): [0..2] offset, [3] joint; the carried item stands at the joint with the owner's axes,
// enters the world (FUN_001d3a30), its state slot reset, its model published (FUN_00109d40); ACT flag 0x10000000
void showItem(Chr& owner, ActRunner& act, const data::ActCmnd& c) {
    Item* it = carried(owner);
    if (it == nullptr) return;
    auto argF = [&](size_t i) { return i < c.args.size() ? c.args[i].f : 0.0f; };
    const Vec3f at = jointPoint(owner, jointIndex(owner, c.args.size() > 3 ? c.args[3].s : std::string()), argF(0),
                                argF(1), argF(2));
    const ChrTransform& t = owner.xf.m;
    it->fwd = Vec3{t.fwd[0], t.fwd[1], t.fwd[2]};       // FUN_00128740(item, +0x30, +0x24, +0x18)
    it->up = Vec3{t.up[0], t.up[1], t.up[2]};
    it->right = Vec3{t.right[0], t.right[1], t.right[2]};
    it->pos = Vec3{at[0], at[1], at[2]};
    itemSystem().game->itemEnterWorld(*it);              // FUN_001d3a30
    ItemStateSlot& ss = it->state;                       // +0x78
    if ((ss.factory == 0 && ss.pending == 0) || ss.param0 < 2) {
        ss.pending = -1;                                 // +0x18
        ss.param0 = 1;                                   // +0xc (+8 = 0: the slot's owner, not modelled)
        ss.param1 = 1;                                   // +0x10
    }
    it->modelXf.pos = it->pos;                           // FUN_00109d40(model, pos, scale, fwd, up, right)
    it->modelXf.right = it->right;
    it->modelXf.up = it->up;
    it->modelXf.fwd = it->fwd;
    it->modelXf.scale = it->scale;
    it->modelXf.valid = true;
    act.orFlags(0x10000000u);
}

// orig FUN_0014a2e0 (HideItem)
void hideItem(Chr& owner, ActRunner& act) {
    if ((act.flags() & 0x10000000u) == 0) return;
    act.clearFlags(0x10000000u);
    if (Item* it = carried(owner)) itemSystem().game->itemLeaveWorld(*it);   // FUN_001d3b30
}

// ---------------------------------------------------------------------------------------------------------------
// PORT dev options
// ---------------------------------------------------------------------------------------------------------------
struct Dev {
    bool parsed = false;
    long equipAt = -1;      // --equip-at TICK: every unequipped SD / CT / WG item of the inventory is equipped
    long trailLog = 0;      // --trail-log N: trail / look-at state every N ticks
    struct ActAt {
        std::string id, name;
        long tick = 0;
    };
    std::vector<ActAt> actAt;   // --act-at ID NAME TICK: FUN_00143aa0(act, NAME) on the first live ID at TICK
    struct SpawnAt {
        std::string id;
        float pos[3] = {};
        long tick = 0;
    };
    std::vector<SpawnAt> spawnAt;   // --spawn-at ID X Y Z TICK: FUN_0012d2f0(0x50b1a0, ID, 1) like EVT op 30
    long tick = 0;
    const void* dungeon = nullptr;
};
Dev gDev;

void devParse() {
    gDev.parsed = true;
    if (__argv == nullptr) return;
    for (int i = 1; i < __argc; ++i) {
        if (std::strcmp(__argv[i], "--equip-at") == 0 && i + 1 < __argc) gDev.equipAt = std::atol(__argv[i + 1]);
        if (std::strcmp(__argv[i], "--trail-log") == 0 && i + 1 < __argc) gDev.trailLog = std::atol(__argv[i + 1]);
        if (std::strcmp(__argv[i], "--spawn-at") == 0 && i + 5 < __argc)
            gDev.spawnAt.push_back({__argv[i + 1],
                                    {static_cast<float>(std::atof(__argv[i + 2])), static_cast<float>(std::atof(__argv[i + 3])),
                                     static_cast<float>(std::atof(__argv[i + 4]))},
                                    std::atol(__argv[i + 5])});
        if (std::strcmp(__argv[i], "--act-at") == 0 && i + 3 < __argc)
            gDev.actAt.push_back({__argv[i + 1], __argv[i + 2], std::atol(__argv[i + 3])});
    }
}

}  // namespace

bool actPropCommand(Chr& owner, ActRunner& act, const data::ActCmnd& c) {
    using data::ActCommand;
    auto argI = [&](size_t i) { return i < c.args.size() ? c.args[i].i : 0; };
    auto argF = [&](size_t i) { return i < c.args.size() ? c.args[i].f : 0.0f; };
    auto argS = [&](size_t i) { return i < c.args.size() ? c.args[i].s : std::string(); };
    switch (c.cmd) {
    case ActCommand::Track:      // FUN_001492b0: FUN_00145560(act, [0], joint([1]), 2, 0, 0)
        actTrackStart(owner, argI(0), jointIndex(owner, argS(1)), 2, 0, 0);
        return true;
    case ActCommand::TrackEx:    // FUN_00149320: FUN_00145560(act, [0], joint([4]), [1], [2], [3])
        actTrackStart(owner, argI(0), jointIndex(owner, argS(4)), argI(1), argI(2), argI(3));
        return true;
    case ActCommand::TrackEnd:   // FUN_00149390
        actTrackEnd(owner, argI(0), argI(1));
        return true;
    case ActCommand::NTurnPos: { // FUN_00149750: room-relative point -> FUN_00145840
        float p[3] = {argF(0), argF(1), argF(2)};
        const int room = cellRoomOf(owner);
        if (room != -1) {
            p[0] = argF(0) + hooks::roomOriginX(room);   // FUN_0011e6c0
            p[2] = argF(2) + hooks::roomOriginZ(room);   // FUN_0011e740
        }
        NTurn& n = nturnCreate(owner);                   // FUN_00145840 -> FUN_001456f0
        n.pos[0] = p[0];
        n.pos[1] = p[1];
        n.pos[2] = p[2];
        n.targetJoint = -1;
        return true;
    }
    case ActCommand::NTurnAct: { // FUN_00149840: first live character (type < 3) with the tag, its joint [1]
        const uint32_t tag = tag4(argS(0));
        for (Chr* t : characters().list()) {
            if ((t->taskFlags & 0x10000000u) != 0) continue;
            if (static_cast<uint32_t>(t->type) < 3u && t->id == tag) {
                const int j = jointIndex(*t, argS(1));
                if (j != -1) {                            // FUN_001458a0(act, target, joint)
                    NTurn& n = nturnCreate(owner);
                    n.target = t;
                    n.targetSerial = t->serial;
                    n.targetJoint = j;
                }
                break;
            }
        }
        return true;
    }
    case ActCommand::NTurnEnd: { // FUN_00149970 -> FUN_001459c0
        NTurn& n = nturnOf(owner);
        if (n.exists) {
            n.ending = 1;
            n.release = 0;
        }
        return true;
    }
    case ActCommand::ShowBox:    // FUN_00149b90: "C12A" (0x44b7d8) -> FUN_001b56c0
        if (owner.id == tag4("C12A")) npcC12Box(owner, true);
        return true;
    case ActCommand::HideBox:    // FUN_00149c00 -> FUN_001b5680
        if (owner.id == tag4("C12A")) npcC12Box(owner, false);
        return true;
    case ActCommand::ObjThrow:
        objThrow(owner, act, c);
        return true;
    case ActCommand::DropItem:
        dropItem(owner, act, c);
        return true;
    case ActCommand::ClearItm:
        clearItem(owner);
        return true;
    case ActCommand::GetItem:    // FUN_0014a0b0: status object +0x10 bit 0 (FUN_001d9040: the NPC refuses items)
        if (argI(0) == 0) owner.status.flags10 |= 1u;
        else owner.status.flags10 &= 0xfffffffeu;
        return true;
    case ActCommand::ShowItem:
        showItem(owner, act, c);
        return true;
    case ActCommand::HideItem:
        hideItem(owner, act);
        return true;
    default:
        return false;
    }
}

// orig FUN_001459f0 (work/decompiled/001459f0.c)
void actNTurnStep(Chr& c) {
    const auto found = gNTurn.find(&c);
    if (found == gNTurn.end() || found->second.serial != c.serial || !found->second.exists) return;
    NTurn& n = found->second;
    const std::vector<alchemy::Mat4>& pose = chrTickPose(c);
    if (n.mod < 0 || static_cast<size_t>(n.mod) >= c.model.boneMods.size() || n.joint < 0 ||
        static_cast<size_t>(n.joint) >= pose.size())
        return;   // PORT guard: no "Head" joint / model not loaded (the original reads the bone array unchecked)
    alchemy::Mat4& modM = c.model.boneMods[size_t(n.mod)].m;
    ExiMat j = toExi(pose[size_t(n.joint)]);   // FUN_0039d860(m90, bones + joint * 0x40)
    ExiMat d = toExi(modM);                    // FUN_0039d860(md0, matrix +0x20)
    exiInvert(d, d);                           // FUN_0039f1d0(r, md0, md0)
    exiPreMul(j, d);                           // FUN_0039d640(m90, md0): the modifier taken out of the pose
    exiMul(j, j, toExi(chrActorMatrix(c)));    // FUN_0039d450(m90, m90, model +0x10 +0x20)
    float self[3];
    exiGetTranslation(j, self);                // FUN_0039d9a0(m90, fStack_18)
    float avg[3];
    if (n.ending == 0) {
        if (n.targetJoint != -1 && n.target != nullptr && n.target != &c && chrAlive(n.target, n.targetSerial)) {
            const std::vector<alchemy::Mat4>& tp = chrTickPose(*n.target);
            if (static_cast<size_t>(n.targetJoint) < tp.size()) {
                ExiMat tj = toExi(tp[size_t(n.targetJoint)]);
                exiMul(tj, tj, toExi(chrActorMatrix(*n.target)));
                exiGetTranslation(tj, n.pos);  // -> +0x14
            }
        }
        const float v[3] = {n.pos[0] - self[0], n.pos[1] - self[1], n.pos[2] - self[2]};
        // orig FUN_00206dc0(out, v): yaw = atan2f(v.x, v.z); v turned by -yaw about y (FUN_00206ca0);
        // out = (-atan2f(r.y, r.z), yaw, 0)
        const float yaw = std::atan2(v[0], v[2]);
        const float sy = std::sin(-yaw), cy = std::cos(-yaw);
        const float ry = v[1];
        const float rz = v[0] * -sy + v[2] * cy;
        const float look[3] = {-std::atan2(ry, rz), yaw, 0.0f};
        exiInvert(j, j);                       // FUN_0039f1d0(r, m90, m90)
        float e[3];
        exiExtractEuler(j, &e[0], &e[1], &e[2]);   // FUN_0039e160
        exiEuler(e[0], e[1], e[2], j);             // FUN_0039de50(e, m90)
        ExiMat l;
        exiEuler(look[0], look[1], look[2], l);    // FUN_0039de50(look, md0)
        exiPreMul(j, l);                           // FUN_0039d640(m90, md0)
        exiExtractEuler(j, &e[0], &e[1], &e[2]);
        static const int kLimits[3][2] = {{-60, 30}, {-60, 60}, {-60, 60}};   // 0x48e5e0 [V]
        int clamped = 0;
        for (int k = 0; k < 3; ++k) {
            const float lo = (static_cast<float>(kLimits[k][0]) * kPi) / 180.0f;
            if (e[k] < lo) {
                e[k] = lo;
                ++clamped;
            } else {
                const float hi = (static_cast<float>(kLimits[k][1]) * kPi) / 180.0f;
                if (hi < e[k]) {
                    e[k] = hi;
                    ++clamped;
                }
            }
        }
        for (int k = 0; k < 3; ++k) {
            n.hist[3][k] = n.hist[2][k];   // +0x44 = +0x38
            n.hist[2][k] = n.hist[1][k];   // +0x38 = +0x2c
            n.hist[1][k] = n.hist[0][k];   // +0x2c = +0x20
        }
        if (clamped == 0) {
            n.release = 0;
            for (int k = 0; k < 3; ++k) n.hist[0][k] = n.hist[0][k] - (n.hist[0][k] - e[k]) * 0.0625f;
        } else {
            for (int k = 0; k < 3; ++k) n.hist[0][k] = n.hist[0][k] - n.hist[0][k] * 0.0625f;
        }
    } else if (n.release < 0x23) {
        ++n.release;
        for (int k = 0; k < 3; ++k) {
            n.hist[3][k] = n.hist[2][k];
            n.hist[2][k] = n.hist[1][k];
            n.hist[1][k] = n.hist[0][k];
        }
        for (int k = 0; k < 3; ++k) n.hist[0][k] = n.hist[0][k] - n.hist[0][k] * 0.125f;
    } else {
        nturnDelete(c, n);   // FUN_00145900
        return;
    }
    avg[1] = (n.hist[0][1] + n.hist[1][1] + n.hist[2][1] + n.hist[3][1]) * 0.25f;
    avg[2] = (n.hist[0][2] + n.hist[1][2] + n.hist[2][2] + n.hist[3][2]) * 0.25f;
    avg[0] = (n.hist[0][0] + n.hist[1][0] + n.hist[2][0] + n.hist[3][0]) * 0.25f;
    ExiMat out;
    exiEuler(avg[0], avg[1], avg[2], out);   // FUN_0039de50(avg, matrix +0x20)
    modM = fromExi(out);
}

// FUN_00143bf0: FUN_00145660(act) then FUN_001459c0(act, 10)
void actPropsResetTrailsLook(Chr& c) {
    actTrackReset(c);
    const auto it = gNTurn.find(&c);
    if (it != gNTurn.end() && it->second.serial == c.serial && it->second.exists) {
        it->second.ending = 1;    // +0x54
        it->second.release = 0;   // +0x50
    }
}

// FUN_00143bf0 tail: ACT flag 0x10000000 with a carried item -> FUN_001d3b30(item); status object +0x10 &= ~1
void actPropsResetItems(Chr& c, uint32_t actFlags) {
    if ((actFlags & 0x10000000u) != 0) {
        if (Item* it = carried(c)) itemSystem().game->itemLeaveWorld(*it);
    }
    c.status.flags10 &= 0xfffffffeu;
}

void actPropsDevTick() {
    if (!gDev.parsed) devParse();
    if (gDev.equipAt < 0 && gDev.trailLog <= 0 && gDev.actAt.empty() && gDev.spawnAt.empty()) return;
    if (gameWorld().dungeon != gDev.dungeon) {
        gDev.dungeon = gameWorld().dungeon;
        gDev.tick = 0;
    }
    ++gDev.tick;
    for (const Dev::SpawnAt& sp : gDev.spawnAt) {
        if (sp.tick != gDev.tick) continue;
        Chr* c = characters().create(sp.id.c_str(), true);
        if (c == nullptr) {
            platform::log("--spawn-at: " + sp.id + " refused");
            continue;
        }
        if (static_cast<uint32_t>(c->type) < 3u) c->f28 = 0x40000011;   // as EVT op 30 (FUN_0016fe70)
        c->xf.m.pos = {sp.pos[0], sp.pos[1], sp.pos[2]};
        c->prevPos = c->xf.m.pos;
        platform::log("--spawn-at: " + sp.id + " spawned");
    }
    for (const Dev::ActAt& a : gDev.actAt) {
        if (a.tick != gDev.tick) continue;
        for (Chr* c : characters().list()) {
            if ((c->taskFlags & 0x10000000u) != 0 || c->idString() != a.id) continue;
            const bool ok = c->act.start(a.name);
            platform::log("--act-at: " + a.id + " " + a.name + (ok ? " started" : " not found"));
            break;
        }
    }
    if (gDev.tick == gDev.equipAt) {
        Inventory& inv = inventory();
        for (int i = 0; i < inv.count && i < Inventory::kMaxItems; ++i) {
            const Item* it = inv.slots[i];
            if (it == nullptr || (it->cls != kClsSD && it->cls != kClsCT && it->cls != kClsWG)) continue;
            if (inv.equippedSlotOfIndex(i) >= 0) continue;
            const bool ok = inv.equipItem(i, true, itemSystem());   // FUN_001d6d60
            char buf[96];
            std::snprintf(buf, sizeof buf, "--equip-at: slot %d (%c%c%c%c) %s", i, char(it->id & 0xff),
                          char((it->id >> 8) & 0xff), char((it->id >> 16) & 0xff), char(it->id >> 24),
                          ok ? "equipped" : "refused");
            platform::log(buf);
        }
    }
    if (gDev.trailLog > 0 && gDev.tick % gDev.trailLog == 0) {
        char buf[200];
        std::snprintf(buf, sizeof buf, "trail-log t=%ld trails=%d", gDev.tick, actTrailsLive());
        platform::log(buf + actTrailsDescribe());
        for (const Chr* c : characters().list()) {
            if ((c->taskFlags & 0x10000000u) != 0 || c->type != kChrNpc) continue;
            std::snprintf(buf, sizeof buf, "trail-log t=%ld npc %s act=%s pos=(%.0f,%.0f,%.0f)", gDev.tick,
                          c->idString().c_str(), c->act.currentName() ? c->act.currentName() : "-", c->xf.m.pos[0],
                          c->xf.m.pos[1], c->xf.m.pos[2]);
            platform::log(buf);
        }
        for (const auto& [chr, n] : gNTurn) {
            if (!n.exists || !chrAlive(chr, n.serial)) continue;
            std::snprintf(buf, sizeof buf, "trail-log t=%ld %s nturn ending=%d release=%d hist0=(%.3f,%.3f,%.3f)",
                          gDev.tick, chr->idString().c_str(), n.ending, n.release, n.hist[0][0], n.hist[0][1],
                          n.hist[0][2]);
            platform::log(buf);
        }
    }
}

}  // namespace baroque::game
