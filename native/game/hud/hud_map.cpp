// Map2DSource over the generated floor (game::Dungeon, gameWorld().dungeon) and the player character (iRam0050b144):
// the reads FUN_00126120 / FUN_00126cc0 / FUN_00126380 make of the floor context 0x50a350, the room table 0x50a640
// and the player object.
#include "game/hud/hud_api.hpp"
#include "game/character/character.hpp"
#include "game/dungeon.hpp"
#include "game/game_world.hpp"

#include <cstring>

namespace baroque::game {

namespace {

class DungeonMapSource final : public Map2DSource {
public:
    bool hasPlayer() const override { return characters().player() != nullptr; }
    int gridW() const override { const Dungeon* d = gameWorld().dungeon; return d != nullptr ? d->context().maxX : 0; }   // +0x20
    int gridH() const override { const Dungeon* d = gameWorld().dungeon; return d != nullptr ? d->context().maxY : 0; }   // +0x22
    // FUN_00124b40(x, y) -> room, then the room grid cell (x - posX*8) + (y - posY*8) * w8*8
    uint32_t* cell(int x, int y) override {
        Dungeon* d = gameWorld().dungeon;
        if (d == nullptr) return nullptr;
        Room* r = d->roomContaining(x, y);
        if (r == nullptr) return nullptr;
        return d->cellPtr(r, x, y);
    }
    uint32_t* playerCell() override { const Chr* p = characters().player(); return p != nullptr ? p->cell : nullptr; }   // +0x30
    int playerCellX() const override { const Chr* p = characters().player(); return p != nullptr ? p->cellX : 0; }       // +0x34
    int playerCellY() const override { const Chr* p = characters().player(); return p != nullptr ? p->cellY : 0; }       // +0x38
    Vec3 playerForward() const override {                                       // *(player +0x20) +0x30..+0x38
        const Chr* p = characters().player();
        if (p == nullptr) return {};
        return {p->xf.m.fwd[0], p->xf.m.fwd[1], p->xf.m.fwd[2]};
    }
    // FUN_00143d30(*(player +0x2c) +0x10, -1): null, or the 8-byte name compared as a halfword with "WT" (0x44a5d8)
    bool playerIdle() const override {
        const Chr* p = characters().player();
        if (p == nullptr) return true;
        const char* n = p->act.currentName();
        return n == nullptr || (n[0] == 'W' && n[1] == 'T');
    }
    bool roomInfo(int room, char& type, int& wCells, int& hCells, int& posX8, int& posY8, uint32_t*& grid) override {
        Dungeon* d = gameWorld().dungeon;
        if (d == nullptr || room < 0 || room >= d->roomCount()) return false;
        Room* r = d->mutableRoom(room);                         // *(0x50a640 + room*4)
        type = static_cast<char>(r->rdt.roomId);                // **(room +4): RDT byte 0
        wCells = r->rdt.w8 * 8;                                 // RDT byte 1 << 3
        hCells = r->rdt.h8 * 8;                                 // RDT byte 2 << 3
        posX8 = r->posX;                                        // room +0xc
        posY8 = r->posY;                                        // room +0xe
        grid = r->rdt.cells.empty() ? nullptr : r->rdt.cells.data();   // RDT + *(RDT +0xc)
        return true;
    }
    bool targetMarkerShown() const override { return (hudWorld().floorFlags & 0x100000u) != 0; }   // uRam0050a610
    int targetCellX() const override { const Dungeon* d = gameWorld().dungeon; return d != nullptr ? d->context().stairsCellX : 0; }   // +0xcc
    int targetCellY() const override { const Dungeon* d = gameWorld().dungeon; return d != nullptr ? d->context().stairsCellZ : 0; }   // +0xd0
};

DungeonMapSource gSource;

}  // namespace

Map2DSource& dungeonMapSource() { return gSource; }

}  // namespace baroque::game
