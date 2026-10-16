// Floor population runtime of the character manager 0x50b130: tag-count table 0x50a710, enemy respawn FUN_0012c880,
// floor-100 outer boxes FUN_001d9a20. See respawn.hpp.
#include "game/respawn.hpp"

#include <algorithm>
#include <vector>

#include "game/character/character.hpp"
#include "game/dungeon.hpp"
#include "game/flag_manager.hpp"
#include "game/game_world.hpp"
#include "game/hud/hud_api.hpp"
#include "game/items/inventory.hpp"
#include "game/items/item.hpp"
#include "game/items/item_world.hpp"

namespace baroque::game {
namespace {

std::array<FloorTagCount, 10> gTable{};   // 0x50a710 (FUN_00128430: zero)
int32_t gRespawnTimer = 0;                // 0x50b130 +0x18 (FUN_0012b9f0: 0; only FUN_0012c880 writes it)

constexpr uint32_t kTagE16 = 'E' | ('1' << 8) | ('6' << 16);   // 0x44aa08 "E16"
constexpr uint32_t kTagE19 = 'E' | ('1' << 8) | ('9' << 16);   // 0x44a738 "E19"

}  // namespace

const std::array<FloorTagCount, 10>& floorTagCounts() { return gTable; }

// orig FUN_00127ea0 (work/decompiled/00127ea0.c): a slot holding the tag counts it; otherwise the first slot whose
// alive count is 0 is claimed (its max is NOT reset, the original compares against the old value).
void floorTagInsert(uint32_t tag) {
    for (int i = 0; i < 10; ++i) {
        FloorTagCount& s = gTable[size_t(i)];
        if (tag == s.tag) {
            const uint32_t n = s.alive + 1;
            s.alive = n;
            if (n <= s.maxAlive) return;
            s.maxAlive = n;
            return;
        }
    }
    for (int i = 0; i < 10; ++i) {
        FloorTagCount& s = gTable[size_t(i)];
        if (s.alive == 0) {
            s.tag = tag;
            const uint32_t n = s.alive + 1;
            s.alive = n;
            if (n <= s.maxAlive) return;
            s.maxAlive = n;
            return;
        }
    }
}

// orig FUN_00127f90 (work/decompiled/00127f90.c)
void floorTagRemove(uint32_t tag) {
    for (int i = 0; i < 10; ++i) {
        if (tag == gTable[size_t(i)].tag) {
            gTable[size_t(i)].alive = gTable[size_t(i)].alive - 1;
            return;
        }
    }
}

// orig FUN_00127ff0 (work/decompiled/00127ff0.c; thunk 0012cb80.c): +0xc = +8 for the 10 slots
void floorTagSnapshot() {
    for (FloorTagCount& s : gTable) s.maxAtLoad = s.maxAlive;
}

// FUN_0012bfd0 (work/decompiled/0012bfd0.c) after its list walk: every used slot whose tag is not the player's
// (mgr +0x14 -> +0x1c) is released (FUN_0012b7f0, model cache: the port loads per character) and zeroed.
void floorTagFloorExit(uint32_t playerTag) {
    for (FloorTagCount& s : gTable) {
        if (s.tag != 0 && s.tag != playerTag) s = FloorTagCount{};
    }
}

// FUN_0012bed0 (work/decompiled/0012bed0.c) table loop (every used slot zeroed) / FUN_00128430 (whole table zeroed)
void floorTagClearAll() {
    gTable.fill(FloorTagCount{});
}

// orig FUN_0012c230 (work/decompiled/0012c230.c)
uint32_t floorTagEnemyAlive() {
    uint32_t n = 0;
    for (const FloorTagCount& s : gTable)
        if (static_cast<char>(s.tag & 0xff) == 'E') n += s.alive;
    return n;
}

// orig FUN_0012c880 (work/decompiled/0012c880.c; disasm 0x12c880..0x12cb74). Called by the game task FUN_0011d850
// when no event runs (iRam0051b620 == 0). Creation is FUN_0012ce70(0x50b1a0, tag, 1) (a1 = slot tag, a2 = 1 at
// 0x12c980 for the town call too); only the transform position +0xc..+0x14 is written.
void respawnTick(Dungeon* dungeon, int floorId) {
    if (isScenarioFloor(floorId)) return;              // FUN_0011c2c0(0x50a2a0, iRam0050a2a8) == 1
    if (gRespawnTimer != 0) {
        gRespawnTimer = gRespawnTimer - 1;
        return;
    }
    gRespawnTimer = 900;                               // 0x384
    Chr* pl = characters().player();                   // mgr +0x14 (s3)
    int i = static_cast<int>(gameWorld().rng.rand(9)); // FUN_002071e0(0x53eb20, 9)
    if (pl == nullptr) return;                         // PORT: the original reads the player unconditionally
    int n = 9;
    for (;;) {
        const FloorTagCount& s = gTable[size_t(i)];
        const uint32_t tag = s.tag;
        if ((tag & 0xffu) == 0x45u && s.alive < s.maxAtLoad) {
            if (floorId == 0x5b) {
                Chr* c = characters().createTag(tag, true);
                if (c != nullptr) {
                    if (pl->xf.m.pos[0] < -600.0f) c->xf.m.pos = {3300.0f, 0.0f, -1500.0f};   // 0x454e4000, 0xc4bb8000
                    else c->xf.m.pos = {-3800.0f, 0.0f, -350.0f};                             // 0xc56d8000, 0xc3af0000
                    c->prevPos = c->xf.m.pos;                  // PORT: presentation interpolation
                    return;
                }
            } else {
                int room = -1;                                 // player +0x30 cell: room of a 0x1000 cell, else -1
                if (pl->cell != nullptr) {
                    const uint32_t cell = *pl->cell;
                    room = ((cell & 0xf000u) == 0x1000u) ? static_cast<int>(cell & 0xfu) : -1;
                }
                const uint32_t key = tag & 0xffffffu;
                if (key == kTagE16 || key == kTagE19) room = -1;
                float pos[3] = {0.0f, 0.0f, 0.0f};
                // FUN_00124660 returns 0 at once for room < 0. PORT: no floor object -> treated as that failure
                const int found = dungeon != nullptr ? dungeon->randomCellOutside(pos, room, 0x1000) : 0;
                if (found == 1) {
                    Chr* c = characters().createTag(tag, true);
                    if (c != nullptr) {
                        c->xf.m.pos = {pos[0], pos[1], pos[2]};
                        c->prevPos = c->xf.m.pos;              // PORT: presentation interpolation
                        return;
                    }
                }
            }
        }
        i = i + 1;
        if (9 < i) i = 0;
        const bool more = n != 0;
        n = n - 1;
        if (!more) break;
    }
}

// orig FUN_001d9a20 (work/decompiled/001d9a20.c; disasm 0x1d9a20..0x1d9af4), called by FUN_00121f80 on floor 100:
// every item of the inventory's +0xbc list is freed (class 0xc = GN while flag 0x1e is set) or put on a random floor
// cell (FUN_00124730(&pos, 0x1000), result ignored, pos starts at 0) and placed (FUN_001d5620); the slot is cleared.
void scatterOuterBoxes(Dungeon& dungeon) {
    Inventory& inv = inventory();
    ItemSystem& sys = itemSystem();
    for (uint32_t k = 0; k < 0x14; ++k) {
        Item* it = inv.partB[k];
        if (it == nullptr) continue;
        if (it->cls == 0xc && flags().test(0x1e)) {
            sys.game->freeItem(it);                    // FUN_001b7570
        } else {
            float pos[3] = {0.0f, 0.0f, 0.0f};
            dungeon.randomCellAny(pos, 0x1000);        // FUN_00124730
            it->pos = Vec3{pos[0], pos[1], pos[2]};    // transform +0xc..+0x14
            placeItem(*it, sys);                       // FUN_001d5620
        }
        inv.partB[k] = nullptr;
    }
}

}  // namespace baroque::game
