#pragma once
// Floor population runtime of the character manager 0x50b130 (work/reports/AUDIT_WORLD_2026-10-10.md W5, W2):
//   - the floor tag-count table 0x50a710: 10 slots {tag, alive, max alive, max at load}, kept by the kind-0 list
//     insert / remove (FUN_00128160 -> FUN_00127ea0, FUN_00128230 -> FUN_00127f90), snapshotted after the floor's
//     initial spawns (thunk_FUN_00127ff0 in FUN_0011ffc0), cleared at the floor exit (FUN_0012bfd0) and by
//     FUN_0012bed0 / FUN_00128430;
//   - the enemy respawn FUN_0012c880 (game task FUN_0011d850, every 900 ticks on non-scenario floors);
//   - FUN_001d9a20 (floor 100: the inventory's +0xbc "outer box" items are scattered over the floor).
#include <array>
#include <cstdint>

namespace baroque::game {

class Dungeon;

struct FloorTagCount {
    uint32_t tag = 0;        // +0x0
    uint32_t alive = 0;      // +0x4
    uint32_t maxAlive = 0;   // +0x8
    uint32_t maxAtLoad = 0;  // +0xc
};

const std::array<FloorTagCount, 10>& floorTagCounts();   // 0x50a710

void floorTagInsert(uint32_t tag);     // orig FUN_00127ea0 (FUN_00128160, kind 0 = types 0..2)
void floorTagRemove(uint32_t tag);     // orig FUN_00127f90 (FUN_00128230 of the entity dtor FUN_00128610)
void floorTagSnapshot();               // orig FUN_00127ff0 / thunk_FUN_00127ff0 (FUN_0011ffc0)
void floorTagFloorExit(uint32_t playerTag);   // table loop of FUN_0012bfd0 (after the list walk)
void floorTagClearAll();               // table loop of FUN_0012bed0 / FUN_00128430
uint32_t floorTagEnemyAlive();         // orig FUN_0012c230 (sum of the alive counts of the 'E' slots)

void respawnTick(Dungeon* dungeon, int floorId);   // orig FUN_0012c880(0x50b130)
void scatterOuterBoxes(Dungeon& dungeon);           // orig FUN_001d9a20(0x53d910)

}  // namespace baroque::game
