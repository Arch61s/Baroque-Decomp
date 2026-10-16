// Boss / special-enemy support (boss_classes.hpp, work/reports/BOSSES_2026-10-10.md).
#include "game/character/boss_classes.hpp"

#include "alchemy/scene_render.hpp"
#include "game/act_runner.hpp"
#include "game/character/ai.hpp"
#include "game/character/character.hpp"
#include "game/character/combat.hpp"
#include "game/dungeon.hpp"
#include "game/event/stage_script.hpp"
#include "game/flag_manager.hpp"
#include "game/game_world.hpp"
#include "game/hud/hud_api.hpp"
#include "game/items/chr_adapter.hpp"
#include "game/items/game_adapter.hpp"
#include "game/items/inventory.hpp"
#include "game/items/item.hpp"
#include "game/items/item_effects.hpp"
#include "game/items/item_world.hpp"
#include "game/objects/objects.hpp"
#include "game/scenes/game_scene.hpp"
#include "platform/platform.hpp"

#include <bit>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#pragma fenv_access(on)

namespace baroque::game {

namespace {

constexpr float kPi = std::bit_cast<float>(0x40490fdbu);   // 0x40490fdb (lui 0x4049 / ori 0xfdb)

uint32_t tag4(const std::string& s) {
    uint32_t v = 0;
    for (size_t i = 0; i < 4; ++i) v |= uint32_t(i < s.size() ? static_cast<unsigned char>(s[i]) : ' ') << (8 * i);
    return v;
}

// actor +0x48 vt+0x70: joint index by name (igSkeleton lookup FUN_003f6990 is case-insensitive, ENEMIES_NATIVE.md)
int jointIndex(const Chr& c, const std::string& name) {
    const alchemy::Skeleton* sk = c.model.skeleton();
    if (sk == nullptr) return -1;
    for (size_t b = 0; b < sk->bones.size(); ++b)
        if (_stricmp(sk->bones[b].name.c_str(), name.c_str()) == 0) return static_cast<int>(b);
    return -1;   // UNVERIFIED: the original's result for an unknown joint name
}

// orig FUN_0039ddd0 -> FUN_0039ded0: rotation of `angle` about the unit `axis` (rows), as camera.cpp axisAngleMatrix.
// UNVERIFIED: sin/cos of the game's libm (FUN_002386a0 / FUN_00238400) may differ from std:: in the last ulp.
void axisAngle(float angle, const float axis[3], float m[3][3]) {
    const float s = std::sin(angle);
    const float c = std::cos(angle);
    const float omc = 1.0f - c;
    const float ax = axis[0], ay = axis[1], az = axis[2];
    const float zero = 0.0f;
    float t = omc * ax;
    m[0][0] = (zero + c) + t * ax;
    m[0][1] = (zero + s * az) + t * ay;
    m[0][2] = t * az - s * ay;
    t = omc * ay;
    m[1][0] = t * ax - s * az;
    m[1][1] = (zero + c) + t * ay;
    m[1][2] = (zero + s * ax) + t * az;
    t = omc * az;
    m[2][0] = (zero + s * ay) + t * ax;
    m[2][1] = t * ay - s * ax;
    m[2][2] = (zero + c) + t * az;
}

// orig FUN_003a0330: out = v * M (3x3 rows); out may alias v
void mulRows(float out[3], const float v[3], const float m[3][3]) {
    const float x = v[0], y = v[1], z = v[2];
    out[0] = z * m[2][0] + x * m[0][0] + y * m[1][0];
    out[1] = z * m[2][1] + x * m[0][1] + y * m[1][1];
    out[2] = z * m[2][2] + x * m[0][2] + y * m[1][2];
}

// orig FUN_00206ab0 (normalize in place)
void normalize3(float v[3]) {
    const float sq = v[1] * v[1] + v[0] * v[0] + v[2] * v[2];
    if (sq <= 0.0f) return;
    const float l = std::sqrt(sq);
    v[0] /= l;
    v[1] /= l;
    v[2] /= l;
}

bool devVerbose();   // PORT: --boss-log given

float rand01(uint32_t n) { return static_cast<float>(static_cast<int>(gameWorld().rng.rand(n))) / 32768.0f; }

// orig FUN_00148880 (work/decompiled/00148880.c, operand loads and float order from the disassembly 0x148880..)
// operands: [0] s4 what, [1..3] f offset, [4] f yaw (+0x10 lwc1), [5] f pitch (+0x14), [6] f speed (+0x18), [7] S joint
void actThrowItem(Chr& owner, ActRunner& act, const data::ActCmnd& c) {
    auto raw = [&](size_t i) { return i < c.args.size() ? std::bit_cast<float>(c.args[i].i) : 0.0f; };
    auto argF = [&](size_t i) {
        if (i >= c.args.size()) return 0.0f;
        return c.args[i].kind == data::ActOperandKind::Float ? c.args[i].f : raw(i);
    };
    const std::string what = c.args.empty() ? std::string() : c.args[0].s;
    const uint32_t tag = tag4(what);
    float yaw = argF(4), pitch = argF(5), speed = argF(6);   // f22, f21, f20
    int rnd = 0, kind = 0;                                   // v1 (lVar6), s0 (lVar7)
    if (tag == tag4("C09T")) { rnd = 1; kind = 1; }          // 0x44b7b0
    else if (tag == tag4("C11T")) kind = 2;                  // 0x44b7b8
    else if (tag == tag4("C12T")) { rnd = 2; kind = 3; }     // 0x44b7c0
    else if (tag == tag4("E27T")) kind = 4;                  // 0x44b7c8
    if (rnd != 0) {
        yaw = rand01(0x8000) * (rnd == 1 ? 180.0f : 45.0f);
        if (gameWorld().rng.rand(2) != 0) yaw = -yaw;
        pitch = (0.0f + pitch) + 20.0f * rand01(0x8000);     // adda.s / madd.s
        speed = (0.0f + speed) + 5.0f * rand01(0x8000);
    }
    // release point: joint matrix x model matrix, translation (FUN_0039d860 / FUN_0039d450 / FUN_0039d9a0) -> ACT +0x50,
    // plus the offset turned by the model matrix (FUN_003a0330)
    const alchemy::Mat4 am = chrActorMatrix(owner);
    alchemy::Mat4 w = am;
    const int joint = jointIndex(owner, c.args.size() > 7 ? c.args[7].s : std::string());
    const std::vector<alchemy::Mat4>& pose = chrTickPose(owner);
    if (joint >= 0 && static_cast<size_t>(joint) < pose.size()) w = alchemy::mul(pose[static_cast<size_t>(joint)], am);
    const float ox = argF(1), oy = argF(2), oz = argF(3);
    Vec3f at{w.m[12], w.m[13], w.m[14]};
    float ofs[3];
    for (int k = 0; k < 3; ++k)
        ofs[k] = oz * am.m[static_cast<size_t>(8 + k)] + ox * am.m[static_cast<size_t>(k)] + oy * am.m[static_cast<size_t>(4 + k)];
    at = {at[0] + ofs[0], at[1] + ofs[1], at[2] + ofs[2]};
    act.objectPos = at;
    // direction: forward (transform +0x30) pitched about the right axis (+0x18) by -pitch, turned about world up by yaw
    float dir[3] = {owner.xf.m.fwd[0], owner.xf.m.fwd[1], owner.xf.m.fwd[2]};
    float m[3][3];
    axisAngle((kPi * -pitch) / 180.0f, owner.xf.m.right.data(), m);
    mulRows(dir, dir, m);
    const float up[3] = {0.0f, 1.0f, 0.0f};                  // FUN_00206f10
    axisAngle((kPi * yaw) / 180.0f, up, m);
    mulRows(dir, dir, m);
    normalize3(dir);
    dir[0] = dir[0] * speed;
    dir[1] = dir[1] * speed;
    dir[2] = dir[2] * speed;
    const Vec3 pos{at[0], at[1], at[2]};
    const Vec3 vel{dir[0], dir[1], dir[2]};
    auto launch = [&](uint32_t itemTag) {
        if (itemTag == 0) return;
        Chr* e = characters().createTag(itemTag, true);       // FUN_0012ce70(0x50b1a0, tag, 1)
        if (e == nullptr) return;
        if (e->item == nullptr) {
            platform::log("ThrowItm: " + e->idString() + " is not an item entity (UNVERIFIED: not launched)");
            return;
        }
        launchItem(*e->item, pos, vel, &chrAdapter(&owner));  // FUN_001d5780(item, ACT +0x50, dir, owner)
        if (devVerbose()) {
            char buf[160];
            std::snprintf(buf, sizeof buf, "boss-log ThrowItm %s by %s: %s from (%.0f,%.0f,%.0f) vel (%.2f,%.2f,%.2f)",
                          what.c_str(), owner.idString().c_str(), e->idString().c_str(), pos.x, pos.y, pos.z, vel.x,
                          vel.y, vel.z);
            platform::log(buf);
        }
    };
    Dungeon* dg = gameWorld().dungeon;
    switch (kind) {
    case 4:   // E27T: a random item of a random floor category (FUN_001d2f30 -> FUN_001d30c0)
        if (dg != nullptr) launch(dg->pickItem(dg->pickItemCategory()));
        return;
    case 3:   // C12T: FUN_001d30c0(5)
        if (dg != nullptr) launch(dg->pickItem(5));
        return;
    case 2:   // C11T: FUN_001d31b0
        launch(anyUnseenItemId(itemSystem()));
        return;
    case 1: {   // C09T: FUN_002071e0(0x53eb20, a1) == 0 -> FUN_001d30c0(9) else (10)
        // UNVERIFIED: a1 is left holding a stack address (sp + 0xc0) at 0x148da0, i.e. rand(~0x01ffxxxx): practically
        // never 0. The port draws once with that order of magnitude.
        const uint32_t r = gameWorld().rng.rand(0x01fffe00u);
        if (dg != nullptr) launch(dg->pickItem(r == 0 ? 9 : 10));
        return;
    }
    default: break;
    }
    if (tag == tag4("ITEM")) {   // 0x44b7a0: the carried item (status +0x60) of an enemy / NPC; the player latches 0x100000
        if (owner.type != kChrEnemy && owner.type != kChrNpc) {
            act.orFlags(0x100000u);
            return;
        }
        if (owner.status.block.carried != nullptr)
            throwHeldItem(*static_cast<Item*>(owner.status.block.carried), pos, vel, chrAdapter(&owner), itemSystem());
        return;
    }
    launch(tag);
}

// orig FUN_00133400 (work/decompiled/00133400.c): one of the first two PRMCHAR drop tags (+0x78 + 8 * rand(1)); the
// other one when it starts with '0'; 0 when both do
uint32_t prmDropTag(const data::PrmChar* prm) {
    const uint32_t k = gameWorld().rng.rand(1);
    if (prm == nullptr) return 0;   // UNVERIFIED: the original reads the record unconditionally
    auto tagAt = [&](uint32_t i) { return uint32_t(prm->w78[i][0]) | (uint32_t(prm->w78[i][1]) << 16); };
    uint32_t t = tagAt(k);
    if ((t & 0xffu) == 0x30u) {
        t = tagAt(k ^ 1u);
        if ((t & 0xffu) == 0x30u) t = 0;
    }
    return t;
}

// orig FUN_001392f0 (work/decompiled/001392f0.c): the steal of a hit with m3 0x40 (FUN_0014bc30). `thief` holds
// nothing (+0x60); same character type or an NPC victim -> nothing. Victim = player: brand 'ST02' (0x44b100) -> the
// guard flash FUN_0013de20, else a random unequipped inventory item (FUN_001d9450) is taken out (FUN_001d6840) into
// the thief's +0x60, log 0x253 "%s was stolen.", thief 'E15' (0x44b108) -> counter 3 + 1. Otherwise (the player
// stealing from an enemy): inventory not full (20) -> an item of the victim's drop tags (FUN_00133400) is created
// (FUN_0012ce70(tag, 0)), log 0x260 and added to the inventory (FUN_001d65c0).
bool stealHit(StatusObj& thief, StatusObj& victim) {
    if (thief.block.carried != nullptr) return false;
    Chr* vc = victim.owner;
    Chr* tc = thief.owner;
    if (vc == nullptr || tc == nullptr) return false;
    if (tc->type == vc->type) return false;
    if (vc->type == 1) return false;
    ItemSystem& sys = itemSystem();
    if (vc == characters().player()) {
        if (vc->status.block.brand == tag4("ST02")) {
            chrAdapter(vc).invincibleFlash();             // FUN_0013de20
            return false;
        }
        const int idx = inventory().randomUnequipped(sys);   // FUN_001d9450(0x53d910)
        if (idx < 0) return false;
        Item* it = inventory().remove(idx, sys);           // FUN_001d6840(0x53d910, idx)
        thief.block.carried = it;
        if (it == nullptr) return false;
        gameAdapter().message(0x253, MsgArg(itemDisplayName(*it, sys)));   // FUN_0015d480(0x5180f0, str 0x253, name)
        if (devVerbose()) platform::log("boss-log steal: " + tc->idString() + " took " + itemDisplayName(*it, sys));
        if ((tc->id & 0xffffffu) == (tag4("E15") & 0xffffffu)) counters().add(3, 1);   // FUN_00179570(0x53c5c0, 3, 1)
        return true;
    }
    if (inventory().count == 0x14) return false;           // iRam0053d910 == 20
    const uint32_t tag = prmDropTag(victim.prm);           // FUN_00133400(victim +8)
    if (tag == 0) return false;
    Item* it = gameAdapter().createItem(tag, false);       // FUN_0012ce70(0x50b1a0, tag, 0)
    if (it == nullptr) return false;
    gameAdapter().message(0x260, MsgArg(itemDisplayName(*it, sys)));
    return inventory().add(it, sys);                       // FUN_001d65c0(0x53d910, item)
}

// orig FUN_001330c0 (work/decompiled/001330c0.c): drop tag of a dead character. mult = status +0x64 (set by the
// killer), prm = PRMCHAR record (+0x78 + 8i tag, +0x7c + 8i weight of 255, i < 3).
uint32_t dropClass(float mult, const data::PrmChar* prm) {
    float f = 1.0f;
    const int floor = floorIdNow();
    if (isScenarioFloor(floor) || isSecondDungeon(floor)) f = 2.0f;   // FUN_0011c2c0 / FUN_0011c390
    Dungeon* dg = gameWorld().dungeon;
    if (flags().test(0x39d) && gameWorld().rng.rand(100) <= 0x31u) {
        const uint32_t t = dg != nullptr ? dg->pickItem(9) : 0u;     // FUN_001d30c0(9): a disk
        if (t != 0) return t;
    }
    if (prm == nullptr) return 0;   // UNVERIFIED: the original reads the record unconditionally
    int r = static_cast<int>(gameWorld().rng.rand(0xff));
    for (uint32_t i = 0; i < 3; ++i) {
        const uint32_t tag = uint32_t(prm->w78[i][0]) | (uint32_t(prm->w78[i][1]) << 16);
        const int wOrig = static_cast<int>(uint32_t(prm->w78[i][2]) | (uint32_t(prm->w78[i][3]) << 16));
        int w = wOrig;
        const int cls = itemClassOf(tag);                            // FUN_001d2bf0
        if (flags().test(0x398)) {
            if (static_cast<unsigned>(cls) < 3u) w = 0;
        } else if (flags().test(0x399)) {
            if (static_cast<unsigned>(cls - 3) < 2u) w = 0;
        } else if (flags().test(0x39a)) {
            if ((cls >= 0 && cls <= 7) || static_cast<unsigned>(cls - 10) < 2u) w = 0;
        } else if (flags().test(0x39c)) {
            if (cls < 0xc) w = 0;
        }
        if (r < w) {
            if (cls == 0xd) {                                        // crystal: 'CC00' (0x44afe8) unless the roll wins
                if (flags().test(0x4f4)) f = f * 3.0f;
                const int roll = static_cast<int>(gameWorld().rng.rand(100));
                if (static_cast<int>(f * mult) <= roll) return tag4("CC00");
            }
            return tag;
        }
        r = r - wOrig;
        if (r < 0) r = 0;
    }
    return 0;
}

// orig FUN_00136d30 (work/decompiled/00136d30.c): the death drop (FUN_001cd9a0): not for the player; the carried item
// (+0x60, e.g. stolen by E15) or else a new item of FUN_001330c0, placed at the corpse with FUN_001d5410(-1.0, item, chr)
void deathDrop(Chr& c) {
    if (&c == characters().player()) return;
    ItemSystem& sys = itemSystem();
    Item* it = static_cast<Item*>(c.status.block.carried);
    c.status.block.carried = nullptr;
    if (it == nullptr) {
        const uint32_t tag = dropClass(c.status.block.lastExpMul, c.status.prm);
        if (tag != 0) {
            Chr* e = characters().createTag(tag, true);              // FUN_0012ce70(0x50b1a0, tag, 1)
            if (e != nullptr) it = e->item;
        }
    }
    if (it != nullptr) {
        dropItemAtActor(*it, -1.0f, &chrAdapter(&c), sys);          // FUN_001d5410(-1.0, item, chr)
        if (devVerbose()) platform::log("boss-log drop: " + c.idString() + " dropped " + itemIdString(it->id));
    }
}

// ---- PORT dev options -------------------------------------------------------------------------------------------
struct DevOpts {
    bool parsed = false;
    std::string warpTag;
    long warpTick = 30;
    float warpOffset = 0.0f;   // --warp-offset D: D units along the target's forward, facing it
    long logEvery = 0;
    long killAt = -1;       // --boss-kill-at TICK
    long hpAt = -1;         // --boss-hp-at TICK PCT
    int hpPct = 100;
    long fireAt = -1;       // --fire-j030-at TICK
    bool keepHp = false;    // --player-keep-hp
    std::vector<uint32_t> bossFlags;   // --boss-flag N (repeatable)
    bool reentered = false;
    const void* dungeon = nullptr;     // floor instance (tick counter restarts on every floor)
    long tick = 0;
    bool warped = false;
    uint64_t flagSnapshot = 0;
};
DevOpts gDev;
bool devVerbose() { return gDev.logEvery > 0; }

void devParse() {
    gDev.parsed = true;
    const int argc = __argc;
    char** argv = __argv;
    if (argv == nullptr) return;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--warp-player-to") == 0 && i + 1 < argc && std::strlen(argv[i + 1]) == 4) {
            gDev.warpTag = argv[i + 1];
            if (i + 2 < argc && argv[i + 2][0] != '-') gDev.warpTick = std::atol(argv[i + 2]);
        }
        if (std::strcmp(argv[i], "--warp-offset") == 0 && i + 1 < argc) gDev.warpOffset = static_cast<float>(std::atof(argv[i + 1]));
        if (std::strcmp(argv[i], "--boss-log") == 0 && i + 1 < argc) gDev.logEvery = std::atol(argv[i + 1]);
        if (std::strcmp(argv[i], "--boss-kill-at") == 0 && i + 1 < argc) gDev.killAt = std::atol(argv[i + 1]);
        if (std::strcmp(argv[i], "--boss-hp-at") == 0 && i + 2 < argc) {
            gDev.hpAt = std::atol(argv[i + 1]);
            gDev.hpPct = std::atoi(argv[i + 2]);
        }
        if (std::strcmp(argv[i], "--player-keep-hp") == 0) gDev.keepHp = true;
        if (std::strcmp(argv[i], "--fire-j030-at") == 0 && i + 1 < argc) gDev.fireAt = std::atol(argv[i + 1]);
        if (std::strcmp(argv[i], "--boss-flag") == 0 && i + 1 < argc)
            gDev.bossFlags.push_back(static_cast<uint32_t>(std::strtoul(argv[i + 1], nullptr, 0)));
    }
}

uint64_t bossFlagBits() {
    static const uint32_t kFlags[] = {0x3bc, 0x3bd, 0x3be, 0x3bf, 0x3c0, 0x3c1, 0x3c2, 0x3c3, 0x3c4, 0x3c5, 0x19, 0x1e, 5};
    uint64_t v = 0;
    for (size_t i = 0; i < sizeof kFlags / sizeof kFlags[0]; ++i)
        if (flags().test(kFlags[i])) v |= uint64_t(1) << i;
    return v;
}

void devLog() {
    char buf[256];
    const Chr* pl = characters().player();
    if (pl != nullptr) {
        std::snprintf(buf, sizeof buf, "boss-log t=%ld player hp=%d/%d pos=(%.0f,%.0f,%.0f) act=%s stages=%d", gDev.tick,
                      pl->status.block.hp, pl->status.block.hpMax, pl->xf.m.pos[0], pl->xf.m.pos[1], pl->xf.m.pos[2],
                      pl->act.currentName() ? pl->act.currentName() : "-", stageCount());
        platform::log(buf);
    }
    for (const Chr* c : characters().list()) {   // lifts O40N..O42N started by a cleared spawner
        if ((c->taskFlags & 0x10000000u) != 0 || c->type != kChrObject) continue;
        const std::string id = c->idString();
        if (id.size() == 4 && id[0] == 'O' && id[1] == '4' && id[3] == 'N') {
            std::snprintf(buf, sizeof buf, "boss-log t=%ld %s y=%.1f", gDev.tick, id.c_str(), c->xf.m.pos[1]);
            platform::log(buf);
        }
    }
    for (const Chr* c : characters().list()) {
        if ((c->taskFlags & 0x10000000u) != 0 || c->type != kChrEnemy) continue;
        std::snprintf(buf, sizeof buf,
                      "boss-log t=%ld %s hp=%d/%d act=%s FLAG=%u MODE=%u aiflags=%08x pos=(%.0f,%.0f,%.0f) sm=%d/%d/%d/%d task=%08x",
                      gDev.tick, c->idString().c_str(), c->status.block.hp, c->status.block.hpMax,
                      c->act.currentName() ? c->act.currentName() : "-", c->ai.flags & 1u, (c->ai.flags >> 1) & 3u,
                      c->ai.flags, c->xf.m.pos[0], c->xf.m.pos[1], c->xf.m.pos[2], static_cast<int>(c->sm.cur), static_cast<int>(c->sm.curFactory), static_cast<int>(c->sm.next), c->sm.priority, c->taskFlags);
        platform::log(buf);
    }
}

}  // namespace

bool bossActCommand(Chr& owner, ActRunner& act, const data::ActCmnd& c) {
    if (c.cmd != data::ActCommand::ThrowItm) return false;
    actThrowItem(owner, act, c);
    return true;
}

void bossDevTick() {
    // the steal hook of the hit FX code (CombatHooks::steal, FUN_001392f0) when the game build has not installed one
    if (!combatHooks().steal) combatHooks().steal = [](StatusObj& t, StatusObj& v) { return stealHit(t, v); };
    if (!combatHooks().dropItem) combatHooks().dropItem = [](Chr& dead) { deathDrop(dead); };   // FUN_00136d30
    if (!gDev.parsed) devParse();
    if (gDev.warpTag.empty() && gDev.logEvery <= 0 && gDev.killAt < 0 && gDev.hpAt < 0 && gDev.fireAt < 0 && !gDev.keepHp && gDev.bossFlags.empty()) return;
    if (!gameActive() || gameWorld().dungeon == nullptr) return;
    if (gameWorld().dungeon != gDev.dungeon) {   // a new floor: the dev tick counter restarts
        gDev.dungeon = gameWorld().dungeon;
        gDev.tick = 0;
        gDev.warped = false;
    }
    ++gDev.tick;
    if (!gDev.bossFlags.empty() && !gDev.reentered && gDev.tick == 2) {
        // the --floor dev start runs the new-game flag wipe (FUN_00178ad0) after main's --flag: set the flags now and
        // re-enter the same floor (FUN_0011bfe0(game, floor, 1)) so the floor generation (FUN_00122e20 gates) sees them
        for (uint32_t f : gDev.bossFlags) flags().set(f);
        gDev.reentered = true;
        if (stageHooks().requestFloor != nullptr && stageHooks().currentFloor != nullptr) {
            platform::log("boss dev: flags set, re-entering floor " + std::to_string(stageHooks().currentFloor()));
            stageHooks().requestFloor(stageHooks().currentFloor());
        }
        return;
    }
    Chr* pl = characters().player();
    if (!gDev.warped && !gDev.warpTag.empty() && gDev.tick >= gDev.warpTick && pl != nullptr) {
        const uint32_t t = tag4(gDev.warpTag);
        for (Chr* c : characters().list()) {
            if ((c->taskFlags & 0x10000000u) != 0 || c->id != t) continue;
            const Vec3f& p = c->xf.m.pos;
            const Vec3f& f = c->xf.m.fwd;
            const float d = gDev.warpOffset;
            pl->xf.m.pos = {p[0] + f[0] * d, p[1], p[2] + f[2] * d};
            pl->prevPos = pl->xf.m.pos;
            if (d != 0.0f) {   // face the target (as the talk dev option)
                pl->xf.m.fwd = {-f[0], 0.0f, -f[2]};
                const float l = std::sqrt(f[0] * f[0] + f[2] * f[2]);
                if (l > 0.0f) {
                    pl->xf.m.fwd[0] /= l;
                    pl->xf.m.fwd[2] /= l;
                }
                pl->xf.m.up = {0.0f, 1.0f, 0.0f};
                pl->xf.m.right = {pl->xf.m.fwd[2], 0.0f, -pl->xf.m.fwd[0]};
            }
            gDev.warped = true;
            platform::log("boss dev: player placed on " + gDev.warpTag + " at t=" + std::to_string(gDev.tick));
            break;
        }
        if (!gDev.warped && gDev.tick == gDev.warpTick) platform::log("boss dev: no entity " + gDev.warpTag);
    }
    if (gDev.keepHp && pl != nullptr && pl->status.block.hp > 0) {   // PORT dev: HP max 999, refilled every tick
        pl->status.block.hpMax = 999;
        pl->status.block.hp = 999;
    }
    if (gDev.tick == gDev.fireAt && pl != nullptr) {   // one Angelic Rifle shot as the ACT Object "J030" of AT01_G1
        if (Chr* e = characters().createTag(tag4("J030"), true)) {
            objectLaunch(*e, *pl, pl->xf.m.pos, -1);
            platform::log("boss dev: J030 fired at t=" + std::to_string(gDev.tick));
        }
    }
    if (gDev.tick == gDev.hpAt) {   // enemy HP set to PCT % of the maximum (HP_PCT driven phase changes)
        for (Chr* c : characters().list())
            if ((c->taskFlags & 0x10000000u) == 0 && c->type == kChrEnemy && c->status.block.hp > 0) {
                const int v = c->status.block.hpMax * gDev.hpPct / 100;
                c->status.block.hp = v < 1 ? 1 : v;
            }
        platform::log("boss dev: enemies HP " + std::to_string(gDev.hpPct) + "% at t=" + std::to_string(gDev.tick));
    }
    if (gDev.tick == gDev.killAt) {   // HP 0 -> the common tick FUN_00129c60 requests the death state (as --kill)
        for (Chr* c : characters().list())
            if ((c->taskFlags & 0x10000000u) == 0 && c->type == kChrEnemy && c->status.block.hp > 0) c->status.block.hp = 0;
        platform::log("boss dev: enemies HP 0 at t=" + std::to_string(gDev.tick));
    }
    if (gDev.logEvery > 0) {
        const uint64_t f = bossFlagBits();
        if (f != gDev.flagSnapshot) {
            char buf[96];
            std::snprintf(buf, sizeof buf, "boss-log t=%ld flags(3bc..3c5,19,1e,5)=%llx", gDev.tick,
                          static_cast<unsigned long long>(f));
            platform::log(buf);
            gDev.flagSnapshot = f;
        }
        if (gDev.tick % gDev.logEvery == 0) devLog();
    }
}

}  // namespace baroque::game
