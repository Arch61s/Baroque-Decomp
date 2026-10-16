// Enemy/NPC AI, translated from the functions listed in ai.hpp (work/decompiled/<addr>.c, ENEMIES_COMBAT.md §2).
// Constants read from the ELF: 0x466410/0x466418 = 200.0f (grid step / ramp probe), 0x466420..0x466440 = 45.0f
// (sphere radii for FUN_00143150), 0x4e5cb0 = " " ("any action"), DAT_004663d8 initial 4, DAT_004663e0 = AISEQ
// word (0x400 max ops), DAT_004663d8 cleared bits 0/2/4 every global tick.
// Argument recovery from the disassembly: the per-tick call in FUN_00129a30 is FUN_001f07b0(ai, 1) (0x129b68);
// variable references pass the operand itself as the id (0x1f0c00.. `lw a1` of the operand before jal 0x1f1730);
// FUN_001ee1a0 is called as (room, destNode, startNode) (0x1edf9c, a1 stored to 0x53de20[0]).
#include "game/character/ai.hpp"
#include "game/character/boss_classes.hpp"
#include "game/respawn.hpp"

#include "alchemy/ps2_float.hpp"
#include "game/camera.hpp"
#include "game/character/character.hpp"
#include "game/dungeon.hpp"
#include "game/game_world.hpp"
#include "game/rng.hpp"
#include "platform/platform.hpp"

#include <bit>
#include <cmath>
#include <cstring>
#include <fstream>
#include <limits>

#pragma fenv_access(on)

namespace baroque::game {

namespace {

constexpr float kNone = -1000.0f;          // 0xc47a0000
constexpr float kFar = 999999.0f;
constexpr float kRadius = 45.0f;           // 0x466420 / 0x466428 / 0x466430 / 0x466438 / 0x466440
constexpr float kStep = 200.0f;            // DAT_00466410 / DAT_00466418
constexpr float kPi = 3.1415927f;

struct AiGlobals {
    std::vector<uint8_t> raw;              // AISEQ image (base *0x53dd58)
    uint32_t maxSteps = 0;                 // DAT_004663e0
    uint32_t indexOffset = 0;              // 0x53dd70 - base
    uint32_t flags = 4;                    // DAT_004663d8: bit0 loaded, bit1 reload request, bit2 AI off, bit4 player AT05
    uint32_t frame = 0;                    // 0x53dd80
    uint32_t wandering = 0;                // 0x53dd88
    uint32_t reloads = 0;                  // 0x53dea0
    std::array<std::vector<AiNode>, 16> nodes;   // 0x53dd90[16] arrays / 0x53ddd0[16] counts
    std::array<int32_t, 64> path{};        // 0x53de20
    int32_t pathCount = 0;                 // 0x53de10
};
AiGlobals g;

Rng& rng() { return gameWorld().rng; }
Dungeon* dungeon() { return gameWorld().dungeon; }

// orig FUN_00124c70
uint32_t* cellAt(const Vec3f& p) {
    Dungeon* d = dungeon();
    if (d == nullptr) return nullptr;
    int cx = 0, cy = 0;
    return d->cellAtWorld(p.data(), &cx, &cy);
}
// UNVERIFIED: the original dereferences these cells without null checks; 0 stands in for the unknown value.
uint32_t cellValue(const uint32_t* c) { return c != nullptr ? *c : 0u; }

uint32_t rd32(uint32_t o) {
    if (static_cast<size_t>(o) + 4 > g.raw.size()) return 0;
    uint32_t v;
    std::memcpy(&v, &g.raw[o], 4);
    return v;
}
uint8_t rd8(uint32_t o) { return o < g.raw.size() ? g.raw[o] : 0; }
uint32_t strEnd(uint32_t o) {                // FUN_0023f6d4 (strlen) -> offset of the terminator
    while (o < g.raw.size() && g.raw[o] != 0) ++o;
    return o;
}
std::string strAt(uint32_t o) {
    const uint32_t e = strEnd(o);
    return o < e ? std::string(reinterpret_cast<const char*>(&g.raw[o]), e - o) : std::string();
}

// `fStack < 2.1474836e+09 ? (uint)f : (int)(f - 2^31) | 0x80000000` (cvt.w.s truncation)
uint32_t toUint(float f) {
    if (f < 2147483648.0f) return static_cast<uint32_t>(alchemy::ps2::ftoi(f));
    return static_cast<uint32_t>(alchemy::ps2::ftoi(f - 2147483648.0f)) | 0x80000000u;
}
float sqrtNz(float sq) { return sq != 0.0f ? std::sqrt(sq) : 0.0f; }
// |v| with the (z*z + x*x) + y*y evaluation order used by FUN_001ed670 / FUN_001f22a0 / FUN_001eda90 / FUN_001efad0
float lenZxy(const Vec3f& v) { return sqrtNz(v[2] * v[2] + v[0] * v[0] + v[1] * v[1]); }
Vec3f sub(const Vec3f& a, const Vec3f& b) { return {a[0] - b[0], a[1] - b[1], a[2] - b[2]}; }
// out = normalize(to - from) with y forced to 0 (the "out[1] = 0; FUN_00206ab0(out)" idiom)
Vec3f flatDir(const Vec3f& to, const Vec3f& from) {
    Vec3f o{to[0] - from[0], 0.0f, to[2] - from[2]};
    normalize(o);
    return o;
}
float yawRad(const Vec3f& v) { return std::atan2(v[0], v[2]); }   // FUN_00206dc0 out[1] (FUN_00238878(x, z))

int roomOfCell(uint32_t v) { return (v & 0xf000u) == 0x1000u ? static_cast<int>(v & 0xfu) : -1; }

bool tagIs(uint32_t id, char c0, char c1, char c2) {
    return (id & 0xffu) == uint8_t(c0) && ((id >> 8) & 0xffu) == uint8_t(c1) && ((id >> 16) & 0xffu) == uint8_t(c2);
}

void clearWaypoint(Ai& ai) {                 // +0x74 = 0.5f; flags = (flags << 33) >> 33
    ai.waypoint[0] = 0.5f;
    ai.flags &= 0x7fffffffu;
}
// TARGET 'd'/0/1 tail: clear the far flag; if a wander destination existed drop it with the waypoint
void dropWander(Ai& ai) {
    const uint32_t old = ai.flags;
    ai.flags = old & 0xefffffffu;
    if ((old & 0x20000000u) != 0) {
        ai.waypoint[0] = 0.5f;
        ai.flags &= 0x7fffffffu;
        ai.flags &= 0xdfffffffu;
    }
}

// FUN_00206e30(out, {0, yaw, 0}) applied to FUN_00207010's (0, 0, 1): third row of FUN_00207090's matrix
Vec3f yawDirDeg(float yawDeg) {
    const float p = (0.0f * kPi) / 180.0f;
    const float y = (yawDeg * kPi) / 180.0f;
    const float sp = std::sin(p), cp = std::cos(p), sy = std::sin(y), cy = std::cos(y);
    return {0.0f + cp * sy, 0.0f + -sp, 0.0f + cp * cy};
}

uint32_t enemyCount() { return floorTagEnemyAlive(); }   // FUN_0012c230 (tag-count table 0x50a710)

}  // namespace

const std::array<std::vector<AiNode>, 16>& aiNodes() { return g.nodes; }
bool aiLoaded() { return !g.raw.empty(); }

// ---------------------------------------------------------------------------------------------------------------
// Loading and global tick
// ---------------------------------------------------------------------------------------------------------------
// orig FUN_001ed200(0): load DATA/AI/AISEQ.BIN, header words -> parts, DAT_004663e0 = max ops, clear the node arrays
bool aiLoad(const std::filesystem::path& assetRoot, std::string* err) {
    std::ifstream in(assetRoot / "DATA/AI/AISEQ.BIN", std::ios::binary);
    if (!in) {
        if (err != nullptr) *err = "DATA/AI/AISEQ.BIN not found";
        return false;
    }
    g.raw.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    if (g.raw.size() < 0x18) {
        g.raw.clear();
        if (err != nullptr) *err = "AISEQ.BIN truncated";
        return false;
    }
    g.maxSteps = rd32(rd32(0));
    g.indexOffset = rd32(8);
    for (auto& n : g.nodes) n.clear();
    g.flags = (g.flags & 4u) | 1u;
    return true;
}

// orig FUN_001ed450
void aiGlobalTick() {
    const uint32_t reload = g.flags & 2u;
    g.flags &= 0xffffffeau;
    if (reload != 0) ++g.reloads;   // FUN_001ed200(0) debug reload: bit1 is never set in the retail build
    Chr* p = characters().player();
    if (!g.raw.empty() && p != nullptr) {
        const char* n = p->act.currentName();
        if (n != nullptr && n[0] == 'A' && n[1] == 'T' && n[2] == '0' && n[3] == '5') g.flags |= 0x10u;
    }
    g.wandering = 0;
    for (const Chr* c : characters().list()) {
        if ((c->taskFlags & 0x10000000u) != 0) continue;
        if (c->type == kChrEnemy && c->active() && (c->ai.flags & 0x20000000u) != 0) ++g.wandering;
    }
    ++g.frame;
    bossDevTick();   // PORT dev options --warp-player-to / --boss-log (boss_classes.cpp); no effect without them
}

// ---------------------------------------------------------------------------------------------------------------
// Line of sight
// ---------------------------------------------------------------------------------------------------------------
namespace {

// orig FUN_001ed670: 1 = blocked
bool gridBlocked(const Vec3f& pos, const Vec3f& delta) {
    const uint32_t* start = cellAt(pos);
    Vec3f dir = delta;
    const float len0 = sqrtNz(dir[2] * dir[2] + dir[0] * dir[0] + dir[1] * dir[1]);
    normalize(dir);
    int steps;
    float len = len0;
    if (len < 200.0f) {
        steps = 0;
    } else {
        steps = alchemy::ps2::ftoi(len) / 200;
        len = len - static_cast<float>(steps * 200);
    }
    const Vec3f rem{dir[0] * len, dir[1] * len, dir[2] * len};
    if (steps >= 0x32) return true;
    Vec3f p = pos;
    const Vec3f step{dir[0] * kStep, dir[1] * kStep, dir[2] * kStep};
    const uint32_t* prev = start;
    int ramps = 0;
    for (;;) {
        const Vec3f& d = steps != 0 ? step : rem;
        p[2] = p[2] + d[2];
        p[1] = p[1] + d[1];
        p[0] = p[0] + d[0];
        const uint32_t* c = cellAt(p);
        if (c == nullptr || *c == 0) return true;
        const uint32_t v = *c;
        const uint32_t attr = v & 0xf0000u;
        if (attr == 0x40000u || attr == 0x30000u || attr == 0x10000u) return true;
        if ((cellValue(prev) & 0x4000000u) == 0) {
            if ((v & 0x4000000u) == 0) {
                const uint32_t lvl = cellValue(start) & 0xf00u;
                if ((v & 0xf00u) != lvl && ramps == 0) return true;
                if ((v & 0xf00u) == lvl && ramps != 0) return true;
            } else {
                if ((cellValue(prev) & 0xc000000u) == 0) return true;
                ++ramps;
            }
        } else {
            if ((v & 0xc000000u) == 0) return true;
            ++ramps;
        }
        if (steps == 0) break;
        --steps;
        prev = c;
    }
    return false;
}

// orig FUN_001ed9a0: true when the cell 200 ahead along `dir` is NOT a ramp (FUN_001eda80 = bit 26)
bool noRampAhead(const Vec3f& pos, const Vec3f& dir) {
    Vec3f d = dir;
    normalize(d);
    d = {d[0] * kStep, d[1] * kStep, d[2] * kStep};
    const Vec3f p{pos[0] + d[0], pos[1] + d[1], pos[2] + d[2]};
    return (cellValue(cellAt(p)) & 0x4000000u) == 0;
}

bool sweepHits(const Vec3f& from, const Vec3f& delta) {    // FUN_00143150(from, delta, &45.0f)
    return raycast(from.data(), delta.data(), kRadius, nullptr);
}

// orig FUN_001f22a0: true = blocked. Cached 16 ticks (+0x60) in flag bit4 (set = clear line).
bool losBlocked(Ai& ai, const Vec3f& from, const Vec3f& delta) {
    const float len = lenZxy(delta);
    if (2000.0f < len) {
        ai.losTimer = 0;
        ai.flags &= 0xffffffefu;
        return true;
    }
    bool blocked;
    if (1000.0f < lenZxy(delta)) {
        ai.losTimer = 0;
        blocked = gridBlocked(from, delta);
    } else if (ai.losTimer == 0) {
        ai.losTimer = 0x10;
        blocked = sweepHits(from, delta);
    } else {
        return (ai.flags & 0x10u) == 0;
    }
    if (blocked) ai.flags &= 0xffffffefu;
    else ai.flags |= 0x10u;
    return blocked;
}

// ---------------------------------------------------------------------------------------------------------------
// Navigation
// ---------------------------------------------------------------------------------------------------------------
// orig FUN_001ef510: random destination node (normal) or a node of the placed room farthest from the player (far mode)
bool pickWander(Chr& c, Vec3f& out) {
    Dungeon* d = dungeon();
    if (c.cell == nullptr || d == nullptr) return false;
    const int count = d->roomCount();                              // FUN_001ef810 = 0x50a680
    if (count == 0) return false;
    auto takeNode = [&](int room) {
        const auto& nodes = g.nodes[static_cast<size_t>(room)];
        const uint32_t k = rng().rand(static_cast<uint32_t>(nodes.size()) - 1u);
        // UNVERIFIED: a placed room without nodes reads its uninitialised array in the original
        out = k < nodes.size() ? nodes[k].pos : Vec3f{0.0f, 0.0f, 0.0f};
    };
    if ((c.ai.flags & 0x10000000u) == 0) {
        bool anyPlaced = false;
        for (int i = 0; i < count; ++i) anyPlaced = anyPlaced || d->room(i).placed();
        if (!anyPlaced) return false;                              // the original loops forever here
        uint32_t k = rng().rand(static_cast<uint32_t>(count));
        for (;;) {
            for (int i = 0; i < count; ++i) {
                if (!d->room(i).placed()) continue;
                if (k == 0) {
                    takeNode(i);
                    return true;
                }
                --k;
            }
        }
    }
    Chr* p = characters().player();
    if (p == nullptr) return false;
    int best = -1;
    float bestD = -kFar;
    for (int i = 0; i < count; ++i) {
        const auto& nodes = g.nodes[static_cast<size_t>(i)];
        if (!d->room(i).placed() || nodes.empty()) continue;
        const Vec3f& pp = p->xf.m.pos;
        const float dy = nodes[0].pos[1] - pp[1];
        const float dx = nodes[0].pos[0] - pp[0];
        const float dz = nodes[0].pos[2] - pp[2];
        const float sq = dz * dz + dx * dx + dy * dy;
        if (bestD < sqrtNz(sq)) {
            best = i;
            bestD = sqrtNz(sq);
        }
    }
    if (best == -1) return false;
    takeNode(best);
    return true;
}

// orig FUN_001ee1a0: depth-first search over node links from `node` to `target` (stored order, no costs)
int32_t dfs(int room, int32_t node, int32_t target) {
    auto& nodes = g.nodes[static_cast<size_t>(room)];
    for (int k = 0; k < 4; ++k) {
        const int32_t l = nodes[static_cast<size_t>(node)].link[static_cast<size_t>(k)];
        if (l == target) {
            nodes[static_cast<size_t>(node)].flags |= 2u;
            return node;
        }
        if (l != -1 && (nodes[static_cast<size_t>(l)].flags & 3u) == 0) {
            g.path[static_cast<size_t>(g.pathCount++)] = l;
            nodes[static_cast<size_t>(l)].flags |= 1u;
            const int32_t r = dfs(room, l, target);
            if (r != -1 && (nodes[static_cast<size_t>(r)].flags & 2u) != 0) return r;
            nodes[static_cast<size_t>(l)].flags &= 0xfffffffeu;
            --g.pathCount;
        }
    }
    return -1;
}

void setWaypoint(Ai& ai, const Vec3f& wp, const Vec3f& wpDest) {
    ai.waypoint = wp;
    ai.flags &= 0xbfffffffu;
    ai.flags |= 0x80000000u;
    ai.waypointDest = wpDest;
}

// orig FUN_001eda90: waypoint toward `dest` inside the room of `cell` through the node graph
bool pathTo(const Vec3f& from, const Vec3f& dest, Chr& c, const uint32_t* cell, Vec3f& out) {
    const int room = roomOfCell(*cell);
    if (room < 0) return false;   // UNVERIFIED: the original indexes the tables with -1 (callers never do this)
    auto& nodes = g.nodes[static_cast<size_t>(room)];
    const int n = static_cast<int>(nodes.size());
    for (auto& nd : nodes) nd.flags &= 0xfffffffcu;
    int32_t best[2] = {-1, -1};
    for (int k = 0; k < 2; ++k) {
        const Vec3f& p = k == 0 ? from : dest;
        float m = kFar;
        for (int j = 0; j < n; ++j) {
            const Vec3f& q = nodes[static_cast<size_t>(j)].pos;
            const float dy = q[1] - p[1], dz = q[2] - p[2], dx = q[0] - p[0];
            const float dist = sqrtNz(dz * dz + dx * dx + dy * dy);
            if (dist < m && !gridBlocked(p, sub(q, p))) {
                best[k] = j;
                m = dist;
            }
        }
    }
    if (best[0] == -1 || best[1] == -1) return false;
    Ai& ai = c.ai;
    if (best[0] == best[1]) {
        if (!gridBlocked(from, sub(dest, from))) {
            out = flatDir(dest, from);
            setWaypoint(ai, dest, dest);
        } else {
            const Vec3f& np = nodes[static_cast<size_t>(best[0])].pos;
            out = flatDir(np, from);
            setWaypoint(ai, np, nodes[static_cast<size_t>(best[1])].pos);
        }
        return true;
    }
    nodes[static_cast<size_t>(best[0])].flags |= 2u;
    nodes[static_cast<size_t>(best[1])].flags |= 2u;
    g.pathCount = 1;
    g.path[0] = best[1];
    if (dfs(room, best[1], best[0]) == -1) return false;
    const int32_t startIdx = g.pathCount;
    g.path[static_cast<size_t>(g.pathCount++)] = best[0];
    const int32_t start = g.path[static_cast<size_t>(startIdx)];
    int32_t cnt = g.pathCount;
    for (int32_t i = 0; i < cnt; ++i) {
        const int32_t pi = g.path[static_cast<size_t>(i)];
        if (start == pi) continue;
        bool linked = false;
        for (int k = 0; k < 4; ++k) linked = linked || nodes[static_cast<size_t>(pi)].link[static_cast<size_t>(k)] == start;
        if (linked) {
            cnt = i + 1;
            g.pathCount = cnt;
            break;
        }
    }
    const Vec3f& wp = nodes[static_cast<size_t>(g.path[static_cast<size_t>(cnt - 1)])].pos;
    out = flatDir(wp, from);
    setWaypoint(ai, wp, nodes[static_cast<size_t>(best[1])].pos);
    return true;
}

// orig FUN_001ee370: next corridor cell from the facing quadrant and the corridor piece id
bool corridorStep(Chr& c, const Vec3f& from, const uint32_t* cell, Vec3f& out) {
    const Vec3f& f = c.xf.m.fwd;
    const float y = (std::atan2(f[0], f[2]) * 180.0f) / kPi;     // FUN_00206d20 out[1]
    const uint32_t piece = (*cell & 0xf000u) == 0x2000u ? (*cell & 0xffu) : 0xffffffffu;
    int a = 0;
    switch (piece) {
    case 0: case 2: case 3:
        a = (y <= 90.0f) ? ((-90.0f < y) ? 2 : 0) : 0;
        break;
    case 1: case 4: case 5:
        a = (y <= 0.0f) ? ((-180.0f < y) ? 1 : 3) : 3;
        break;
    case 6:
        if (135.0f < y || y <= 45.0f) a = (y <= 45.0f) ? ((-45.0f < y) ? 2 : 1) : 1;
        else a = 2;
        break;
    case 7:
        if (135.0f < y || y <= 45.0f) {
            if (45.0f < y || y <= -45.0f) a = (y <= -45.0f) ? ((-135.0f < y) ? 1 : 0) : 0;
            else a = 1;
        }
        break;
    case 8:
        if (135.0f < y || y <= 45.0f) a = (y <= 45.0f) ? ((-45.0f < y) ? 3 : 0) : 0;
        else a = 3;
        break;
    case 9:
        if (135.0f < y || y <= 45.0f) {
            if (45.0f < y || y <= -45.0f) a = (y <= -45.0f) ? ((-135.0f < y) ? 2 : 3) : 3;
            else a = 2;
        } else {
            a = 3;
        }
        break;
    case 10:
        if (135.0f < y || y <= 45.0f) {
            if (45.0f < y || y <= -45.0f) a = (y <= -45.0f) ? ((-135.0f < y) ? 4 : 0xc) : 0xc;
            else a = 9;
        } else {
            a = 5;
        }
        break;
    case 0xb:
        if (135.0f < y || y <= 45.0f) {
            if (45.0f < y || y <= -45.0f) a = (y <= -45.0f) ? ((-135.0f < y) ? 6 : 9) : 9;
            else a = 10;
        } else {
            a = 7;
        }
        break;
    case 0xc:
        if (135.0f < y || y <= 45.0f) {
            if (45.0f < y || y <= -45.0f) a = (y <= -45.0f) ? ((-135.0f < y) ? 0xd : 4) : 4;
            else a = 6;
        } else {
            a = 8;
        }
        break;
    case 0xd:
        if (135.0f < y || y <= 45.0f) {
            if (45.0f < y || y <= -45.0f) a = (y <= -45.0f) ? ((-135.0f < y) ? 8 : 5) : 5;
            else a = 7;
        } else {
            a = 0xb;
        }
        break;
    case 0xe:
        if (135.0f < y || y <= 45.0f) {
            if (45.0f < y || y <= -45.0f) a = (y <= -45.0f) ? ((-135.0f < y) ? 0xb : 10) : 10;
            else a = 0xc;
        } else {
            a = 0xd;
        }
        break;
    default:
        break;
    }
    if (a >= 4) {
        const uint32_t r = rng().next();                          // FUN_002071b0(0x53eb20)
        const bool b0 = (r & 1u) == 0, b1 = (r & 2u) == 0;
        switch (a) {
        case 4: a = b0 ? 1 : 0; break;
        case 5: a = b0 ? 3 : 0; break;
        case 6: a = b0 ? 1 : 2; break;
        case 7: a = b0 ? 3 : 2; break;
        case 8: a = b0 ? 2 : 0; break;
        case 9: a = b0 ? 3 : 1; break;
        case 10: a = b0 ? (b1 ? 2 : 3) : 1; break;
        case 0xb: a = b0 ? (b1 ? 3 : 2) : 0; break;
        case 0xc: a = b0 ? (b1 ? 0 : 1) : 3; break;
        case 0xd: a = b0 ? (b1 ? 1 : 0) : 2; break;
        default: break;
        }
    }
    float ox = 0.0f, oz = 0.0f;                                   // in_f21 / unaff_f20
    switch (a) {
    case 3: ox = 200.0f; oz = 0.0f; break;
    case 2: oz = 200.0f; ox = 0.0f; break;
    case 1: ox = -200.0f; oz = 0.0f; break;
    default: ox = 0.0f; oz = -200.0f; break;
    }
    Ai& ai = c.ai;
    const float wx = static_cast<float>((alchemy::ps2::ftoi(from[0] + ox) / 200) * 200 + 100);
    const float wy = (*cell & 0xf00u) == 0 ? 50.0f : 350.0f;
    const float wz = static_cast<float>((alchemy::ps2::ftoi(from[2] + oz) / 200) * 200 + 100);
    ai.waypoint = {wx, wy, wz};
    ai.flags |= 0xc0000000u;
    out = flatDir(ai.waypoint, from);
    return true;
}

}  // namespace

// orig FUN_001eedf0: unit xz direction toward the destination (direct, cached waypoint, node path or corridor step)
bool aiSteer(Chr& c, Vec3f& out) {
    Ai& ai = c.ai;
    Vec3f dest = ai.dest;                                         // fStack_18..fStack_10
    if (dest[1] <= kNone) return false;
    const uint32_t* cell = c.cell;
    if (cell == nullptr) return false;
    const uint32_t* destCell = cellAt(dest);
    if (destCell == nullptr) return false;
    dest[1] = dest[1] + 50.0f;
    const Vec3f& pos = c.xf.m.pos;
    const Vec3f from{pos[0], pos[1] + 50.0f, pos[2]};             // fStack_28..fStack_20
    Vec3f delta = sub(dest, from);                                // fStack_38..fStack_30
    bool blocked;
    if ((ai.flags & 0x20000000u) == 0) {
        blocked = losBlocked(ai, from, delta);
    } else {
        const float dxz = sqrtNz(delta[2] * delta[2] + delta[0] * delta[0] + 0.0f);
        if (dxz < 60.0f) ai.flags &= 0xdfffffffu;                // arrived at the wander destination
        blocked = true;
    }
    if (!blocked) {
        out = flatDir(dest, from);
        clearWaypoint(ai);
        return true;
    }
    bool recompute = true;
    if ((ai.flags & 0x80000000u) != 0) {
        Vec3f w{ai.waypoint[0] - from[0], 0.0f, ai.waypoint[2] - from[2]};
        const float d = sqrtNz(w[2] * w[2] + w[0] * w[0] + 0.0f);
        if (60.0f <= d) {
            if ((ai.flags & 0x40000000u) == 0) {
                bool goRecompute = false;
                if ((ai.flags & 0x20000000u) == 0) {
                    const Vec3f e = sub(ai.waypointDest, dest);
                    const float el = sqrtNz(e[2] * e[2] + e[0] * e[0] + e[1] * e[1]);
                    goRecompute = 2000.0f <= el || gridBlocked(dest, e);
                }
                if (!goRecompute) {
                    w = sub(ai.waypoint, from);
                    const float wl = sqrtNz(w[2] * w[2] + w[0] * w[0] + w[1] * w[1]);
                    if (wl < 2000.0f && !gridBlocked(from, w)) {
                        out = flatDir(ai.waypoint, from);
                        return true;
                    }
                }
            } else if (100.0f < d) {
                out = flatDir(ai.waypoint, from);
                return true;
            }
        }
    }
    (void)recompute;
    const uint32_t v = *cell;
    if ((v & 0xf000u) != 0x1000u) return corridorStep(c, from, cell, out);
    clearWaypoint(ai);
    const int own = roomOfCell(v);
    const int destRoom = roomOfCell(*destCell);
    if (destRoom == own) {
        if (pathTo(from, dest, c, cell, out)) return true;
    } else {
        const auto& nodes = g.nodes[static_cast<size_t>(own)];
        float m = kFar;
        int best = -1;
        for (int j = 0; j < static_cast<int>(nodes.size()); ++j) {
            const AiNode& nd = nodes[static_cast<size_t>(j)];
            if ((*nd.cell & 0xf000u) != 0x2000u || (*nd.cell & 0x2000000u) == 0) continue;
            const float dy = nd.pos[1] - dest[1], dx = nd.pos[0] - dest[0], dz = nd.pos[2] - dest[2];
            const float sq = dz * dz + dx * dx + dy * dy;
            if (sqrtNz(sq) < m) {
                best = j;
                m = sqrtNz(sq);
            }
        }
        if (best != -1) {
            const Vec3f exitPos = nodes[static_cast<size_t>(best)].pos;
            if (pathTo(from, exitPos, c, cell, out)) return true;
        }
    }
    if (pickWander(c, ai.dest)) clearWaypoint(ai);
    return false;
}

// orig FUN_001f01f0 + FUN_001efad0
void aiBuildNodes() {
    for (auto& n : g.nodes) n.clear();
    Dungeon* d = dungeon();
    if (d == nullptr) return;
    const int count = d->roomCount();
    for (int i = 0; i < count; ++i) {
        Room* room = d->mutableRoom(i);
        if (!room->placed()) continue;
        auto& list = g.nodes[static_cast<size_t>(i)];
        list.reserve(32);                                         // FUN_002077a0(0x4e5e40, 0x480)
        const uint32_t w = static_cast<uint32_t>(room->rdt.w8) * 8u;
        const uint32_t rows = static_cast<uint32_t>(room->rdt.h8) * 8u;
        float fx = 0.0f, fz = 0.0f;
        const Vec3f probe{100.0f, 100.0f, 100.0f};
        int cx = 0, cy = 0;
        if (d->cellAtWorld(probe.data(), &cx, &cy) != nullptr) {
            fx = -(static_cast<float>(cx) * 200.0f);
            fz = -(static_cast<float>(cy) * 200.0f);
        }
        for (uint32_t row = 0; row < rows; ++row) {
            for (uint32_t col = 0; col < w; ++col) {
                const size_t idx = static_cast<size_t>(row) * w + col;
                if (idx >= room->rdt.cells.size()) continue;
                uint32_t* cp = &room->rdt.cells[idx];
                const uint32_t v = *cp;
                if (((v & 0xf0000u) == 0x20000u || (v & 0x2000000u) != 0 || (v & 0x8000000u) != 0) && list.size() < 0x20) {
                    AiNode n;
                    n.pos[1] = (v & 0xf00u) == 0 ? 50.0f : 350.0f;
                    n.pos[0] = fx + static_cast<float>(col) * 200.0f + static_cast<float>(room->posX) * 8.0f * 200.0f +
                               100.0f + 0.0f;
                    n.pos[2] = fz + static_cast<float>(room->posY) * 8.0f * 200.0f + 100.0f + static_cast<float>(row) * 200.0f;
                    n.cell = cp;
                    n.flags = 0;
                    list.push_back(n);
                }
            }
        }
    }
    // FUN_001efad0(room) for every room: up to 4 links per node, one per direction, ramp link reserved
    for (int r = 0; r < count; ++r) {
        if (!d->room(r).placed()) continue;
        aiRoomLinks(r);
    }
}

// orig FUN_001efad0 (work/decompiled/001efad0.c): links of the nodes of one room (also called by the breakable
// objects O??M when they disappear)
void aiRoomLinks(int r) {
    if (r < 0 || r >= 16) return;
    {
        auto& nodes = g.nodes[static_cast<size_t>(r)];
        const int n = static_cast<int>(nodes.size());
        for (int i = 0; i < n; ++i) {
            std::array<float, 32> dist{};
            std::array<float, 32> yaw;
            yaw.fill(std::numeric_limits<float>::quiet_NaN());   // uninitialised stack in the original (never matched)
            int ramp = -1;
            AiNode& ni = nodes[static_cast<size_t>(i)];
            for (int j = 0; j < n; ++j) {
                const AiNode& nj = nodes[static_cast<size_t>(j)];
                const Vec3f delta = sub(nj.pos, ni.pos);          // fStack_10 / fStack_c / fStack_8
                float& dj = dist[static_cast<size_t>(j)];
                if (j == i) {
                    dj = kFar;
                    continue;
                }
                const uint32_t c1 = *nj.cell, c2 = *ni.cell;
                auto measure = [&](bool limit200) {
                    dj = lenZxy(delta);
                    if (limit200 && 200.0f < dj) dj = kFar;
                    else yaw[static_cast<size_t>(j)] = yawRad(delta);
                };
                if ((c1 & 0xf00u) == (c2 & 0xf00u)) {
                    if ((c1 & 0x8000000u) == 0 || (c2 & 0x8000000u) == 0) {
                        const bool jCorr = (c1 & 0xf000u) == 0x2000u;
                        if (jCorr && (c1 & 0x2000000u) != 0) {
                            if ((c2 & 0xf000u) == 0x2000u || (c2 & 0x2000000u) == 0) dj = kFar;
                            else if (!sweepHits(ni.pos, delta)) measure(true);
                            else dj = kFar;
                        } else if ((c2 & 0xf000u) == 0x2000u && (c2 & 0x2000000u) != 0) {
                            if (jCorr || (c1 & 0x2000000u) == 0) dj = kFar;
                            else if (!sweepHits(ni.pos, delta)) measure(true);
                            else dj = kFar;
                        } else {
                            if (!sweepHits(ni.pos, delta)) measure(false);
                            else dj = kFar;
                        }
                    } else {
                        if (!gridBlocked(ni.pos, delta)) measure(false);
                        else dj = kFar;
                    }
                } else if ((c1 & 0x8000000u) == 0 || (c2 & 0x8000000u) == 0) {
                    dj = kFar;
                } else if (!gridBlocked(ni.pos, delta)) {
                    const Vec3f back = sub(ni.pos, nj.pos);       // fStack_20..fStack_18
                    if (!noRampAhead(ni.pos, delta) && !noRampAhead(nj.pos, back)) {
                        measure(false);
                        ramp = j;
                    } else {
                        dj = kFar;
                    }
                } else {
                    dj = kFar;
                }
            }
            ni.link = {-1, -1, -1, -1};
            int maxLinks = ramp == -1 ? 4 : 3;
            int linkCount = 0;
            for (int it = 0; it < n; ++it) {
                int best = -1;
                float m = kFar;
                for (int j = 0; j < n; ++j) {
                    if (!(m <= dist[static_cast<size_t>(j)])) {
                        m = dist[static_cast<size_t>(j)];
                        best = j;
                    }
                }
                if (best == -1) break;
                if (best == ramp) {
                    ramp = -1;
                    maxLinks = 4;
                }
                ++linkCount;
                dist[static_cast<size_t>(best)] = kFar;
                ni.link[static_cast<size_t>(linkCount - 1)] = best;
                if (linkCount == maxLinks) break;
                const float yb = yaw[static_cast<size_t>(best)];
                for (int j = 0; j < n; ++j)
                    if (yb == yaw[static_cast<size_t>(j)]) dist[static_cast<size_t>(j)] = kFar;
            }
            if (ramp != -1) ni.link[static_cast<size_t>(linkCount)] = ramp;
        }
    }
}

// ---------------------------------------------------------------------------------------------------------------
// Variables
// ---------------------------------------------------------------------------------------------------------------
namespace {

uint32_t statusBit(const Chr* c, uint32_t bit) {
    return c != nullptr && (c->status.block.statusMask & bit) != 0 ? 1u : 0u;
}

// orig FUN_001f1730
uint32_t getVar(Ai& ai, uint32_t id) {
    Chr& self = *ai.owner;
    Chr* pl = characters().player();
    if (id >= 0xffffffu && id <= 0x1000006u) return ai.v[id - 0xffffffu];
    switch (id) {
    case 0x1000007: return (ai.flags & 1u) != 0 ? 1u : 0u;
    case 0x1000008: return (ai.flags & 6u) >> 1;
    case 0x1000009: return ai.seed;
    case 0x100000a:
        if ((ai.cache & 1u) == 0) {
            const int32_t hpMax = self.status.block.hpMax, hp = self.status.block.hp;
            const int32_t pct = hpMax != 0 ? (hp * 100) / hpMax : 0;   // UNVERIFIED: hpMax 0 (division by zero)
            ai.hpPct = pct == 0 ? (hp == 0 ? 0u : 1u) : static_cast<uint32_t>(pct);
            ai.cache |= 1u;
        }
        return ai.hpPct;
    case 0x100000b:
    case 0x1000011: {
        const bool mine = id == 0x100000bu;
        if (mine && (ai.cache & 2u) != 0) return ai.myRoom;
        if (!mine && (ai.cache & 0x40u) != 0) return ai.playerRoom;
        const Chr* e = mine ? &self : pl;
        Dungeon* d = dungeon();
        if (e != nullptr && e->cell != nullptr && d != nullptr) {
            const int r = roomOfCell(*e->cell);
            if (r >= 0) {
                const uint32_t b = d->room(r).rdt.roomId;
                if (mine) { ai.cache |= 2u; ai.myRoom = b; }
                else { ai.cache |= 0x40u; ai.playerRoom = b; }
                return b;
            }
        }
        if (mine) { ai.cache |= 2u; ai.myRoom = 0xffffu; return 0xffffu; }
        ai.cache |= 0x40u;
        ai.playerRoom = 0xfffeu;
        return 0xfffeu;
    }
    case 0x100000c:
    case 0x100000d:
        if ((ai.cache & 4u) == 0) {
            if (pl == nullptr) {
                ai.distXz = 0xffffu;
                ai.dist3d = 0xffffu;
            } else {
                const Vec3f& a = pl->xf.m.pos;
                const Vec3f& b = self.xf.m.pos;
                const float dy = a[1] - b[1];
                const float dx2 = (a[0] - b[0]) * (a[0] - b[0]);
                const float dz2 = (a[2] - b[2]) * (a[2] - b[2]);
                const float sq = dz2 + dy * dy + dx2 + 0.0f;
                ai.dist3d = toUint(std::fabs(sqrtNz(sq) + 0.5f));
                const float sq2 = dz2 + dx2 + 0.0f;
                ai.distXz = toUint(std::fabs(sqrtNz(sq2) + 0.5f));
            }
            ai.cache |= 4u;
        }
        return id == 0x100000cu ? ai.dist3d : ai.distXz;
    case 0x100000e:
        if ((ai.cache & 0x10u) == 0) {
            if (pl == nullptr) ai.distY = 0xffffu;
            else ai.distY = toUint(std::fabs((pl->xf.m.pos[1] - self.xf.m.pos[1]) + 0.5f));
            ai.cache |= 0x10u;
        }
        return ai.distY;
    case 0x100000f:
        return pl != nullptr && self.xf.m.pos[1] < pl->xf.m.pos[1] ? 1u : 0u;
    case 0x1000010:
        if ((ai.cache & 0x20u) == 0) {
            if (pl == nullptr) {
                ai.angle = 0xffffu;
            } else {
                Vec3f v{pl->xf.m.pos[0] - self.xf.m.pos[0], 0.0f, pl->xf.m.pos[2] - self.xf.m.pos[2]};
                normalize(v);
                const Vec3f& f = self.xf.m.fwd;
                const float a = ((v[2] * f[2] + v[0] * f[0] + v[1] * f[1]) - 1.0f) * -90.0f;
                ai.angle = toUint(a);
            }
            ai.cache |= 0x20u;
        }
        return ai.angle;
    case 0x1000012:
        if ((ai.cache & 0x80u) == 0) {
            if (pl == nullptr) {
                ai.canSee = 0;
            } else {
                const Vec3f from{self.xf.m.pos[0], self.xf.m.pos[1] + 50.0f, self.xf.m.pos[2]};
                Vec3f d{pl->xf.m.pos[0] - from[0], (pl->xf.m.pos[1] + 50.0f) - from[1], pl->xf.m.pos[2] - from[2]};
                float len = lenZxy(d);
                bool blocked = false;
                if (90.0f < len) {
                    normalize(d);
                    len = len - 90.0f;
                    d = {d[0] * len, d[1] * len, d[2] * len};
                    blocked = losBlocked(ai, from, d);
                }
                ai.canSee = blocked ? 0u : 1u;
            }
            ai.cache |= 0x80u;
        }
        return ai.canSee;
    case 0x1000013: return g.flags & 0x10u;
    case 0x1000014: return g.frame;
    case 0x1000015: return self.status.block.carried != nullptr ? 1u : 0u;
    case 0x1000016:
        if ((ai.flags & 0x10000000u) != 0) return 3;
        if ((ai.flags & 0x20000000u) != 0) return 2;
        if (ai.target == nullptr) return 100;
        return ai.target == pl ? 0u : 100u;
    case 0x100001b: return g.wandering;
    case 0x100001c: return enemyCount();
    case 0x100001d: return statusBit(&self, 1u);
    case 0x100001e: return statusBit(&self, 2u);
    case 0x100001f: return statusBit(&self, 4u);
    case 0x1000020: return statusBit(&self, 8u);
    case 0x1000021: return statusBit(&self, 0x10u);
    case 0x1000022: return statusBit(&self, 0x20u);
    case 0x1000023: return statusBit(&self, 0x10000u);
    case 0x1000024: return statusBit(&self, 0x40000u);
    case 0x1000025: return statusBit(&self, 0x2000u);
    case 0x1000026: return statusBit(pl, 1u);
    case 0x1000027: return statusBit(pl, 2u);
    case 0x1000028: return statusBit(pl, 4u);
    case 0x1000029: return statusBit(pl, 8u);
    case 0x100002a: return statusBit(pl, 0x10u);
    case 0x100002b: return statusBit(pl, 0x20u);
    case 0x100002c: return statusBit(pl, 0x10000u);
    case 0x100002d: return statusBit(pl, 0x40000u);
    case 0x100002e: return statusBit(pl, 0x2000u);
    default: return 0;
    }
}

// orig FUN_001f1690
void setVar(Ai& ai, uint32_t id, uint32_t value) {
    if (id >= 0xffffffu && id <= 0x1000006u) {
        ai.v[id - 0xffffffu] = value;
    } else if (id == 0x1000007u) {
        if (value == 0) ai.flags &= 0xfffffffeu;
        else ai.flags |= 1u;
    } else if (id == 0x1000008u) {
        ai.flags = (ai.flags & 0xfffffff9u) | ((value & 3u) << 1);
    }
}

uint32_t operand(Ai& ai, uint32_t v) { return 0xfffffeu < v ? getVar(ai, v) : v; }

// orig FUN_001f0750
void enterState(Ai& ai, int state) {
    const uint32_t ofs = rd32(ai.stateTable + static_cast<uint32_t>(state) * 4u);
    ai.pc = ofs == 0 ? 0u : ofs;
}

// E16* / E19* facing snap onto the corridor piece axis (FUN_00206e30 + FUN_001287a0)
void snapToPiece(Ai& ai, Chr& c, bool inverted) {
    const uint32_t v = *c.cell;
    if ((v & 0xf000u) != 0x2000u) return;
    const Vec3f& t = ai.target->xf.m.pos;
    const Vec3f& s = c.xf.m.pos;
    float yaw;
    switch (v & 0xffu) {
    case 0: case 2: case 3:
        yaw = (t[2] < s[2]) ? (inverted ? 180.0f : 0.0f) : (inverted ? 0.0f : 180.0f);
        break;
    case 1: case 4: case 5:
        yaw = (t[0] < s[0]) ? (inverted ? -90.0f : 90.0f) : (inverted ? 90.0f : -90.0f);
        break;
    default:
        return;
    }
    snapForward(c.xf.m, yawDirDeg(yaw));
}

}  // namespace

// orig FUN_001f0630
void aiInit(Ai& ai, Chr& owner) {
    ai.owner = &owner;
    ai.target = nullptr;
    ai.dest[1] = kNone;
    ai.pc = 0;
    ai.flags = 0;
    ai.losTimer = 0;
    ai.seed = rng().next();                                       // FUN_002071b0(0x53eb20)
    std::memset(ai.pending, 0, sizeof ai.pending);
    if (g.raw.empty()) return;                                    // AISEQ not loaded (the original would crash)
    uint32_t o = g.indexOffset, ofs = 0;
    for (;;) {
        const uint32_t id = rd32(o);
        ofs = rd32(o + 4);
        o += 8;
        if (id == 0x20202020u || id == owner.id || o >= g.raw.size()) break;
    }
    ai.recOffset = ofs;
    ai.stateTable = ofs;                                          // base + ofs (offsets are file-relative here)
    enterState(ai, 1);
    aiRun(ai, false);
    enterState(ai, 0);
    std::memset(ai.pending, 0, sizeof ai.pending);
}

// orig FUN_001f2240
bool aiOnHit(Ai& ai) {
    ai.flags &= 0xfffffff7u;
    enterState(ai, 2);
    aiRun(ai, false);
    enterState(ai, 0);
    return (ai.flags & 8u) != 0;
}

// orig FUN_001f07b0
int aiRun(Ai& ai, bool tick) {
    if (ai.owner == nullptr) return 0;
    Chr& c = *ai.owner;
    if (tick) {
        Dungeon* d = dungeon();
        if (d == nullptr || (d->floorFlags() & 1u) == 0) return 0;
        if (!c.active()) return 0;
        if ((c.type == kChrPlayerType || c.type == kChrEnemy) && c.status.block.hp == 0) return 0;
    }
    if ((g.flags & 4u) != 0) return 0;
    uint32_t pc = ai.pc;
    if (pc == 0) return 0;
    if (tick) {
        if (ai.losTimer != 0) --ai.losTimer;
        ai.cache = 0;
        if (ai.pending[0] != '\0') {
            const std::string name(ai.pending, strnlen(ai.pending, sizeof ai.pending));
            if (c.act.has(name)) c.act.start(name);              // FUN_00143e80 / FUN_00143a40
            ai.pending[0] = '\0';
        }
    }
    float turnOfs = 0.0f;   // fStack_10
    float rate = 0.0f;      // fStack_c
    bool running = true;    // bVar5
    bool turn = false;      // bVar6
    uint32_t ret = 0;       // pfVar15
    auto jump = [&](int32_t target) { return ai.stateTable + (static_cast<uint32_t>(target) - ai.recOffset); };
    for (uint32_t steps = 0;; ++steps) {
        if (pc == 0) return 0;
        if (g.maxSteps <= steps) return 0;
        if (pc >= g.raw.size()) {                                 // UNVERIFIED guard: past the image
            ai.pc = 0;
            return 0;
        }
        const uint8_t op = rd8(pc);
        const uint32_t p1 = pc + 1;
        switch (op) {
        case 0x22:                                                // TURN_OFS
            turnOfs = std::bit_cast<float>(rd32(p1));
            pc += 5;
            break;
        case 0x21:                                                // TURN_TO_DEST
            turn = true;
            rate = std::bit_cast<float>(rd32(p1));
            pc += 5;
            break;
        case 0x20: {                                              // TARGET
            const uint8_t sub = rd8(p1);
            pc += 2;
            if (sub == 3 || sub == 2) {
                ai.target = nullptr;
                if (sub == 3) ai.flags |= 0x10000000u;
                else ai.flags &= 0xefffffffu;
                if ((ai.flags & 0x20000000u) == 0 && pickWander(c, ai.dest)) {
                    ai.flags |= 0x20000000u;
                    ai.waypoint[0] = 0.5f;
                    ai.flags &= 0x7fffffffu;
                }
            } else if (sub == 1 || sub == 0) {
                Chr* pl = characters().player();
                ai.target = pl;
                if (pl != nullptr) ai.dest = pl->xf.m.pos;        // the original dereferences 0x50b144 unchecked
                dropWander(ai);
            } else {                                              // 'd' and any other value
                ai.target = nullptr;
                ai.dest[1] = kNone;
                dropWander(ai);
            }
            break;
        }
        case 0x15: case 0x14: case 0x13: case 0x10: {
            const uint32_t var = rd32(p1);
            const uint32_t val = operand(ai, rd32(pc + 5));
            pc += 9;
            const uint32_t cur = op == 0x10 ? 0u : getVar(ai, var);
            if (op == 0x15) setVar(ai, var, val & cur);
            else if (op == 0x14) setVar(ai, var, cur < val ? 0u : cur - val);
            else if (op == 0x13) setVar(ai, var, val + cur);
            else setVar(ai, var, val);
            break;
        }
        case 0x12: {                                              // RAND
            const uint32_t var = rd32(p1);
            const uint32_t range = rd32(pc + 5);
            pc += 9;
            setVar(ai, var, rng().rand(range));                   // FUN_002071e0(0x53eb20, range)
            break;
        }
        case 0x0f:
            pc += 5;
            break;
        case 0x0e:
            pc = strEnd(p1) + 1;
            break;
        case 0x06:
            ai.flags |= 8u;
            pc = p1;
            break;
        case 0x05: {                                              // IF_ID (float compare of the id bits)
            const float id = std::bit_cast<float>(rd32(p1));
            const int32_t target = static_cast<int32_t>(rd32(pc + 5));
            const uint8_t call = rd8(pc + 9);
            pc += 10;
            if (id == std::bit_cast<float>(c.id)) {
                if (call != 0) ret = pc;
                pc = jump(target);
            }
            break;
        }
        case 0x04: {                                              // IF var cmp value
            const uint32_t a = getVar(ai, rd32(p1));
            const uint32_t b = operand(ai, rd32(pc + 5));
            const uint8_t cmp = rd8(pc + 9);
            const int32_t target = static_cast<int32_t>(rd32(pc + 10));
            const uint8_t call = rd8(pc + 14);
            pc += 15;
            bool taken = false;
            switch (cmp) {
            case 0: taken = a == b; break;
            case 1: taken = a != b; break;
            case 2: taken = b < a; break;
            case 3: taken = b <= a; break;
            case 4: taken = a < b; break;
            case 5: taken = a <= b; break;
            case 6: taken = (a & b) != 0; break;
            default: break;
            }
            if (taken) {
                if (call != 0) ret = pc;
                pc = jump(target);
            }
            break;
        }
        case 0x03: {                                              // IF_ACTION
            const std::string name = strAt(p1);
            const uint32_t q = strEnd(p1) + 1;
            const int32_t target = static_cast<int32_t>(rd32(q));
            const uint8_t call = rd8(q + 4);
            pc = q + 5;
            bool taken = false;
            if (const char* cur = c.act.currentName(); cur != nullptr) {
                if (name == " ") taken = true;                    // 0x4e5cb0
                else if (name == cur) taken = true;               // FUN_001f2430(name, FUN_00143d30(act, -1))
            }
            if (taken) {
                if (call != 0) ret = pc;
                pc = jump(target);
            }
            break;
        }
        case 0x02: {                                              // GOTO / CALL
            const int32_t target = static_cast<int32_t>(rd32(p1));
            if (rd8(pc + 5) != 0) ret = pc + 6;
            pc = jump(target);
            break;
        }
        case 0x01: {                                              // ACTION
            const std::string name = strAt(p1);
            pc = strEnd(p1) + 1;
            if (c.act.has(name)) c.act.start(name);
            break;
        }
        case 0x00:                                                // END
            if (ret == 0) {
                running = false;
                pc = p1;
            } else {
                pc = ret;
                ret = 0;
            }
            break;
        default:
            ai.pc = 0;
            return 0;
        }
        if (running) continue;
        // post-step
        if (turn) {
            if (ai.dest[1] <= kNone) return 1;
            Vec3f dir{};
            if (aiSteer(c, dir)) {
                if (turnOfs != 0.0f) rotateY((turnOfs * kPi) / 180.0f, dir.data());   // FUN_00206be0
                turnToward(rate, c.xf.m, dir, true);                                  // FUN_00128fc0(rate, chr, dir, 1)
            }
        } else if (ai.target != nullptr) {
            if ((ai.flags & 1u) == 0 && tagIs(c.id, 'E', '1', '6')) {
                if (c.cell == nullptr) return 1;
                snapToPiece(ai, c, false);
                return 1;
            }
            if (getVar(ai, 0x1000008u) == 0 && tagIs(c.id, 'E', '1', '9') && c.cell != nullptr) snapToPiece(ai, c, true);
        }
        return 1;
    }
}

uint32_t aiVar(Ai& ai, uint32_t id) { return getVar(ai, id); }   // orig FUN_001f1730

// orig FUN_001ef820 (work/decompiled/001ef820.c, float order from the disassembly 0x1ef820..0x1efac0): ACT Warp 3 of a
// boss child (AI flag 0x100). Four candidates around the AI target (+0x64), one per quadrant from a random base angle
// (rand(0x8000)/32768 * 90 deg), 400..600 away (rand(0x8000) each); a candidate is free when the sphere sweep
// FUN_00143150(target pos, offset, &own phys radius +0x14) hits nothing; rand(free - 1) picks one of the free ones.
bool aiWarpPoint(Chr& c, Vec3f& out) {
    const Chr* t = c.ai.target;
    if (t == nullptr) return false;
    const float base = (static_cast<float>(static_cast<int>(rng().rand(0x8000))) / 32768.0f) * 90.0f;   // f20
    float cand[4][3];
    bool freeSlot[4];
    uint32_t nFree = 0;
    for (int i = 0; i < 4; ++i) {
        cand[i][0] = 0.0f;
        cand[i][1] = 0.0f;
        const float r = static_cast<float>(static_cast<int>(rng().rand(0x8000))) / 32768.0f;
        cand[i][2] = (0.0f + 400.0f) + 200.0f * r;                                   // adda.s / madd.s
        const float deg = (0.0f + base) + 90.0f * static_cast<float>(i);
        rotateY((kPi * deg) / 180.0f, cand[i]);                                      // FUN_00206be0
        const Vec3f& tp = t->xf.m.pos;                                               // target +0x20 -> +0xc
        if (!raycast(tp.data(), cand[i], c.phys.radius, nullptr)) {                  // FUN_00143150 == 0
            ++nFree;
            freeSlot[i] = true;
        } else {
            freeSlot[i] = false;
        }
    }
    if (nFree == 0) return false;
    uint32_t k = rng().rand(nFree - 1u);
    for (int i = 0; i < 4; ++i) {
        if (!freeSlot[i]) continue;
        if (k == 0) {
            const Vec3f& tp = t->xf.m.pos;
            out = {tp[0] + cand[i][0], tp[1] + cand[i][1], tp[2] + cand[i][2]};
            return true;
        }
        --k;
    }
    return false;
}

}  // namespace baroque::game
