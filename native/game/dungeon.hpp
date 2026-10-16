#pragma once
// Baroque dungeon floor system: FDT/RDT loading, random/scenario floor generation, corridor digging,
// object pieces, spawns, items, stairs and player placement. Translated function-by-function from the
// Ghidra pseudocode in work/decompiled (addresses on every function); see work/reports/DUNGEON_TRANSLATION.md.
//
// Original globals mapped to members:
//   0x50a2a8 floor id, 0x50a2ac previous floor id (Game scene object 0x50a2a0 +8/+0xc)
//   0x50a640[16] room objects, 0x50a680 room count, 0x50a620 FDT data, 0x50a610 floor flags
//   0x50a350 floor context (FloorContext), 0x4e5c94 pick_door last axis, 0x53eb20 RNG state (Rng&)
//   0x50ba50[19] MT piece CLS slots, 0x50c050[16] room CLS slots
#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "data/archive.hpp"
#include "data/map_formats.hpp"
#include "game/dungeon_tables.hpp"
#include "game/rng.hpp"

namespace baroque::game {

// ---- floor id classes (Game scene +8), work/decompiled/0011c090.c .. 0011c3b0.c
bool isLowFloor(int id);          // orig FUN_0011c090: id < 0x5a (floors 0-89)
bool isTownFloor(int id);         // orig FUN_0011c110: 0x5b or 0x5c
bool isEventFloor(int id);        // orig FUN_0011c150: 0x5d..0x60
bool isFloor99(int id);           // orig FUN_0011c100
bool isGeneratedFloor(int id);    // orig FUN_0011c0a0: !town && !event && !99
bool isRandomDungeon(int id);     // orig FUN_0011c290: 99 < id < 300 || id == 99
bool isScenarioA(int id);         // orig FUN_0011c330: 300 < id < 0x134
bool isScenarioB(int id);         // orig FUN_0011c350: 0x136 < id < 0x13e
bool isScenarioC(int id);         // orig FUN_0011c370: 0x140 < id < 0x148
bool isScenarioFloor(int id);     // orig FUN_0011c2c0
bool isSecondDungeon(int id);     // orig FUN_0011c390: 399 < id < 500
bool isFinalFloor(int id);        // orig FUN_0011c210: id == 499
bool usesPt14Stairs(int id);      // orig FUN_0011c1d0: id-0x133 in {0,10,20}
bool isNoStairsFloor(int id);     // orig FUN_0011c220: 0x8b, 0xd8, 0xde, 0xe6
int floorNumber(int id);          // orig FUN_0011c3b0

// Game flag manager (0x53c3b0): FUN_00178a80 test / FUN_00178890 set. Defaults: all clear, set ignored.
struct GameFlags {
    std::function<bool(uint32_t)> test;
    std::function<void(uint32_t)> set;
};

// Room object (FUN_00127d70 allocation, pointer kept in 0x50a640[index])
struct Room {
    data::Rdt rdt;            // +4  RDT data (mutable: cells, door flags and flags byte are rewritten at runtime)
    int16_t posX = 0;         // +0xc position in 8-cell blocks
    int16_t posY = 0;         // +0xe
    uint8_t index = 0;        // +0x10
    uint8_t flags = 0;        // +0x11 bit0 placed, bit1 model loaded
    std::string modelPath;    // +8 igb scene: path of the room model (FUN_0011e020)
    std::optional<data::Cls> cls; // 0x50c050[index] collision (FUN_0013fed0)
    bool placed() const { return (flags & 1) != 0; }
    bool disabled() const { return (rdt.flags & 0x80) != 0; }
    // world origin of the room model (FUN_0011e520): (posX*1600+100, 0, posY*1600+100)
    float originX() const { return static_cast<float>(posX) * 8.0f * 200.0f + 100.0f; }
    float originZ() const { return static_cast<float>(posY) * 8.0f * 200.0f + 100.0f; }
};

// Floor context 0x50a350 (only the fields the translated functions touch)
struct FloorContext {
    uint8_t chosenRoomCount = 0;  // +0xc  fdt[1 + rand(3)]
    int32_t stairsRoom = -1;      // +0x10 room index holding the stairs cell (-1 none)
    uint32_t seed = 0;            // +0x14 RNG state snapshot at generation
    Room* lastRoom = nullptr;     // +0x18 last placed room
    int16_t minX = 0, minY = 0;   // +0x1c,+0x1e bounding box min (cells, x8)
    int16_t maxX = 0, maxY = 0;   // +0x20,+0x22 bounding box max (cells)
    int32_t mapW = 0, mapH = 0;   // +0xc4,+0xc8 minimap dims (FUN_00126120, pow2 >= max, 8..0x200)
    int32_t stairsCellX = -1;     // +0xcc stairs world x / 200 (FUN_00126350)
    int32_t stairsCellZ = -1;     // +0xd0
};

// Corridor/door piece instance produced by FUN_00123280 (one per 0x2000 cell)
struct PieceInstance {
    int room;            // owning room index
    int cx, cy;          // local cell
    uint8_t id;          // object id (cell & 0xff)
    uint32_t mt;         // model table 0x48dcd0 mt id
    float rotY;          // rotation about Y (radians)
    float x, y, z;       // world position (cell centre; y = 0 / 300)
    std::string modelPath; // DATA/MAP/M%02d/MT%02d_%02d.IGB (empty if the mt id has no model)
};

// Entity placement record ({tag, position, angle}; the entities themselves are created through EntityHooks).
struct Placement {
    enum class Kind { RdtSpawn, ScenarioEntity, FdtEntity, ExtraSpawn, RandomItem, FixedItem, Stairs, ExtraModel };
    Kind kind;
    uint32_t tag;        // entity tag (4 chars as u32); for ExtraModel the model path is in `note`
    float x, y, z;       // world position
    float angleDeg;      // facing (degrees), as passed to FUN_00128ae0/FUN_00128e90 (converted from radians)
    int room;            // room index (-1 if n/a)
    std::string note;
};

struct PlayerStart {
    bool valid = false;
    float x = 0, y = 0, z = 0;
    float angleRad = 0;
    // reload path of FUN_00120260 ((0x50a610 & 6) == 2): no rotation, the player's current position is shifted by
    // (-x, 0, -z) (x/z = the new bounding-box origin ctx+0x1c/+0x1e * 200) and the camera gets FUN_0013e300
    bool shift = false;
};

// Entity creation FUN_0012ce70(0x50b1a0, tag, 1) and the transform writes of the spawn functions, installed by the
// Game scene so entities are created at the original point of the RNG sequence. Without hooks only placements are
// recorded (checks, test area). `create` returning null = the original's failed creation. The stairs / exit objects
// of FUN_001222d0 (FUN_00179f60, "PT" tags) are created through `create` + `setPos` too; the Game scene's hook
// dispatches "PT" tags to the stage-object list.
struct EntityHooks {
    std::function<void*(uint32_t tag)> create;
    std::function<void(void*, const float* pos)> setPos;        // transform +0xc..+0x14
    std::function<void(void*)> resetAxes;                       // FUN_001286c0 (identity axes)
    std::function<void(void*, float rad)> rotateWorldUp;        // FUN_00128e90(rad, ent, FUN_00206f10())
    std::function<void(void*, float rad)> rotateUp;             // FUN_00128ae0(rad, ent)
    std::function<void(void*)> placeItem;                       // FUN_001d5620(ent) (FUN_00121f80 items)
    // FUN_001d4d70(ent) (FUN_00128c00 + FUN_001d4db0: flip an item and recompute its 0x10 "upside-down" flag)
    std::function<void(void*)> flipItem;
    std::function<uint32_t(void*)> entityId;                    // entity +0x1c
    std::function<uint32_t(void*)> entityFlags;                 // entity +0x70 (item flags)
    std::function<void(void*, uint32_t)> setEntityFlags;
    // FUN_00120260's writes to the player (iRam0050b144): FUN_001286c0, transform +0xc..+0x14, FUN_00128ae0
    // (see PlayerStart); called at the original point of the sequence (before FUN_00120a70(ctx, 1)).
    std::function<void(const PlayerStart&)> placePlayer;
    std::function<void()> snapshotTagCounts;                    // thunk_FUN_00127ff0(0x50b130) (FUN_0011ffc0)
    std::function<void()> scatterOuterBoxes;                    // FUN_001d9a20(0x53d910) (FUN_00121f80, floor 100)
};

struct FloorMeshTriangle { float v[3][3]; }; // floor collision mesh built by FUN_00123280 from MT CLS 'F' faces

class Dungeon {
public:
    Dungeon(data::Archive& archive, Rng& rng);

    void setGameFlags(GameFlags flags) { flags_ = std::move(flags); }
    // Item picker hook for FUN_001d30c0 (needs the PRM item tables, out of scope): receives the category
    // (0..11) and the already-drawn rand(0xff); returns the item tag or 0 for none.
    void setItemPicker(std::function<uint32_t(int, uint32_t)> f) { itemPicker_ = std::move(f); }
    void setEntityHooks(EntityHooks h) { ent_ = std::move(h); }

    // Loading pipeline (synchronous equivalent of FUN_0011f960 -> FUN_0011fa00 -> FUN_00122d40 -> FUN_00122e20).
    bool load(int floorId, int prevFloorId, std::string* err);
    // orig FUN_0011fab0: scenario floor -> FUN_00124250, otherwise FUN_00123dc0(ctx, 0)
    void generateLayout();
    // orig FUN_0011fb30 (collision part): MT piece CLS 0..18 and room CLS; models are only path-resolved.
    bool loadCollision(std::string* err);
    // orig FUN_0011ff50 / FUN_0011ffc0 / FUN_00120060: pieces, spawns pass 0, player, pass 1, FDT entities,
    // random items, stairs, minimap dims, clear occupied bits, flags |= 9.
    void placeContents();

    // Results
    int floorId() const { return floorId_; }
    const data::Fdt& fdt() const { return fdt_; }
    int roomCount() const { return roomCount_; }
    const Room& room(int i) const { return rooms_[static_cast<size_t>(i)]; }
    const FloorContext& context() const { return ctx_; }
    uint32_t floorFlags() const { return floorFlags_; }          // 0x50a610
    const std::vector<PieceInstance>& pieces() const { return pieces_; }
    const std::vector<Placement>& placements() const { return placements_; }
    const PlayerStart& player() const { return player_; }
    const std::vector<FloorMeshTriangle>& floorMesh() const { return floorMesh_; }
    const std::optional<data::Cls>& mtCls(int id) const { return mtCls_[static_cast<size_t>(id)]; }
    std::string texturePath() const;                              // FUN_0011dfe0
    const std::string& roomPrefix() const { return roomPrefix_; }

    // Cell lookup helpers (public for checks)
    uint32_t* cellPtr(Room* r, int x, int y);                     // orig FUN_00124bf0
    Room* roomContaining(int x, int y);                           // orig FUN_00124b40
    uint32_t* cellAtWorld(const float* pos, int* cx, int* cy);    // orig FUN_00124c70
    Room* mutableRoom(int i) { return &rooms_[static_cast<size_t>(i)]; }
    int randomCellAny(float* pos, uint32_t type);                 // orig FUN_00124730 (also FUN_001d9a20)
    int randomCellOutside(float* pos, int excludeRoom, uint32_t type); // orig FUN_00124660 (also FUN_0012c880)

    // public for the NPC C18A random conversions (npc_class.cpp)
    int pickItemCategory();                                        // orig FUN_001d2f30
    uint32_t pickItem(int category);                               // orig FUN_001d30c0 (hooked)
private:
    // path builders 0011dec0 / 0011df20 / 0011dfe0 / 0011e020 / 0011e0f0 / 0011e1c0 / 0011e270
    std::string fdtPath(int floorId) const;
    std::string rdtPath(int mapSet, int roomId) const;
    std::string roomModelPath(int mapSet, int roomId) const;
    std::string roomClsPath(int mapSet, int roomId) const;
    std::string mtModelPath(int mapSet, int mt) const;
    std::string mtClsPath(int mapSet, int id) const;

    bool flagSet(uint32_t id) const { return flags_.test ? flags_.test(id) : false; }
    void setFlag(uint32_t id) { if (flags_.set) flags_.set(id); }

    // loading
    void addRoom(data::Rdt rdt);                                   // orig FUN_00122e20
    void resetGenerationState();                                   // orig FUN_00126300 (context part)

    // generation (FUN_00123dc0 and callees)
    void generateRandom(Room* keepRoom);                           // orig FUN_00123dc0
    void generateScenario();                                       // orig FUN_00124250
    const tables::ScenarioLayout* scenarioLayout() const;          // orig FUN_0011e3d0
    void normaliseBounds();                                        // shared tail of FUN_00123dc0 / FUN_00124250
    int pickRoom();                                                // orig FUN_00124e90
    int nextCandidate(uint32_t i);                                 // orig FUN_00124fe0
    Room* placeFirstOrAttach(Room* a);                             // orig FUN_001255e0
    int attach(Room* a, Room* b);                                  // orig FUN_001256e0
    int pickDoor(data::RdtDoor* doors, uint32_t n);                // orig FUN_00125070
    int nextUnconnectedDoor(data::RdtDoor* doors, uint32_t n, uint32_t d); // orig FUN_00125170
    data::RdtDoor* matchDoor(Room* a, data::RdtDoor* da, Room* b); // orig FUN_001251e0
    int tryPlace(Room* a, const data::RdtDoor* da, Room* b, const data::RdtDoor* db); // orig FUN_00125420
    int dig(Room* a, data::RdtDoor* da, Room* b, data::RdtDoor* db, int test); // orig FUN_001257f0
    void writeEndCap(uint32_t* c, uint32_t lvl, uint32_t dir);     // orig FUN_00125350
    void writeCorridor(uint32_t* c, uint32_t lvl, uint32_t dir, uint32_t prev); // orig FUN_00125380
    void connectAdjacent();                                        // orig FUN_00125c70
    void blockUnconnectedDoors();                                  // orig FUN_00125fc0
    Room* roomById(int roomId);                                    // orig FUN_001245b0

    // contents
    void buildPieces();                                            // orig FUN_00123280
    void spawnPass(int pass);                                      // orig FUN_00120a70
    void spawnEntry(Room* room, const data::RdtSpawn& sp, uint32_t* lastKey); // orig FUN_00120f70
    void placePlayer();                                            // orig FUN_00120260
    void placePlayerStart();                                       // FUN_00120260 position / angle selection
    void fdtEntities();                                            // orig FUN_00121a00
    void fdtEntity(const data::FdtEntity& e);                      // orig FUN_00121c50
    const tables::ExtraSpawn* extraSpawnEntry() const;             // orig FUN_0011e7c0
    void randomItems();                                            // orig FUN_00121f80
    void placeStairs();                                            // orig FUN_001222d0
    void* createStage(uint32_t tag);                               // FUN_00179f60 through EntityHooks::create
    void setStagePos(void* ent, const float* pos);                 // stage transform +0xc..+0x14 (EntityHooks::setPos)
    void setStairsCell(float x, float z);                          // orig FUN_00126350
    void minimapDims();                                            // orig FUN_00126120 (dims only)
    void finalizeCells();                                          // orig FUN_00120060 (occupied-bit clearing)
    int randomCell(Room* room, float* pos, uint32_t type);         // orig FUN_001248a0
    Room* nextSpawnRoom(Room* prev);                               // orig FUN_001247c0
    float randomAngleDeg();                                        // rand(0x8000)/32768*359 idiom
    static bool isItemTag(uint32_t tag);                           // orig FUN_001d38f0
    void record(Placement::Kind kind, uint32_t tag, const float* pos, float angleDeg, int room, std::string note = {});

    data::Archive& archive_;
    Rng& rng_;
    GameFlags flags_;
    std::function<uint32_t(int, uint32_t)> itemPicker_;
    EntityHooks ent_;

    int floorId_ = 0;                 // 0x50a2a8
    int prevFloorId_ = 0;             // 0x50a2ac
    std::string roomPrefix_ = "MR";
    data::Fdt fdt_{};                 // 0x50a620
    bool fdtLoaded_ = false;
    std::array<Room, 16> rooms_{};    // 0x50a640
    int roomCount_ = 0;               // 0x50a680
    uint32_t floorFlags_ = 0;         // 0x50a610 (1 generated, 2 reload models only, 4 re-entry, 8)
    FloorContext ctx_{};              // 0x50a350
    uint8_t lastDoorAxis_ = 0xff;     // DAT_004e5c94 (.data 0xff; reset to 0xff per floor by FUN_0011f960)
    std::array<std::optional<data::Cls>, 19> mtCls_{}; // 0x50ba50 slots
    bool mtClsLoaded_ = false;

    std::vector<PieceInstance> pieces_;
    std::vector<Placement> placements_;
    std::vector<FloorMeshTriangle> floorMesh_;
    PlayerStart player_;
};

} // namespace baroque::game
