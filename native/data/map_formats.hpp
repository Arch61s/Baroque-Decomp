#pragma once
// Baroque dungeon map formats: FDT (floor), RDT (room), CLS (collision mesh).
// Layouts recovered in work/reports/MAP_FORMATS.md sections 2-4; parsers assert exactly like tools/map_formats.py.
// All integers little-endian, floats IEEE single. Parsers return std::nullopt and fill *err on any violation.
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace baroque::data {

// 4-byte tag as the game compares it: little-endian u32 of the ASCII bytes (e.g. "PLAY").
constexpr uint32_t makeTag(char a, char b, char c, char d) {
    return static_cast<uint32_t>(static_cast<uint8_t>(a)) | (static_cast<uint32_t>(static_cast<uint8_t>(b)) << 8) |
           (static_cast<uint32_t>(static_cast<uint8_t>(c)) << 16) | (static_cast<uint32_t>(static_cast<uint8_t>(d)) << 24);
}
constexpr uint32_t makeTag(const char (&s)[5]) { return makeTag(s[0], s[1], s[2], s[3]); }
std::string tagToString(uint32_t tag); // printable 4 chars, trailing NULs dropped

// ------------------------------------------------------------------------------------------------ FDT
struct FdtEntity {       // 8 bytes, at entityTableOffset
    uint32_t tag;        // +0 char[4]
    uint32_t count;      // +4
};
struct Fdt {
    uint8_t mapSet;                   // +0x00 M%02d directory / texture set
    uint8_t roomCountChoice[4];       // +0x01 FUN_00123dc0 picks choice[rand(3)]
    uint8_t roomCount;                // +0x05 entries in room-id table (1..16)
    uint8_t entityTableCount;         // +0x06
    uint8_t randomItemCount;          // +0x07 FUN_00121f80
    uint8_t itemCategoryWeight[12];   // +0x08..0x13 FUN_001d2f30: weights of the 12 item categories (SD,CT,WG,HT,MT,BX,LQ,ST,TL,TP,BN,WM)
    uint32_t roomTableOffset;         // +0x14 (always 0x1c)
    uint32_t entityTableOffset;       // +0x18 (always align4(0x1c + roomCount))
    std::vector<uint8_t> roomIds;     // +0x1c u8[roomCount]
    std::vector<FdtEntity> entities;  // at entityTableOffset
    size_t parsedEnd = 0;             // = entityTableOffset + 8*entityTableCount
};
std::optional<Fdt> parseFdt(const std::vector<uint8_t>& b, std::string* err);

// ------------------------------------------------------------------------------------------------ RDT
// Cell word bits (MAP_FORMATS.md section 3):
constexpr uint32_t kCellTypeMask = 0xf000;    // 0 void, 0x1000 floor, 0x2000 object (runtime only)
constexpr uint32_t kCellFloor = 0x1000;
constexpr uint32_t kCellObject = 0x2000;
constexpr uint32_t kCellLevelMask = 0xf00;    // 0 ground (y=0) / 0x100 upper (y=300)
constexpr uint32_t kCellRoomMask = 0xf;       // room index written at load (FUN_00122e20)
constexpr uint32_t kCellOccupied = 0x100000;  // runtime: random placement
constexpr uint32_t kCellStairs = 0x200000;    // runtime: FUN_001222d0
constexpr uint32_t kCellNoSpawn = 0x1000000;  // file: FUN_001248a0 rejects cell & 0x1100000
constexpr uint32_t kCellCorridor = 0x2000000; // set with 0x2000 by FUN_00125350 (also present in files)

struct RdtDoor {        // 4 bytes
    uint8_t dir;        // +0: 0=N(-y) 1=E(+x) 2=S(+y) 3=W(-x)
    uint8_t x, y;       // +1,+2: floor cell inside the room next to the opening
    uint8_t flags;      // +3: bits0-3 level, bit4 'connected' (runtime)
};
struct RdtSpawn {       // 0x18 bytes
    uint32_t tag;       // +0
    float x, y, z;      // +4,+8,+0xc room-relative (negative x or z = random floor cell)
    float angleDeg;     // +0x10 (<0 random)
    int32_t cond;       // +0x14 >0 flag must be set, <0 see FUN_00120f70
};
struct RdtEmitter {     // 0x18 bytes, FUN_0011edf0 -> FUN_00112190
    uint32_t effectId;  // +0
    float x, y, z;      // +4..
    float rotYDeg;      // +0x10
    float scale;        // +0x14
};
struct RdtLight {       // 0x20 bytes, FUN_0011f160 (only x,y,z use verified)
    uint32_t type;      // +0
    float x, y, z;      // +4..
    float r, g, b;      // +0x10..
    float radius;       // +0x1c
};
struct Rdt {
    uint8_t roomId;          // +0 (== file index)
    uint8_t w8, h8;          // +1,+2 size in 8-cell blocks
    uint8_t doorCount;       // +3
    uint8_t spawnCount;      // +4
    uint8_t emitterCount;    // +5
    uint8_t lightCount;      // +6
    uint8_t flags;           // +7 bit0 start-room candidate, bit1 needs flagId set, bit2 needs flagId clear; runtime bit7 disabled
    uint32_t flagId;         // +8
    uint32_t gridOffset;     // +0xc (always 0x20)
    uint32_t doorOffset;     // +0x10 (0 if none)
    uint32_t spawnOffset;    // +0x14
    uint32_t emitterOffset;  // +0x18
    uint32_t lightOffset;    // +0x1c
    std::vector<uint32_t> cells;      // +0x20 u32[h][w], index y*w + x
    std::vector<RdtDoor> doors;
    std::vector<RdtSpawn> spawns;
    std::vector<RdtEmitter> emitters;
    std::vector<RdtLight> lights;
    size_t parsedEnd = 0;
    int w() const { return w8 * 8; }
    int h() const { return h8 * 8; }
};
std::optional<Rdt> parseRdt(const std::vector<uint8_t>& b, std::string* err);

// ------------------------------------------------------------------------------------------------ CLS
struct ClsVec3 { float x, y, z; };
struct ClsFaceEdge {
    uint32_t vecA;      // +0x1c/+0x28/+0x34 index into vectors (edge dir / edge normal, roles UNVERIFIED)
    uint32_t vecB;      // +0x20/+0x2c/+0x38 index into vectors
    int32_t neighbor;   // +0x24/+0x30/+0x3c face index or -1
};
struct ClsFace {        // 0x44 bytes (pointers relocated to indices)
    char type;          // +0 'W' wall / 'F' floor / 'L'
    char attr[8];       // +1 char[7] ("F","F0S","WD","WE","I","D","00S" or empty), NUL-terminated here
    uint32_t normal;    // +0xc index into vectors
    uint32_t verts[3];  // +0x10,+0x14,+0x18 indices into vertices
    ClsFaceEdge edges[3];
};
struct ClsCell {        // {u32 faceListOffset; u32 faceCount} -> indices into faceRefs
    uint32_t firstRef;  // index into Cls::faceRefs (0 when empty)
    uint32_t count;
};
struct Cls {
    uint32_t vertexCount, vectorCount, faceCount, cellCount;
    ClsVec3 bboxMin, bboxMax;             // +0x20, +0x2c
    float cellSize[3];                    // +0x40 (x,0,z)
    uint32_t nx, ny, nz;                  // +0x4c,+0x50,+0x54 (ny always 1)
    int32_t originX, originY, originZ;    // +0x58.. (= -floor(bboxMin/cellSize))
    std::vector<ClsVec3> vertices;        // at 0x64
    std::vector<ClsVec3> vectors;
    std::vector<ClsFace> faces;
    std::vector<ClsCell> cells;           // nx*ny*nz, index iz*nx + ix (UNVERIFIED indexing)
    std::vector<uint32_t> faceRefs;       // the cells' face lists in cell order
    size_t parsedEnd = 0;
};
std::optional<Cls> parseCls(const std::vector<uint8_t>& b, std::string* err);

// orig FUN_001400b0 (work/decompiled/001400b0.c): floor face walkable test.
bool clsFaceWalkable(const ClsFace& f);

} // namespace baroque::data
