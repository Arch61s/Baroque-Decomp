#pragma once
// 2D floor map of the floor context 0x50a350 (ITEMS_HUD_MENUS.md 6.4): texture FUN_00126120, show FUN_00126cc0,
// hide FUN_00126fb0, per-tick fill/scroll FUN_00126380, reset FUN_00126300. Cell data comes from the dungeon
// module through Map2DSource (hud_map.cpp: DungeonMapSource over game::Dungeon).
//
// Context fields: +0xa8 map sprite (UI layer 0x4e68c0, texture bucket, layer flag 0), +0xac player marker and +0xb0
// target marker (polygon layer 0x4e6990, game/render2d.hpp: persistent nodes like the original), +0xb4 texture slot,
// +0xb8 scale, +0xbc/+0xc0 centre, +0xc4/+0xc8 texture size. The port re-emits the map sprite every tick through
// Sprite2D (see ui_sprite2d.hpp) from these fields.
#include "game/hud/sprite2d.hpp"
#include "game/render2d.hpp"
#include <vector>

namespace baroque::game {

// Cell word layout (MAP_FORMATS.md): type bits 0xf000 (0x1000 floor, 0x2000 object), low byte object/piece id,
// bit 0x80000000 seen, bit 0x40000000 partially seen; room cells carry the room index in the low nibble.
struct Map2DSource {
    virtual ~Map2DSource() = default;
    virtual bool hasPlayer() const = 0;                   // iRam0050b144 != 0
    virtual int gridW() const = 0;                        // ctx +0x20 (cells)
    virtual int gridH() const = 0;                        // ctx +0x22
    virtual uint32_t* cell(int x, int y) = 0;             // FUN_00124b40 room + its grid cell (nullptr outside)
    virtual uint32_t* playerCell() = 0;                   // player +0x30 (nullptr when not on a cell)
    virtual int playerCellX() const = 0;                  // player +0x34
    virtual int playerCellY() const = 0;                  // player +0x38
    virtual Vec3 playerForward() const = 0;               // player xform +0x30
    virtual bool playerIdle() const = 0;                  // current action name null or starting "WT" (0x44a5d8)
    // room table 0x50a640[room]: RDT data (+4: byte 0 type char, w8 / h8, grid at +0xc) and placement (+0xc / +0xe
    // in 8-cell units). wCells / hCells are the grid size in cells (w8 * 8, h8 * 8), the grid row stride.
    virtual bool roomInfo(int room, char& type, int& wCells, int& hCells, int& posX8, int& posY8, uint32_t*& grid) = 0;
    virtual bool targetMarkerShown() const = 0;           // uRam0050a610 & 0x100000
    virtual int targetCellX() const = 0;                  // ctx +0xcc
    virtual int targetCellY() const = 0;                  // ctx +0xd0
};

class Map2D {
public:
    void createTexture(Map2DSource& src, Sprite2D& out);   // orig FUN_00126120 (pow2 >= grid, cleared)
    bool show(Map2DSource& src, int floorId);               // orig FUN_00126cc0 (true = shown now; caller sets flag 99)
    void hide();                                            // orig FUN_00126fb0
    void reset(Sprite2D& out);                              // orig FUN_00126300 (map part; +0xcc/+0xd0 are the dungeon's)
    void tick(Map2DSource& src, Sprite2D& out);             // orig FUN_00126380
    void draw(Sprite2D& out) const;                         // the +0xa8 sprite (markers: polygon layer)
    const Polygon2D* marker() const { return marker_; }
    const Polygon2D* targetMarker() const { return targetPoly_; }
    bool shown() const { return shown_; }                   // ctx +0xa8 != 0
    bool hasTexture() const { return texture_ != -1; }
    int texW() const { return texW_; }
    int texH() const { return texH_; }
    const uint32_t* pixels() const { return pixels_.data(); }
    int centreX() const { return cx_; }
    int centreY() const { return cy_; }
    uint32_t alpha() const { return alpha_; }
    // PORT: mini map in the bottom-left corner of the HUD raster, 2 px per cell, a window around the
    // player, anchored to the left screen edge in widescreen, no fade while moving. Off = the original centred map.
    void setCorner(bool on) { corner_ = on; }
    bool corner() const { return corner_; }
    int idleCounter = 0;            // iRam0050a690
    int targetSpin = 0;             // iRam0050a6d0
private:
    void cornerTick(Map2DSource& src);   // PORT: corner window + markers
    bool corner_ = false;           // PORT
    int winX_ = 0, winY_ = 0, winW_ = 0, winH_ = 0;   // PORT: corner window (cells)
    bool shown_ = false;            // +0xa8 map sprite
    int texture_ = -1;              // +0xb4
    float scale_ = 2.0f;            // +0xb8 (half-size per cell -> 4 px per cell)
    int cx_ = 0x100, cy_ = 0xe0;    // +0xbc / +0xc0
    int texW_ = 0, texH_ = 0;       // +0xc4 / +0xc8
    int gridW_ = 0, gridH_ = 0;     // ctx +0x20 / +0x22 as read by FUN_00126cc0 (sprite size / UVs)
    uint32_t alpha_ = 0x40;         // map sprite +0x28 >> 24
    Polygon2D* marker_ = nullptr;   // +0xac player triangle (verts 0x44a5e0 -> buffer 0x50a6e0)
    Polygon2D* targetPoly_ = nullptr;   // +0xb0 target quad (verts 0x44a570 -> buffer 0x50a6a0)
    PolyVert markerVerts_[4];       // 0x50a6e0
    PolyVert targetVerts_[4];       // 0x50a6a0
    std::vector<uint32_t> pixels_;  // FUN_00106630 buffer of the texture slot
};

}  // namespace baroque::game
