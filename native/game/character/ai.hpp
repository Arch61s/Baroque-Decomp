#pragma once
// Enemy/NPC AI (ENEMIES_COMBAT.md §2): AI component (character components +0x14, vtable 0x4cf150), translated from
//   FUN_001ed200 AISEQ load, FUN_001ed450 global tick, FUN_001f0630 init, FUN_001f0750 state entry,
//   FUN_001f07b0 interpreter, FUN_001f1730 / FUN_001f1690 variables, FUN_001f2240 on-hit state,
//   FUN_001eedf0 steering, FUN_001ef510 wander destination, FUN_001ee370 corridor step, FUN_001eda90 / FUN_001ee1a0
//   node path, FUN_001f01f0 / FUN_001efad0 node graph, FUN_001ed670 grid line of sight, FUN_001ed9a0 ramp probe,
//   FUN_001f22a0 cached line of sight.
// Byte code: DATA/AI/AISEQ.BIN (data/aiseq.hpp); the interpreter executes the raw image in place like the original.
// Run inside an alchemy::ps2::Ps2FloatScope (the game tick opens it).
#include "game/collision.hpp"

#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace baroque::game {

struct Chr;

// AI object (FUN_001f05c0 ctor). Offsets are the original's.
struct Ai {
    Chr* owner = nullptr;          // +0x04
    uint32_t recOffset = 0;        // +0x08 record file offset (index entry)
    uint32_t stateTable = 0;       // +0x0c file offset of the state table (= recOffset)
    uint32_t pc = 0;               // +0x10 file offset of the current state's code (0 = halted)
    uint32_t flags = 0;            // +0x14 bit0 FLAG, bits1-2 MODE, bit3 handled, bit4 LOS clear (cached),
                                   //       0x10000000 wander-far, 0x20000000 wander dest, 0x40000000 corridor waypoint,
                                   //       0x80000000 waypoint valid
    uint32_t seed = 0;             // +0x18
    std::array<uint32_t, 8> v{};   // +0x1c..+0x38 (variable ids 0xffffff, 0x1000000..0x1000006)
    uint32_t cache = 0;            // +0x3c per-tick cache bits
    uint32_t hpPct = 0;            // +0x40
    uint32_t myRoom = 0;           // +0x44
    uint32_t dist3d = 0;           // +0x48
    uint32_t distXz = 0;           // +0x4c
    uint32_t distY = 0;            // +0x50
    uint32_t angle = 0;            // +0x54
    uint32_t playerRoom = 0;       // +0x58
    uint32_t canSee = 0;           // +0x5c
    int32_t losTimer = 0;          // +0x60
    Chr* target = nullptr;         // +0x64
    Vec3f dest{0.0f, -1000.0f, 0.0f};  // +0x68 (y <= -1000 = none)
    Vec3f waypoint{};              // +0x74
    Vec3f waypointDest{};          // +0x80
    char pending[8] = {};          // +0x8c pending action name
};

// Navigation node (0x24 bytes; 0x53dd90[room] arrays, counts 0x53ddd0[room]).
struct AiNode {
    uint32_t flags = 0;            // +0x00 bit0 visited, bit1 path end (FUN_001ee1a0)
    uint32_t* cell = nullptr;      // +0x04
    Vec3f pos{};                   // +0x08
    std::array<int32_t, 4> link{-1, -1, -1, -1};   // +0x14
};

bool aiLoad(const std::filesystem::path& assetRoot, std::string* err);   // orig FUN_001ed200(0) (boot, FUN_00100730)
bool aiLoaded();
void aiGlobalTick();               // orig FUN_001ed450 (every engine tick, before the task list)
void aiBuildNodes();               // orig FUN_001f01f0 (end of the floor finalize FUN_00120060)
void aiRoomLinks(int room);        // orig FUN_001efad0 (node links of one room)
void aiInit(Ai& ai, Chr& owner);   // orig FUN_001f0630
int aiRun(Ai& ai, bool tick);      // orig FUN_001f07b0
uint32_t aiVar(Ai& ai, uint32_t id);   // orig FUN_001f1730 (script variable / AI query)
bool aiOnHit(Ai& ai);              // orig FUN_001f2240 (hit code FUN_0014c970)
bool aiSteer(Chr& c, Vec3f& out);  // orig FUN_001eedf0
bool aiWarpPoint(Chr& c, Vec3f& out);   // orig FUN_001ef820 (ACT Warp 3 of a boss child: free point 400..600 from the target)
const std::array<std::vector<AiNode>, 16>& aiNodes();   // debug view of 0x53dd90/0x53ddd0

}  // namespace baroque::game
