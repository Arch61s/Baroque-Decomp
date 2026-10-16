#pragma once
// Static generator tables copied from the SLUS_217.14 ELF (.data), see work/reports/MAP_FORMATS.md section 9.
// Values were read with tools/disasm.py word reads; scenario layouts are generated into dungeon_tables.cpp.
#include <cstdint>

namespace baroque::game::tables {

// 0x48de20 delta[dir] = N(0,-1) E(1,0) S(0,1) W(-1,0)   (corridor walk, FUN_001257f0)
inline constexpr int32_t kDirDelta[4][2] = {{0, -1}, {1, 0}, {0, 1}, {-1, 0}};
// 0x48de40 same deltas used for the blocked-door cell (FUN_00125fc0)
inline constexpr int32_t kDoorCellDelta[4][2] = {{0, -1}, {1, 0}, {0, 1}, {-1, 0}};
// 0x48de00 maskOut[dir] = {1,8,2,4}  0x48de10 maskIn[dir] = {2,4,1,8}   (N=1,S=2,W=4,E=8)
inline constexpr uint32_t kDirMaskOut[4] = {1, 8, 2, 4};
inline constexpr uint32_t kDirMaskIn[4] = {2, 4, 1, 8};
// 0x48dd70 pieceByMask[16] (FUN_001252d0)
inline constexpr int32_t kPieceByMask[16] = {-1, 0, 0, 0, 1, 7, 6, 12, 1, 8, 9, 13, 1, 10, 11, 14};
// 0x48ddb0 maskByPiece[16] (FUN_001252f0)
inline constexpr uint32_t kMaskByPiece[16] = {3, 12, 0, 0, 0, 0, 6, 5, 9, 10, 13, 14, 7, 11, 15, 0};
// 0x48ddf0 endCap[dir] = {2,5,3,4} (FUN_00125350)
inline constexpr uint32_t kEndCapByDir[4] = {2, 5, 3, 4};
// 0x48de60 blockedDoor[dir] = {0x10,0x11,0x0f,0x12} (FUN_00125fc0)
inline constexpr uint32_t kBlockedDoorByDir[4] = {0x10, 0x11, 0x0f, 0x12};

// 0x48dcd0 model[19] {u32 mt; f32 rotY} for object ids 0..18 (FUN_00123280)
struct PieceModel { uint32_t mt; float rotY; };
inline constexpr PieceModel kPieceModel[19] = {
    {0, 0.0f}, {0, 1.5707963705062866f},                                                   // 0,1 straight
    {2, 0.0f}, {2, 3.1415927410125732f}, {2, 1.5707963705062866f}, {2, -1.5707963705062866f}, // 2..5 end caps
    {6, 0.0f}, {6, -1.5707963705062866f}, {6, 3.1415927410125732f}, {6, 1.5707963705062866f}, // 6..9 corners
    {10, 0.0f}, {10, 3.1415927410125732f}, {10, 1.5707963705062866f}, {10, -1.5707963705062866f}, // 10..13 T
    {14, 0.0f},                                                                            // 14 cross
    {15, 0.0f}, {15, 3.1415927410125732f}, {15, 1.5707963705062866f}, {15, -1.5707963705062866f}, // 15..18 blocked door
};
// 0x44a140 MT ids that have an IGB model (FUN_0011e1c0)
inline constexpr int32_t kMtIdsWithModel[6] = {0, 6, 10, 14, 2, 15};

// 0x48dbe0 C05 quest table {flagId, tag} x 11 (FUN_00120f70); tags filled at runtime from 0x44a488..0x44a4d8
struct FlagTag { uint32_t flag; uint32_t tag; };
extern const FlagTag kC05Table[11];
// 0x48dc40 C07A table {flag, floorIdA, floorIdB, roomId} x 6 (FUN_00120f70)
struct C07Entry { uint32_t flag; int32_t floorA; int32_t floorB; uint32_t roomId; };
inline constexpr C07Entry kC07Table[6] = {
    {1634, 102, 0, 2}, {1635, 106, 206, 13}, {1636, 108, 208, 10}, {1637, 111, 211, 10}, {204, 114, 214, 10}, {0, 120, 0, 3}};
// 0x44a500 fixed item placements for ids 100-299 {u8 mapSet, u8 roomId, u16 pad, tag, f32 x,y,z} x 2 (FUN_00121f80);
// tags LQ22 (0x44a528) and MT00 (0x44a3e8) are written at runtime.
struct FixedItem { uint8_t mapSet; uint8_t roomId; uint32_t tag; float x, y, z; };
extern const FixedItem kFixedItems[2];
// 0x48dca0 town stairs {tag, f32 x, f32 z} x 3 (FUN_001222d0); tags PT10/PT11/PT12 (0x44a530..) written at runtime.
struct TownStairs { uint32_t tag; float x, z; };
extern const TownStairs kTownStairs[3];
// 0x44a210 extra spawns {s32 floorId; u32 tag; s32 count} x 24 (FUN_0011e7c0 / FUN_00121a00); tags filled at runtime
// from the strings at 0x44a330..0x44a388.
struct ExtraSpawn { int32_t floorId; uint32_t tag; int32_t count; };
extern const ExtraSpawn kExtraSpawns[24];
// 0x463eb0 item category prefixes (FUN_001d38f0 compares the first two tag chars against 15 entries)
extern const char* const kItemPrefixes[15];

// Scenario layouts (FUN_0011e3d0): pointer tables 0x44a1b0 / 0x44a1d0 / 0x44a1f0 indexed by floorNumber-1.
struct ScenarioConn { uint8_t room, door, room2, door2; };            // conn_t, terminated by room==0xff; door==0xff = place only
struct ScenarioStart { int32_t room; uint32_t tag; float x, y, z; int32_t angleDeg; }; // spawn_t +0 (player) / +0x18 (exit)
struct ScenarioEntity { uint8_t room; uint32_t tag; float x, y, z; uint32_t flag; };   // from spawn_t +0x30, terminated by room==0xff
struct ScenarioLayout {
    uint32_t entryAddr;            // ELF address of the {conn*, spawn*} entry (FUN_00120a70 compares 0x48d8a8)
    const ScenarioConn* conns;
    ScenarioStart start;
    ScenarioStart exit;
    const ScenarioEntity* entities;
    int entityCount;
};
extern const ScenarioLayout* const kScenarioLayoutsA[7];
extern const ScenarioLayout* const kScenarioLayoutsB[7];
extern const ScenarioLayout* const kScenarioLayoutsC[7];

} // namespace baroque::game::tables
