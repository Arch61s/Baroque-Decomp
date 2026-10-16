#include "game/dungeon.hpp"

#include <cstdio>
#include <cstring>

#include "platform/platform.hpp"

namespace baroque::game {

using namespace tables;
using data::kCellCorridor;
using data::kCellFloor;
using data::kCellLevelMask;
using data::kCellObject;
using data::kCellOccupied;
using data::kCellRoomMask;
using data::kCellStairs;
using data::kCellTypeMask;
using data::makeTag;

namespace {
constexpr uint32_t kTagPLAY = makeTag("PLAY");  // 0x44a3e0
constexpr uint32_t kTagMT00 = makeTag("MT00");  // 0x44a3e8
constexpr uint32_t kTagO99Z = makeTag("O99Z");  // 0x44a3f0
constexpr uint32_t kTagJ024 = makeTag("J024");  // 0x44a3f8 -> DATA/MAP/M54/ME54_00.IGB (0x44a400)
constexpr uint32_t kTagJ025 = makeTag("J025");  // 0x44a420 -> DATA/MAP/M99/MR99_01.IGB (0x44a430)
constexpr uint32_t kTagJ026 = makeTag("J026");  // 0x44a450 -> DATA/MAP/M53/ME53_01.IGB (0x44a460)
constexpr uint32_t kTagC05 = makeTag('C', '0', '5', 0);  // 0x44a480 (3-char compare)
constexpr uint32_t kTagC07A = makeTag("C07A"); // 0x44a4e0
constexpr uint32_t kTagTP00 = makeTag("TP00"); // 0x44a4e8
constexpr uint32_t kTagE16 = makeTag('E', '1', '6', 0);  // 0x44a4f0
constexpr uint32_t kTagE19 = makeTag('E', '1', '9', 0);  // 0x44a4f8
constexpr uint32_t kTagPT00 = makeTag("PT00"); // 0x44a548
constexpr uint32_t kTagPT14 = makeTag("PT14"); // 0x44a550
constexpr float kPi = 3.1415927f;               // 0x40490fdb as used by the callers
} // namespace

// ------------------------------------------------------------------------------------------ floor id classes
bool isLowFloor(int id) { return static_cast<uint32_t>(id) < 0x5a; }                                   // orig FUN_0011c090 (work/decompiled/0011c090.c)
bool isTownFloor(int id) { return id == 0x5b || id == 0x5c; }                                          // orig FUN_0011c110 (work/decompiled/0011c110.c)
bool isEventFloor(int id) { return id == 0x5d || id == 0x5e || id == 0x5f || id == 0x60; }             // orig FUN_0011c150 (work/decompiled/0011c150.c)
bool isFloor99(int id) { return id == 99; }                                                             // orig FUN_0011c100 (work/decompiled/0011c100.c)
bool isGeneratedFloor(int id) { return !isTownFloor(id) && !isEventFloor(id) && !isFloor99(id); }      // orig FUN_0011c0a0 (work/decompiled/0011c0a0.c)
bool isRandomDungeon(int id) { const auto u = static_cast<uint32_t>(id); return (99 < u && u < 300) || u == 99; } // orig FUN_0011c290 (work/decompiled/0011c290.c)
bool isScenarioA(int id) { const auto u = static_cast<uint32_t>(id); return 300 < u && u < 0x134; }   // orig FUN_0011c330 (work/decompiled/0011c330.c)
bool isScenarioB(int id) { const auto u = static_cast<uint32_t>(id); return 0x136 < u && u < 0x13e; } // orig FUN_0011c350 (work/decompiled/0011c350.c)
bool isScenarioC(int id) { const auto u = static_cast<uint32_t>(id); return 0x140 < u && u < 0x148; } // orig FUN_0011c370 (work/decompiled/0011c370.c)
bool isScenarioFloor(int id) { return isScenarioA(id) || isScenarioB(id) || isScenarioC(id); }         // orig FUN_0011c2c0 (work/decompiled/0011c2c0.c)
bool isSecondDungeon(int id) { const auto u = static_cast<uint32_t>(id); return 399 < u && u < 500; } // orig FUN_0011c390 (work/decompiled/0011c390.c)
bool isFinalFloor(int id) { return id == 499; }                                                         // orig FUN_0011c210 (work/decompiled/0011c210.c)
bool usesPt14Stairs(int id) {                                                                           // orig FUN_0011c1d0 (work/decompiled/0011c1d0.c)
    const uint32_t u = static_cast<uint32_t>(id - 0x133);
    if (u < 0x15) return ((1u << (u & 0x1f)) & 0x100401u) != 0;  // ids 307, 317, 327
    return false;
}
bool isNoStairsFloor(int id) { return id == 0x8b || id == 0xd8 || id == 0xde || id == 0xe6; }           // orig FUN_0011c220 (work/decompiled/0011c220.c)
int floorNumber(int id) {                                                                               // orig FUN_0011c3b0 (work/decompiled/0011c3b0.c)
    if (isRandomDungeon(id)) return id % 100;
    if (isScenarioA(id)) return id - 300;
    if (isScenarioB(id)) return id - 0x136;
    if (isScenarioC(id)) return id - 0x140;
    if (isSecondDungeon(id)) return id - 399;
    return 0;
}

// ------------------------------------------------------------------------------------------ path builders
Dungeon::Dungeon(data::Archive& archive, Rng& rng) : archive_(archive), rng_(rng) {}

static std::string fmt(const char* f, int a, int b = 0, int c = 0) {
    char buf[64];
    std::snprintf(buf, sizeof buf, f, a, b, c);
    return buf;
}
std::string Dungeon::fdtPath(int floorId) const { return fmt("DATA/MAP/FLOOR/F%03d.FDT", floorId); }  // orig FUN_0011dec0 (0x449fd0)
std::string Dungeon::rdtPath(int mapSet, int roomId) const {                                            // orig FUN_0011df20 (0x44a000/20/40)
    if (isTownFloor(floorId_)) return fmt("DATA/MAP/ROOM/MO%02d_%02d.RDT", mapSet, roomId);
    if (isEventFloor(floorId_)) return fmt("DATA/MAP/ROOM/ME%02d_%02d.RDT", mapSet, roomId);
    return fmt("DATA/MAP/ROOM/MR%02d_%02d.RDT", mapSet, roomId);
}
std::string Dungeon::texturePath() const { return fmt("DATA/MAP/M%02d/M%02d_TEX.IGB", fdt_.mapSet, fdt_.mapSet); } // orig FUN_0011dfe0 (0x44a060)
std::string Dungeon::roomModelPath(int mapSet, int roomId) const {                                      // orig FUN_0011e020 (0x44a080/a0/c0)
    if (isTownFloor(floorId_)) return fmt("DATA/MAP/M%02d/MO%02d_%02d.IGB", mapSet, mapSet, roomId);
    if (isEventFloor(floorId_)) return fmt("DATA/MAP/M%02d/ME%02d_%02d.IGB", mapSet, mapSet, roomId);
    return fmt("DATA/MAP/M%02d/MR%02d_%02d.IGB", mapSet, mapSet, roomId);
}
std::string Dungeon::roomClsPath(int mapSet, int roomId) const {                                        // orig FUN_0011e0f0 (0x44a0e0/100/120)
    if (isTownFloor(floorId_)) return fmt("DATA/MAP/M%02d/MO%02d_%02d.CLS", mapSet, mapSet, roomId);
    if (isEventFloor(floorId_)) return fmt("DATA/MAP/M%02d/ME%02d_%02d.CLS", mapSet, mapSet, roomId);
    return fmt("DATA/MAP/M%02d/MR%02d_%02d.CLS", mapSet, mapSet, roomId);
}
std::string Dungeon::mtModelPath(int mapSet, int mt) const {                                            // orig FUN_0011e1c0 (0x44a140 ids, 0x44a160)
    for (int i = 0; i < 6; ++i)
        if (mt == kMtIdsWithModel[i]) return fmt("DATA/MAP/M%02d/MT%02d_%02d.IGB", mapSet, mapSet, mt);
    return {};
}
std::string Dungeon::mtClsPath(int mapSet, int id) const { return fmt("DATA/MAP/M%02d/MT%02d_%02d.CLS", mapSet, mapSet, id); } // orig FUN_0011e270 (0x44a180)

// ------------------------------------------------------------------------------------------ loading
// Synchronous equivalent of FUN_0011f960 (reset) -> FUN_0011fa00 (pack name, FDT) -> FUN_00122d40 (RDTs) ->
// FUN_00122e20 (room objects). The original issues the RDT loads in FDT order and the callbacks create the room
// objects in completion order; UNVERIFIED: completion order assumed equal to issue order (single pending load).
bool Dungeon::load(int floorId, int prevFloorId, std::string* err) {
    floorId_ = floorId;
    prevFloorId_ = prevFloorId;
    floorFlags_ = 0;                       // FUN_0011f960: uRam0050a610 = 0
    lastDoorAxis_ = 0xff;                  // FUN_0011f960: sb 0xff, -0x7edc($gp) at 0x11f9dc (DAT_004e5c94)
    roomCount_ = 0;
    fdtLoaded_ = false;
    for (auto& r : rooms_) r = Room{};
    ctx_ = FloorContext{};
    pieces_.clear(); placements_.clear(); floorMesh_.clear(); player_ = PlayerStart{};
    for (auto& c : mtCls_) c.reset();
    mtClsLoaded_ = false;
    roomPrefix_ = isTownFloor(floorId) ? "MO" : isEventFloor(floorId) ? "ME" : "MR";

    archive_.setPack(data::Archive::packNameForFloor(floorId));   // FUN_0011df00 -> 0x50a330
    const auto fdtBytes = archive_.load(fdtPath(floorId));
    if (!fdtBytes) { if (err) *err = "missing " + fdtPath(floorId); return false; }
    auto fdt = data::parseFdt(*fdtBytes, err);
    if (!fdt) return false;
    fdt_ = std::move(*fdt);
    fdtLoaded_ = true;
    // orig FUN_00122d40: for i < fdt[5]: load {prefix}%02d_%02d.RDT(fdt[0], roomIds[i]) with callback FUN_00122e20
    for (uint32_t i = 0; i < fdt_.roomCount; ++i) {
        const std::string p = rdtPath(fdt_.mapSet, fdt_.roomIds[i]);
        const auto bytes = archive_.load(p);
        if (!bytes) { if (err) *err = "missing " + p; return false; }
        auto rdt = data::parseRdt(*bytes, err);
        if (!rdt) { if (err) *err = p + ": " + *err; return false; }
        addRoom(std::move(*rdt));
    }
    return true;
}

// orig FUN_00122e20 (work/decompiled/00122e20.c)
void Dungeon::addRoom(data::Rdt rdt) {
    Room& room = rooms_[static_cast<size_t>(roomCount_)];   // FUN_00127d70 new room object -> 0x50a640[count]
    room.rdt = std::move(rdt);
    room.posX = 0; room.posY = 0;
    room.flags = 0;
    room.index = static_cast<uint8_t>(roomCount_);
    room.modelPath = roomModelPath(fdt_.mapSet, room.rdt.roomId);
    for (uint32_t& c : room.rdt.cells)                       // tag every non-void cell with the room index
        if ((c & kCellTypeMask) != 0) { c &= 0xfffffff0u; c |= static_cast<uint32_t>(roomCount_); }
    roomCount_++;
    data::Rdt& d = room.rdt;
    if (!isLowFloor(floorId_)) {
        if ((d.flags & 2) == 0) {
            if ((d.flags & 4) != 0 && flagSet(d.flagId)) d.flags |= 0x80;   // enabled only if flag clear
        } else if (!flagSet(d.flagId)) {
            d.flags |= 0x80;                                                   // enabled only if flag set
        }
    }
    if (floorId_ == 0x89) {
        if (d.roomId != 0x15) return;
        if (!flagSet(0x1e)) { d.flags |= 0x80; return; }
        if (flagSet(0x3be) && !flagSet(0x3bf)) { d.flags |= 1; return; }
        d.flags |= 0x80;
        return;
    }
    if (floorId_ != 0x7f) {
        if (floorId_ != 0x67) return;
        if (!flagSet(0xd2)) {
            if (flagSet(0xd1)) {
                if (d.roomId == '!') { d.flags |= 0x80; return; }
                if (d.roomId != 0x1e) { if (d.roomId != ' ') return; d.flags |= 1; return; }
                d.flags |= 0x80; return;
            }
            if (d.roomId == '!') { d.flags |= 0x80; return; }
            if (d.roomId != ' ') { if (d.roomId != 0x1e) return; d.flags |= 1; return; }
            d.flags |= 0x80; return;
        }
        if (d.roomId == ' ') { d.flags |= 0x80; return; }
        if (d.roomId != 0x1e) { if (d.roomId != '!') return; d.flags |= 1; return; }
        d.flags |= 0x80;
        return;
    }
    if (d.roomId != 0x15) return;
    if (!flagSet(0x1e)) { d.flags |= 0x80; return; }
    if (flagSet(0x3bc) && !flagSet(0x3bd)) { d.flags |= 1; return; }
    if (flagSet(0x3c1) && !flagSet(0x3c2)) { d.flags |= 1; return; }
    d.flags |= 0x80;
}

// orig FUN_00126300 (work/decompiled/00126300.c): frees the minimap image (FUN_00126fb0/FUN_00105f00, out of
// scope) and clears ctx+0xb4 (image handle), ctx+0xcc, ctx+0xd0.
void Dungeon::resetGenerationState() {
    ctx_.stairsCellX = 0;
    ctx_.stairsCellZ = 0;
}

// orig FUN_0011fab0 (work/decompiled/0011fab0.c)
void Dungeon::generateLayout() {
    if (isScenarioFloor(floorId_)) generateScenario();
    else generateRandom(nullptr);
}

// orig FUN_0011fb30 (work/decompiled/0011fb30.c), collision part. The texture/model loads are path-only here.
bool Dungeon::loadCollision(std::string* err) {
    if ((floorFlags_ & 2) != 0) return true;   // reload-models-only path -> FUN_0011fe00 directly
    bool loadPieces = true;
    if (ctx_.chosenRoomCount < 2) {
        if (!isScenarioFloor(floorId_) && !isSecondDungeon(floorId_) && floorId_ != 0x8c) loadPieces = false; // FUN_0013fdd0(0,...) reset
    }
    if (loadPieces) {
        for (int i = 0; i < 0x13; ++i) {   // MT IGB (ids with models) + MT CLS 0..18
            const std::string p = mtClsPath(fdt_.mapSet, i);
            const auto bytes = archive_.load(p);
            if (!bytes) { if (err) *err = "missing " + p; return false; }
            auto cls = data::parseCls(*bytes, err);
            if (!cls) { if (err) *err = p + ": " + *err; return false; }
            mtCls_[static_cast<size_t>(i)] = std::move(*cls);
        }
        mtClsLoaded_ = true;
    }
    for (int i = 0; i < fdt_.roomCount; ++i) {   // every room's CLS (placed or not), slot = room index
        Room& r = rooms_[static_cast<size_t>(i)];
        const std::string p = roomClsPath(fdt_.mapSet, r.rdt.roomId);
        const auto bytes = archive_.load(p);
        if (!bytes) { if (err) *err = "missing " + p; return false; }
        auto cls = data::parseCls(*bytes, err);
        if (!cls) { if (err) *err = p + ": " + *err; return false; }
        r.cls = std::move(*cls);
    }
    return true;
}

// ------------------------------------------------------------------------------------------ random floors
// orig FUN_00123dc0 (work/decompiled/00123dc0.c). keepRoom != 0 goes through FUN_00124110 (unreachable: the only
// caller passes 0); it is not translated.
void Dungeon::generateRandom(Room* keepRoom) {
    ctx_.seed = rng_.state;                     // ctx+0x14 = uRam0053eb20
    resetGenerationState();                     // FUN_00126300
    ctx_.stairsRoom = -1;                       // ctx+0x10
    ctx_.stairsCellX = -1;                      // ctx+0xcc
    ctx_.stairsCellZ = -1;                      // ctx+0xd0
    if (keepRoom == nullptr) {
        ctx_.lastRoom = placeFirstOrAttach(nullptr);   // FUN_001255e0(0)
    } else {
        // UNVERIFIED: FUN_00124110(ctx, keepRoom) path never executes in the shipped game.
        platform::log("dungeon: keepRoom path (FUN_00124110) not translated");
        ctx_.lastRoom = keepRoom;
    }
    const uint32_t pick = rng_.rand(3);         // disasm 0x123e28: addiu a1,zero,3
    ctx_.chosenRoomCount = fdt_.roomCountChoice[pick];   // *(fdt + 1 + rand)
    int8_t remaining = static_cast<int8_t>(ctx_.chosenRoomCount);
    while (static_cast<int8_t>(--remaining) != 0) {
        Room* r = placeFirstOrAttach(ctx_.lastRoom);     // FUN_001255e0(last)
        if (r == nullptr) {
            for (uint32_t i = 0; i < static_cast<uint32_t>(roomCount_); ++i) {
                Room* o = &rooms_[i];
                if (o->placed() && (r = placeFirstOrAttach(o)) != nullptr) break;   // disasm 0x123e88: a0 = rooms[i]
            }
            if (r == nullptr) break;
        }
        ctx_.lastRoom = r;
    }
    connectAdjacent();          // FUN_00125c70
    blockUnconnectedDoors();    // FUN_00125fc0
    normaliseBounds();
}

// Shared tail of FUN_00123dc0 / FUN_00124250: shift placed rooms so min == 0, record bounds (cells = blocks * 8).
void Dungeon::normaliseBounds() {
    ctx_.minX = 0x7fff; ctx_.minY = 0x7fff;
    for (int i = 0; i < roomCount_; ++i) {
        const Room& r = rooms_[static_cast<size_t>(i)];
        if (!r.placed()) continue;
        if (r.posX < ctx_.minX) ctx_.minX = r.posX;
        if (r.posY < ctx_.minY) ctx_.minY = r.posY;
    }
    for (int i = 0; i < roomCount_; ++i) {
        Room& r = rooms_[static_cast<size_t>(i)];
        if (!r.placed()) continue;
        r.posX = static_cast<int16_t>(r.posX - ctx_.minX);
        r.posY = static_cast<int16_t>(r.posY - ctx_.minY);
    }
    ctx_.minX = static_cast<int16_t>(ctx_.minX << 3);
    ctx_.minY = static_cast<int16_t>(ctx_.minY << 3);
    ctx_.maxX = 0; ctx_.maxY = 0;
    for (int i = 0; i < roomCount_; ++i) {
        const Room& r = rooms_[static_cast<size_t>(i)];
        if (!r.placed()) continue;
        const int16_t ex = static_cast<int16_t>(r.posX + r.rdt.w8);
        if (ctx_.maxX < ex) ctx_.maxX = ex;
        const int16_t ey = static_cast<int16_t>(r.posY + r.rdt.h8);
        if (ctx_.maxY < ey) ctx_.maxY = ey;
    }
    ctx_.maxX = static_cast<int16_t>(ctx_.maxX << 3);
    ctx_.maxY = static_cast<int16_t>(ctx_.maxY << 3);
}

// orig FUN_00124e90 (work/decompiled/00124e90.c): returns a room index or -1
int Dungeon::pickRoom() {
    if (!isLowFloor(floorId_)) {
        for (uint32_t i = 0; i < static_cast<uint32_t>(roomCount_); ++i) {
            const Room& r = rooms_[i];
            if (!r.placed() && (r.rdt.flags & 1) != 0 && (r.rdt.flags & 0x80) == 0) return static_cast<int>(i);
        }
    }
    int r = static_cast<int>(rng_.rand(0x10));
    for (;;) {
        uint32_t idx = 0;
        uint32_t remaining = static_cast<uint32_t>(roomCount_);
        for (;;) {
            const Room& room = rooms_[idx];
            if (!room.placed() && (room.rdt.flags & 0x80) == 0) {
                if (r == 0) return static_cast<int>(idx);
                r--;
            } else {
                remaining--;
                if (remaining == 0) return -1;
            }
            idx++;
            if (static_cast<uint32_t>(roomCount_) <= idx) break;
        }
    }
}

// orig FUN_00124fe0 (work/decompiled/00124fe0.c): next unplaced, enabled room index after i (cyclic) or -1
int Dungeon::nextCandidate(uint32_t i) {
    uint32_t n = static_cast<uint32_t>(roomCount_);
    do {
        if (n == 0) return -1;
        n--;
        i++;
        if (static_cast<uint32_t>(roomCount_) <= i) i = 0;
    } while (rooms_[i].placed() || (rooms_[i].rdt.flags & 0x80) != 0);
    return static_cast<int>(i);
}

// orig FUN_001255e0 (work/decompiled/001255e0.c): a == 0 -> place first room at (0,0); else attach a room to a.
Room* Dungeon::placeFirstOrAttach(Room* a) {
    int i = pickRoom();
    if (i == -1) return nullptr;
    if (a == nullptr) {
        Room* r = &rooms_[static_cast<size_t>(i)];
        r->posX = 0; r->posY = 0;
        r->flags |= 1;
        return r;
    }
    int n = roomCount_;
    do {
        if (n == 0) return nullptr;
        n--;
        Room* b = &rooms_[static_cast<size_t>(i)];
        if (a != b && attach(a, b) == 1) return b;
        i = nextCandidate(static_cast<uint32_t>(i));
    } while (i != -1);
    return nullptr;
}

// orig FUN_001256e0 (work/decompiled/001256e0.c)
int Dungeon::attach(Room* a, Room* b) {
    data::RdtDoor* doors = a->rdt.doors.empty() ? nullptr : a->rdt.doors.data();
    const uint8_t n = a->rdt.doorCount;
    int d = pickDoor(doors, n);
    uint32_t remaining = n;
    if (n != 0) {
        do {
            remaining--;
            if (d == -1) return 0;
            data::RdtDoor* da = &doors[d];
            data::RdtDoor* db = matchDoor(a, da, b);
            if (db != nullptr) {
                dig(a, da, b, db, 0);
                return 1;
            }
            d = nextUnconnectedDoor(doors, n, static_cast<uint32_t>(d));
        } while (remaining != 0);
    }
    return 0;
}

// orig FUN_00125070 (work/decompiled/00125070.c). The r draw is rand(n) with n = door count: the disassembly
// (0x125070..0x1250a4) never writes a1 before the FUN_002071e0 call, so a1 is still param_2 (the report's
// "rand(255)" was a misread of the 0xff axis-wildcard constant).
int Dungeon::pickDoor(data::RdtDoor* doors, uint32_t n) {
    uint8_t axis = lastDoorAxis_;          // DAT_004e5c94
    if (n == 0) return -1;
    int r = static_cast<int>(rng_.rand(n));
    for (;;) {
        uint32_t i = 0;
        uint32_t remaining = n;
        do {
            const data::RdtDoor& d = doors[i];
            const uint8_t dirAxis = d.dir & 1;
            if ((d.flags & 0x10) == 0 && axis != dirAxis) {
                if (r == 0) { lastDoorAxis_ = dirAxis; return static_cast<int>(i); }
                r--;
            } else {
                remaining--;
                if (remaining == 0) {
                    if (axis == 0xff) return -1;
                    axis = 0xff;
                    remaining = n;
                }
            }
            i++;
        } while (i < n);
    }
}

// orig FUN_00125170 (work/decompiled/00125170.c)
int Dungeon::nextUnconnectedDoor(data::RdtDoor* doors, uint32_t n, uint32_t d) {
    uint32_t k = n;
    do {
        if (k == 0) return -1;
        k--;
        d++;
        if (n <= d) d = 0;
    } while ((doors[d].flags & 0x10) != 0);
    return static_cast<int>(d);
}

// orig FUN_001251e0 (work/decompiled/001251e0.c): first door of b facing da (dir ^ 2, same level) that fits.
data::RdtDoor* Dungeon::matchDoor(Room* a, data::RdtDoor* da, Room* b) {
    const uint8_t dir = da->dir;
    data::RdtDoor* db = b->rdt.doors.empty() ? nullptr : b->rdt.doors.data();
    uint32_t n = b->rdt.doorCount;
    if (n != 0) {
        do {
            n--;
            if (db->dir == (dir ^ 2) && (db->flags & 0xf) == (da->flags & 0xf) && tryPlace(a, da, b, db) == 1) return db;
            db++;
        } while (n != 0);
    }
    return nullptr;
}

// orig FUN_00125420 (work/decompiled/00125420.c): positions b so that db meets da; coordinates in 8-cell blocks.
int Dungeon::tryPlace(Room* a, const data::RdtDoor* da, Room* b, const data::RdtDoor* db) {
    const int bx = db->x >> 3, by = db->y >> 3;
    const int bw1 = b->rdt.w8 - 1, bh1 = b->rdt.h8 - 1;
    int ox, oy;                                   // edge block of db inside b
    switch (db->dir) {
    case 3: ox = 0; oy = by; break;
    case 2: ox = bx; oy = bh1; break;
    case 1: ox = bw1; oy = by; break;
    case 0: ox = bx; oy = 0; break;
    default: ox = bx; oy = by; break;
    }
    const int ax = da->x >> 3, ay = da->y >> 3;
    int tx, ty;                                   // block just outside a through da
    switch (da->dir) {
    case 3: tx = -1; ty = ay; break;
    case 2: tx = ax; ty = a->rdt.h8; break;
    case 1: tx = a->rdt.w8; ty = ay; break;
    case 0: tx = ax; ty = -1; break;
    default: tx = ax; ty = ay; break;             // LAB_001254f4 with uVar15/uVar13 unchanged
    }
    const int nx = tx + a->posX - ox;
    const int ny = ty + a->posY - oy;
    for (int i = 0; i < roomCount_; ++i) {
        const Room* o = &rooms_[static_cast<size_t>(i)];
        if (o == a || !o->placed()) continue;
        if (o->posX <= bw1 - ox + tx + a->posX && nx <= o->posX + o->rdt.w8 - 1 &&
            o->posY <= bh1 - oy + ty + a->posY && ny <= o->posY + o->rdt.h8 - 1)
            return 0;
    }
    b->posX = static_cast<int16_t>(nx);
    b->posY = static_cast<int16_t>(ny);
    b->flags |= 1;
    return 1;
}

// orig FUN_00124bf0 (work/decompiled/00124bf0.c): cell pointer if global (x,y) lies inside r's grid, else null
uint32_t* Dungeon::cellPtr(Room* r, int x, int y) {
    const int lx = x - r->posX * 8;
    const int w = r->rdt.w8 * 8;
    if (lx < 0 || w <= lx) return nullptr;
    const int ly = y - r->posY * 8;
    if (ly < 0 || (r->rdt.h8 << 3) <= ly) return nullptr;
    return &r->rdt.cells[static_cast<size_t>(ly * w + lx)];
}

// orig FUN_00124b40 (work/decompiled/00124b40.c): first placed room whose grid contains global cell (x,y)
Room* Dungeon::roomContaining(int x, int y) {
    for (int i = 0; i < roomCount_; ++i) {
        Room* r = &rooms_[static_cast<size_t>(i)];
        if (!r->placed()) continue;
        const int lx = x - r->posX * 8, ly = y - r->posY * 8;
        if (-1 < lx && lx < (r->rdt.w8 << 3) && -1 < ly && ly < (r->rdt.h8 << 3)) return r;
    }
    return nullptr;
}

// orig FUN_00125350 (work/decompiled/00125350.c)
void Dungeon::writeEndCap(uint32_t* c, uint32_t lvl, uint32_t dir) {
    *c = lvl | kEndCapByDir[dir] | 0x2002000u;
}

// orig FUN_00125380 (work/decompiled/00125380.c) with FUN_001252d0 (pieceByMask) / FUN_001252f0 (merge)
void Dungeon::writeCorridor(uint32_t* c, uint32_t lvl, uint32_t dir, uint32_t prev) {
    int32_t t = kPieceByMask[kDirMaskOut[dir] | kDirMaskIn[prev]];           // FUN_001252d0
    const int32_t existing = ((*c & kCellTypeMask) == kCellObject) ? static_cast<int32_t>(*c & 0xff) : -1;
    if (existing != -1)                                                       // FUN_001252f0
        t = kPieceByMask[kMaskByPiece[t] | kMaskByPiece[existing]];
    const uint32_t v = *c;
    *c = v & 0xffffff00u;
    *c = (v & 0xffffff00u) | static_cast<uint32_t>(t) | lvl | kCellObject;
}

// orig FUN_001257f0 (work/decompiled/001257f0.c): walk from door da of a to door db of b writing corridor pieces
// (test == 0) or only validating (test != 0). Cells are global: room.pos*8 + local. Register s5 holds a
// (Ghidra's unaff_10000068); the walking room r starts as a.
int Dungeon::dig(Room* a, data::RdtDoor* da, Room* b, data::RdtDoor* db, int test) {
    if (test == 0) { da->flags |= 0x10; db->flags |= 0x10; }
    const uint32_t lvl = (da->flags & 0xf) ? 0x100u : 0u;
    uint32_t dir = da->dir;
    const int tx = kDirDelta[db->dir][0] + db->x + b->posX * 8;
    const int ty = kDirDelta[db->dir][1] + db->y + b->posY * 8;
    int x = kDirDelta[dir][0] + da->x + a->posX * 8;
    int y = kDirDelta[dir][1] + da->y + a->posY * 8;
    Room* r = a;
    uint32_t* c = cellPtr(r, x, y);
    if (c == nullptr) {
        // The original writes through the null pointer here; never happens for the shipped data (door cells are
        // one cell inside the grid edge). Treated as a failed dig.
        platform::log("dungeon: door opening outside grid (FUN_001257f0)");
        return 0;
    }
    if (test == 0) writeEndCap(c, lvl, dir ^ 2);
    for (;;) {
        const uint32_t prev = dir;
        x += kDirDelta[prev][0];
        y += kDirDelta[prev][1];
        c = cellPtr(r, x, y);
        if (c == nullptr) {
            r = roomContaining(x, y);
            if (r == nullptr) return 0;
            c = cellPtr(r, x, y);
        }
        const int lx = x - r->posX * 8;
        const int ly = y - r->posY * 8;
        if (lx == 0 && ly == 0) {
            if ((db->dir & 1) == 0) {
                if (x == tx) dir = (y < ty) ? 2u : 0u; else dir = (x < tx) ? 1u : 3u;
            } else {
                if (y == ty) dir = (x < tx) ? 1u : 3u; else dir = (y < ty) ? 2u : 0u;
            }
        } else if (lx == 0) {
            if (y == ty) dir = (x < tx) ? 1u : 3u; else dir = (y < ty) ? 2u : 0u;
        } else if (ly == 0) {
            if (x == tx) dir = (y < ty) ? 2u : 0u; else dir = (x < tx) ? 1u : 3u;
        } else {
            dir = prev;
            if (r != a && r != b) return 0;
        }
        if (x == tx && y == ty) {
            if ((dir & 1) != static_cast<uint32_t>(db->dir & 1)) return 0;
            if (test != 0) return 1;
            writeEndCap(c, lvl, dir);
            return 1;
        }
        if (test == 0) { writeCorridor(c, lvl, dir, prev); continue; }
        // validation only
        if ((*c & kCellTypeMask) == kCellObject && lvl != (*c & kCellLevelMask)) return 0;
        if (dir == prev) continue;
        uint32_t* n = cellPtr(r, x + kDirDelta[prev][0], y + kDirDelta[prev][1]);
        if (n != nullptr && (*n & kCellTypeMask) == kCellObject && lvl != (*n & kCellLevelMask)) return 0;
        n = cellPtr(r, x + kDirDelta[dir ^ 2][0], y + kDirDelta[dir ^ 2][1]);
        if (n != nullptr && (*n & kCellTypeMask) == kCellObject && lvl != (*n & kCellLevelMask)) return 0;
    }
}

// orig FUN_00125c70 (work/decompiled/00125c70.c): connect facing doors of rooms that touch each other.
void Dungeon::connectAdjacent() {
    for (uint32_t i = 0; i < static_cast<uint32_t>(roomCount_); ++i) {
        Room* ri = &rooms_[i];
        if (!ri->placed()) continue;
        data::RdtDoor* doorsA = ri->rdt.doors.empty() ? nullptr : ri->rdt.doors.data();
        const uint8_t na = ri->rdt.doorCount;
        for (uint32_t j = i + 1; j < static_cast<uint32_t>(roomCount_); ++j) {
            Room* rj = &rooms_[j];
            if (!rj->placed()) continue;
            data::RdtDoor* doorsB = rj->rdt.doors.empty() ? nullptr : rj->rdt.doors.data();
            const uint8_t nb = rj->rdt.doorCount;
            long ka = 0, kb = 0;   // unaff_s8 / unaff_s1: stale for dirs > 3 (never happens)
            for (uint32_t ia = 0; ia < na; ++ia) {
                data::RdtDoor* da = &doorsA[ia];
                const uint8_t dirA = da->dir;
                if (dirA == 3) ka = ri->posX - 1;
                else if (dirA == 2) ka = ri->posY + ri->rdt.h8;
                else if (dirA == 1) ka = ri->posX + ri->rdt.w8;
                else if (dirA == 0) ka = ri->posY - 1;
                for (uint32_t ib = 0; ib < nb; ++ib) {
                    data::RdtDoor* dbp = &doorsB[ib];
                    const uint8_t dirB = dbp->dir;
                    if ((dirA ^ 2) != dirB || (da->flags & 0xf) != (dbp->flags & 0xf)) continue;
                    if (dirB == 3) kb = rj->posX;
                    else if (dirB == 2) kb = rj->posY + rj->rdt.h8 - 1;
                    else if (dirB == 1) kb = rj->posX + rj->rdt.w8 - 1;
                    else if (dirB == 0) kb = rj->posY;
                    if (ka == kb && dig(ri, da, rj, dbp, 1) == 1) dig(ri, da, rj, dbp, 0);
                }
            }
        }
    }
}

// orig FUN_00125fc0 (work/decompiled/00125fc0.c): blocked-door piece in front of every unconnected door.
void Dungeon::blockUnconnectedDoors() {
    for (int i = 0; i < roomCount_; ++i) {
        Room* r = &rooms_[static_cast<size_t>(i)];
        if (!r->placed()) continue;
        const uint32_t w8 = r->rdt.w8;
        for (uint32_t k = 0; k < r->rdt.doorCount; ++k) {
            const data::RdtDoor& d = r->rdt.doors[k];
            if ((d.flags & 0x10) != 0) continue;
            const uint32_t lvl = (d.flags & 0xf) ? 0x100u : 0u;
            const int cx = static_cast<int>(d.x) + kDoorCellDelta[d.dir][0];
            const int cy = static_cast<int>(d.y) + kDoorCellDelta[d.dir][1];
            const int idx = static_cast<int>(w8 * 8) * cy + cx;    // original indexes the grid without bounds checks
            if (cx < 0 || cx >= static_cast<int>(w8 * 8) || cy < 0 || idx < 0 || static_cast<size_t>(idx) >= r->rdt.cells.size()) {
                platform::log("dungeon: blocked door outside grid (FUN_00125fc0)");
                continue;
            }
            r->rdt.cells[static_cast<size_t>(idx)] = lvl | kBlockedDoorByDir[d.dir] | kCellObject;
        }
    }
}

// orig FUN_001245b0 (work/decompiled/001245b0.c): room object whose RDT id equals roomId (all rooms, placed or not)
Room* Dungeon::roomById(int roomId) {
    for (int i = 0; i < roomCount_; ++i)
        if (static_cast<int>(static_cast<char>(rooms_[static_cast<size_t>(i)].rdt.roomId)) == static_cast<int>(static_cast<char>(roomId)))
            return &rooms_[static_cast<size_t>(i)];
    return nullptr;
}

// ------------------------------------------------------------------------------------------ scenario floors
// orig FUN_0011e3d0 (work/decompiled/0011e3d0.c)
const ScenarioLayout* Dungeon::scenarioLayout() const {
    const int idx = floorNumber(floorId_) - 1;
    if (idx < 0 || idx >= 7) return nullptr;
    if (isScenarioA(floorId_)) return kScenarioLayoutsA[idx];
    if (isScenarioB(floorId_)) return kScenarioLayoutsB[idx];
    return kScenarioLayoutsC[idx];
}

// orig FUN_00124250 (work/decompiled/00124250.c)
void Dungeon::generateScenario() {
    ctx_.seed = rng_.state;
    resetGenerationState();
    ctx_.stairsRoom = -1;
    ctx_.stairsCellX = -1;
    ctx_.stairsCellZ = -1;
    ctx_.lastRoom = nullptr;
    const ScenarioLayout* layout = scenarioLayout();
    if (layout == nullptr) { platform::log("dungeon: no scenario layout"); return; }
    for (const ScenarioConn* c = layout->conns; c->room != 0xff; ++c) {
        Room* a = roomById(c->room);
        if (a == nullptr) { platform::log("dungeon: scenario room missing"); return; }   // original would dereference null
        if (ctx_.lastRoom == nullptr) {
            ctx_.lastRoom = a;
            a->posX = 0; a->posY = 0;
            a->flags |= 1;
        }
        if (c->door != 0xff) {
            data::RdtDoor* da = &a->rdt.doors[c->door];
            Room* b = roomById(c->room2);
            if (b == nullptr) { platform::log("dungeon: scenario room2 missing"); return; }
            data::RdtDoor* db = &b->rdt.doors[c->door2];
            tryPlace(a, da, b, db);          // result ignored by the original
            dig(a, da, b, db, 0);
        }
    }
    blockUnconnectedDoors();
    normaliseBounds();
}

// ------------------------------------------------------------------------------------------ contents
// orig FUN_0011ff50 / FUN_0011ffc0 / FUN_00120060 stage functions (work/decompiled/0011ff50.c, 0011ffc0.c, 00120060.c)
void Dungeon::placeContents() {
    buildPieces();          // FUN_00123280
    spawnPass(0);           // FUN_00120a70(ctx, 0)
    // FUN_0011ffc0: id 0x5b -> FUN_0011f790 (work/decompiled/0011f790.c): frees the floor's M%02d_TEX file task
    // (iRam0050a630, stored by the TEX load callback 0x11fdc0 of FUN_0011fb30) once the room models hold their
    // textures. Memory only: the port's FloorWorld keeps its texture library, nothing to do here.
    placePlayer();          // FUN_00120260
    spawnPass(1);           // FUN_00120a70(ctx, 1)
    fdtEntities();          // FUN_00121a00
    if (ent_.snapshotTagCounts) ent_.snapshotTagCounts();   // thunk_FUN_00127ff0(0x50b130) (FUN_0011ffc0)
    // FUN_00120060:
    randomItems();          // FUN_00121f80
    placeStairs();          // FUN_001222d0
    minimapDims();          // FUN_00126120
    finalizeCells();
    floorFlags_ |= 9;
    // FUN_001229d0 / FUN_0011d2a0(game, mapSet) / FUN_001f01f0: scene side effects, out of scope.
}

// orig FUN_00123280 (work/decompiled/00123280.c): one MT model per 0x2000 cell + floor collision mesh from the
// 'F' faces of MT%02d_<id>.CLS (ids 15..18 excluded). Alchemy node construction is replaced by PieceInstance.
void Dungeon::buildPieces() {
    for (int ri = 0; ri < roomCount_; ++ri) {
        Room* r = &rooms_[static_cast<size_t>(ri)];
        if (!r->placed()) continue;
        const uint32_t w = r->rdt.w8 * 8u, h = r->rdt.h8 * 8u;
        const float roomX = static_cast<float>(r->posX) * 8.0f * 200.0f + 100.0f;
        const float roomZ = static_cast<float>(r->posY) * 8.0f * 200.0f + 100.0f;
        int floorFaces = 0;
        for (uint32_t cy = 0; cy < h; ++cy) {
            const float fz = roomZ + 0.0f;
            for (uint32_t cx = 0; cx < w; ++cx) {
                const uint32_t cell = r->rdt.cells[cy * w + cx];
                if ((cell & kCellTypeMask) != kCellObject) continue;
                const uint8_t id = static_cast<uint8_t>(cell);
                PieceInstance p{};
                p.room = ri; p.cx = static_cast<int>(cx); p.cy = static_cast<int>(cy); p.id = id;
                p.y = (cell & kCellLevelMask) ? 300.0f : 0.0f;
                p.x = static_cast<float>(cx) * 200.0f + roomX + 0.0f;
                p.z = static_cast<float>(cy) * 200.0f + fz;
                p.mt = kPieceModel[id].mt;
                p.rotY = kPieceModel[id].rotY;
                p.modelPath = mtModelPath(fdt_.mapSet, static_cast<int>(p.mt));
                // bounds ±150 xz / -100..+400 y around (x,y,z) are set on the node (FUN_003979f0 object)
                pieces_.push_back(std::move(p));
                if (id != 0x12 && id != 0x11 && id != 0x10 && id != 0x0f) {
                    const auto& cls = mtCls_[id];
                    if (cls) for (const auto& f : cls->faces) if (f.type == 'F') floorFaces++;
                }
            }
        }
        if (floorFaces == 0) continue;
        for (uint32_t cy = 0; cy < h; ++cy) {
            const float fz = roomZ + 0.0f;
            for (uint32_t cx = 0; cx < w; ++cx) {
                const uint32_t cell = r->rdt.cells[cy * w + cx];
                if ((cell & kCellTypeMask) != kCellObject) continue;
                const uint32_t id = cell & 0xff;
                if (id == 0x12 || id == 0x11 || id == 0x10 || id == 0x0f) continue;
                const float fy = (cell & kCellLevelMask) ? 300.15f : 0.15f;
                const float fx = static_cast<float>(cx) * 200.0f + roomX + 0.0f;
                const auto& cls = mtCls_[id];
                if (!cls) continue;   // MT CLS not loaded (single-room floors): the original would read a stale slot
                for (const auto& f : cls->faces) {
                    if (f.type != 'F') continue;
                    FloorMeshTriangle t{};
                    for (int k = 0; k < 3; ++k) {
                        const data::ClsVec3& v = cls->vertices[f.verts[k]];
                        t.v[k][0] = v.x + fx;
                        t.v[k][1] = v.y + fy;
                        t.v[k][2] = v.z + static_cast<float>(cy) * 200.0f + fz;
                    }
                    floorMesh_.push_back(t);
                }
            }
        }
    }
}

// orig FUN_001d38f0 (work/decompiled/001d38f0.c): first two tag chars match one of the 15 item prefixes
bool Dungeon::isItemTag(uint32_t tag) {
    const char c0 = static_cast<char>(tag & 0xff), c1 = static_cast<char>((tag >> 8) & 0xff);
    for (uint32_t i = 0; i < 15; ++i)
        if (kItemPrefixes[i][0] == c0 && kItemPrefixes[i][1] == c1) return true;
    return false;
}

float Dungeon::randomAngleDeg() {
    const uint32_t v = rng_.rand(0x8000);   // u64 -> float idiom in every caller
    return (static_cast<float>(v) / 32768.0f) * 359.0f;
}

void Dungeon::record(Placement::Kind kind, uint32_t tag, const float* pos, float angleDeg, int room, std::string note) {
    placements_.push_back(Placement{kind, tag, pos[0], pos[1], pos[2], angleDeg, room, std::move(note)});
}

// orig FUN_00120a70 (work/decompiled/00120a70.c)
void Dungeon::spawnPass(int pass) {
    if (isScenarioFloor(floorId_) && pass == 1) {
        const ScenarioLayout* layout = scenarioLayout();
        if (layout != nullptr && layout->entities[0].room != 0xff) {
            for (const ScenarioEntity* e = layout->entities; ; ++e) {
                // FUN_0012ce70(0x50b1a0, tag, 1) -> entity; a null result stops the loop (UNVERIFIED which tags fail)
                void* ent = nullptr;
                if (ent_.create) {
                    ent = ent_.create(e->tag);
                    if (ent == nullptr) return;
                }
                // layout 0x48d8a8 (id 324): an 'MT00' entry (0x44a3e8) whose entity id (+0x1c) is neither 'MT03' nor
                // 'HT02' (bytes 0x48de78.. / 0x48de98.., disasm 0x120aec..0x120c14) gets entity flag 4 unless flag 2
                if (ent != nullptr && layout->entryAddr == 0x48d8a8 && e->tag == kTagMT00 && ent_.entityId &&
                    ent_.entityFlags && ent_.setEntityFlags) {
                    const uint32_t id = ent_.entityId(ent);
                    if (id != makeTag("MT03") && id != makeTag("HT02")) {
                        const uint32_t f = ent_.entityFlags(ent);
                        if ((f & 2u) == 0) ent_.setEntityFlags(ent, f | 4u);
                    }
                }
                Room* room = roomById(e->room);
                float pos[3] = {e->x, e->y, e->z};
                if (room != nullptr) {
                    pos[0] = e->x + static_cast<float>(room->posX) * 8.0f * 200.0f + 100.0f;
                    pos[2] = e->z + static_cast<float>(room->posY) * 8.0f * 200.0f + 100.0f;
                }
                if (ent != nullptr) ent_.setPos(ent, pos);
                const float ang = randomAngleDeg();
                if (ent != nullptr) ent_.rotateWorldUp(ent, (ang * 3.1415927f) / 180.0f);
                // FUN_001d38f0(ent +0x1c) -> FUN_001d5620(ent) (run by the rotateWorldUp hook for item entities); then
                // for 'T','P' ids (TP disks, `(id & 0xff) == 0x54 && (id >> 8 & 0xff) == 0x50`) FUN_001d4d70 makes the
                // entity's 0x10 (reversed) flag match the entry's +0x14 flag
                if (ent != nullptr && ent_.entityId && ent_.entityFlags && ent_.flipItem) {
                    const uint32_t id = ent_.entityId(ent);
                    if (isItemTag(id) && (id & 0xffu) == 0x54u && ((id >> 8) & 0xffu) == 0x50u) {
                        if (e->flag == 0) {
                            if ((ent_.entityFlags(ent) & 0x10u) != 0) ent_.flipItem(ent);
                        } else if ((ent_.entityFlags(ent) & 0x10u) == 0) {
                            ent_.flipItem(ent);
                        }
                    }
                }
                record(Placement::Kind::ScenarioEntity, e->tag, pos, ang, room ? room->index : -1);
                if (e[1].room == 0xff) break;
            }
        }
    }
    for (int i = 0; i < roomCount_; ++i) {
        Room* room = &rooms_[static_cast<size_t>(i)];
        if (!room->placed()) continue;
        uint32_t lastKey = 0;
        for (uint32_t k = 0; k < room->rdt.spawnCount; ++k) {
            const data::RdtSpawn& sp = room->rdt.spawns[k];
            if (sp.tag == kTagO99Z) { lastKey = 0; continue; }
            const uint32_t c0 = sp.tag & 0xff;
            const bool second = (c0 == 'C' || c0 == 'E');
            if ((pass == 1 && second) || (pass == 0 && !second)) spawnEntry(room, sp, &lastKey);
        }
    }
}

// orig FUN_00120f70 (work/decompiled/00120f70.c)
void Dungeon::spawnEntry(Room* room, const data::RdtSpawn& sp, uint32_t* lastKey) {
    if (sp.tag == kTagPLAY) return;
    const uint32_t key = isItemTag(sp.tag) ? sp.tag : (sp.tag & 0xffffffu);
    if (*lastKey != 0) {
        if (*lastKey == key) return;
        *lastKey = 0;
    }
    const int32_t cond = sp.cond;
    if (cond != 0) {
        if (cond < 0) {
            if (!flagSet(static_cast<uint32_t>(-cond))) return;
            *lastKey = key;          // suppresses the following same-key entry; never spawns itself
            return;
        }
        if (!flagSet(static_cast<uint32_t>(cond))) return;
        *lastKey = key;
    }
    float pos[3];
    if (isGeneratedFloor(floorId_)) {
        if (sp.x < 0.0f || sp.z < 0.0f) {
            if (randomCell(room, pos, kCellFloor) == 0) return;
        } else {
            pos[0] = sp.x + static_cast<float>(room->posX) * 8.0f * 200.0f + 100.0f;
            pos[2] = sp.z + static_cast<float>(room->posY) * 8.0f * 200.0f + 100.0f;
            pos[1] = sp.y;
            int cx, cy;
            uint32_t* c = cellAtWorld(pos, &cx, &cy);
            if (c != nullptr) *c |= kCellOccupied;   // original dereferences without a null check
        }
    } else {
        pos[0] = sp.x; pos[2] = sp.z; pos[1] = sp.y;
    }
    uint32_t tag = sp.tag;
    if (tag == kTagJ024) { record(Placement::Kind::ExtraModel, tag, pos, 0.0f, room->index, "DATA/MAP/M54/ME54_00.IGB"); return; }
    if (tag == kTagJ025) { record(Placement::Kind::ExtraModel, tag, pos, 0.0f, room->index, "DATA/MAP/M99/MR99_01.IGB"); return; }
    if (tag == kTagJ026) { record(Placement::Kind::ExtraModel, tag, pos, 0.0f, room->index, "DATA/MAP/M53/ME53_01.IGB"); return; }
    if ((tag & 0xffffffu) == kTagC05) {
        if (!flagSet(5)) return;
        if (flagSet(0x1e)) return;
        if (flagSet(0xc2)) return;
        if (flagSet(0x40f)) return;
        uint32_t picked = 0;
        for (uint32_t i = 0; i < 0xb; ++i)
            if (flagSet(kC05Table[i].flag)) { picked = kC05Table[i].tag; break; }
        if (picked == 0) return;
        setFlag(0x40f);
        tag = picked;
    } else if (tag == kTagC07A) {
        for (uint32_t i = 0; i < 6; ++i) {
            const C07Entry& e = kC07Table[i];
            if (e.flag != 0 && !flagSet(e.flag)) continue;
            if (e.floorA != floorId_) {
                if (e.floorB == 0) return;
                if (e.floorB != floorId_) return;
            }
            if (static_cast<uint32_t>(room->rdt.roomId) != e.roomId) return;
            break;
        }
    }
    if (floorId_ == 0x5b && prevFloorId_ == 0x5c && tag == kTagTP00 && flagSet(0x60)) return;
    // FUN_0012ce70(0x50b1a0, tag, 1) -> entity (null result = no spawn)
    void* ent = nullptr;
    if (ent_.create) {
        ent = ent_.create(tag);
        if (ent == nullptr) return;
        ent_.setPos(ent, pos);
    }
    float ang = sp.angleDeg;
    if (ang < 0.0f) ang = randomAngleDeg();
    if (ent != nullptr) ent_.rotateWorldUp(ent, (ang * 3.1415927f) / 180.0f);   // FUN_00128e90
    record(Placement::Kind::RdtSpawn, tag, pos, ang, room->index);
    // FUN_001d38f0(tag) -> FUN_001d5620(entity): item initialisation, out of scope
}

// orig FUN_00120260 (work/decompiled/00120260.c). The player's writes (FUN_001286c0, transform +0xc..+0x14,
// FUN_00128ae0) go through EntityHooks::placePlayer at this point of the sequence (before FUN_00120a70(ctx, 1));
// FUN_0013e130 / FUN_0013e300 (camera reset) are the Game scene's (it resets the camera after the load).
void Dungeon::placePlayer() {
    placePlayerStart();
    if (ent_.placePlayer) ent_.placePlayer(player_);
}

void Dungeon::placePlayerStart() {
    player_ = PlayerStart{};
    player_.valid = true;
    if (floorId_ == 0x5b) {
        if (prevFloorId_ == 0x5c || (300 < static_cast<uint32_t>(prevFloorId_) && static_cast<uint32_t>(prevFloorId_) < 500)) {
            player_.x = -600.0f; player_.y = 0.0f; player_.z = -100.0f;   // 0xc4160000, 0, 0xc2c80000
            player_.angleRad = 3.1415927f;                                   // 0x40490fdb
            return;
        }
    } else if (isScenarioFloor(floorId_)) {
        const ScenarioLayout* layout = scenarioLayout();
        Room* room = layout ? roomById(layout->start.room) : nullptr;
        if (layout && room) {
            player_.x = layout->start.x + static_cast<float>(room->posX) * 8.0f * 200.0f + 100.0f;
            player_.y = layout->start.y;
            player_.z = layout->start.z + static_cast<float>(room->posY) * 8.0f * 200.0f + 100.0f;
            player_.angleRad = (static_cast<float>(layout->start.angleDeg) * 3.1415927f) / 180.0f;
        }
        return;
    }
    if (isFinalFloor(floorId_)) {
        const ScenarioLayout* layout = kScenarioLayoutsA[6];   // PTR_PTR_0044a1c8 = 0x44a1b0[6] (id 307 layout)
        Room* room = roomById(layout->start.room);
        if (room) {
            player_.x = layout->start.x + static_cast<float>(room->posX) * 8.0f * 200.0f + 100.0f;
            player_.y = layout->start.y;
            player_.z = layout->start.z + static_cast<float>(room->posY) * 8.0f * 200.0f + 100.0f;
            player_.angleRad = (static_cast<float>(layout->start.angleDeg) * 3.1415927f) / 180.0f;
        }
        return;
    }
    if (isRandomDungeon(floorId_) && floorId_ == 0x8c) {
        const Room& r0 = rooms_[0];   // iRam0050a640 (first room object)
        player_.x = static_cast<float>(r0.posX) * 8.0f * 200.0f + 100.0f + 800.0f;
        player_.y = 0.0f;
        player_.z = static_cast<float>(r0.posY) * 8.0f * 200.0f + 100.0f + 1000.0f;
        player_.angleRad = 1.5707964f;   // 0x3fc90fdb
        return;
    }
    if ((floorFlags_ & 2) == 0) {
        bool found = false;
        // all rooms, placed or not; the break only leaves the spawn loop, so the last room with a PLAY entry wins
        // (each match with angle < 0 draws rand(0x8000))
        for (int i = 0; i < roomCount_; ++i) {
            const Room& r = rooms_[static_cast<size_t>(i)];
            for (uint32_t k = 0; k < r.rdt.spawnCount; ++k) {
                const data::RdtSpawn& sp = r.rdt.spawns[k];
                if (sp.tag != kTagPLAY) continue;
                player_.x = sp.x; player_.y = sp.y; player_.z = sp.z;   // used as absolute coordinates
                float ang = sp.angleDeg;
                if (ang < 0.0f) ang = randomAngleDeg();
                player_.angleRad = (ang * 3.1415927f) / 180.0f;
                found = true;
                break;
            }
        }
        if (!found) {
            float pos[3] = {0, 0, 0};
            randomCellAny(pos, kCellFloor);   // result ignored by the original
            player_.x = pos[0]; player_.y = pos[1]; player_.z = pos[2];
            player_.angleRad = (randomAngleDeg() * 3.1415927f) / 180.0f;
        }
        return;
    }
    if ((floorFlags_ & 4) == 0) {
        // reload path: keep the current position shifted by the new bounding-box origin (ctx+0x1c/+0x1e * 200)
        player_.shift = true;
        player_.x = static_cast<float>(ctx_.minX * 200);
        player_.z = static_cast<float>(ctx_.minY * 200);
        return;
    }
    Room* first = nullptr;
    for (int i = 0; i < roomCount_; ++i) if (rooms_[static_cast<size_t>(i)].placed()) { first = &rooms_[static_cast<size_t>(i)]; break; }
    float pos[3] = {0, 0, 0};
    if (first == nullptr || randomCell(first, pos, kCellFloor) == 0) { pos[0] = pos[1] = pos[2] = 0.0f; }
    player_.x = pos[0]; player_.y = pos[1]; player_.z = pos[2];
    player_.angleRad = (randomAngleDeg() * 3.1415927f) / 180.0f;
}

// orig FUN_0011e7c0 (work/decompiled/0011e7c0.c): table 0x44a210 entry for the current floor id
const ExtraSpawn* Dungeon::extraSpawnEntry() const {
    if (floorId_ == 0x7f && flagSet(0x3c1)) return &kExtraSpawns[2];   // &DAT_0044a228
    for (uint32_t i = 0; i < 0x18; ++i)
        if (kExtraSpawns[i].floorId == floorId_) return &kExtraSpawns[i];
    return nullptr;
}

// orig FUN_00121a00 (work/decompiled/00121a00.c)
void Dungeon::fdtEntities() {
    for (uint32_t i = 0; i < fdt_.entityTableCount; ++i) {
        const data::FdtEntity& e = fdt_.entities[i];
        for (uint32_t k = 0; k < e.count; ++k) fdtEntity(e);
    }
    if ((floorFlags_ & 4) == 0 && flagSet(0x1e) && isSecondDungeon(floorId_) && flagSet(0x3c0) && flagSet(0x3c5)) {
        const ExtraSpawn* ex = extraSpawnEntry();
        if (ex == nullptr) return;
        for (int n = ex->count; n != 0; --n) {
            float pos[3];
            if (randomCellAny(pos, kCellFloor) == 0) return;
            // FUN_0012ce70(0x50b1a0, tag, 1); null -> return
            void* ent = nullptr;
            if (ent_.create) {
                ent = ent_.create(ex->tag);
                if (ent == nullptr) return;
                ent_.setPos(ent, pos);
                ent_.resetAxes(ent);                                   // FUN_001286c0
            }
            const float ang = randomAngleDeg();
            if (ent != nullptr) ent_.rotateUp(ent, (ang * 3.1415927f) / 180.0f);   // FUN_00128ae0
            record(Placement::Kind::ExtraSpawn, ex->tag, pos, ang, -1);
        }
    }
}

// orig FUN_00121c50 (work/decompiled/00121c50.c)
void Dungeon::fdtEntity(const data::FdtEntity& e) {
    if ((floorFlags_ & 4) != 0 && static_cast<char>(e.tag & 0xff) == 'E') return;   // re-entry: no enemies
    const uint32_t key3 = e.tag & 0xffffffu;
    const bool onCorridor = (key3 == kTagE16 || key3 == kTagE19);
    float pos[3];
    if (onCorridor) { if (randomCellAny(pos, kCellObject) == 0) return; }
    else { if (randomCellAny(pos, kCellFloor) == 0) return; }
    // FUN_0012ce70(0x50b1a0, tag, 1) -> entity (not null-checked by the original); transform = pos
    void* ent = nullptr;
    if (ent_.create) {
        ent = ent_.create(e.tag);
        if (ent != nullptr) ent_.setPos(ent, pos);
    }
    float ang = 0.0f;
    if (onCorridor) {
        int cx, cy;
        const uint32_t* c = cellAtWorld(pos, &cx, &cy);
        const uint8_t piece = c ? static_cast<uint8_t>(*c) : 0xff;
        if (piece == 0) ang = (rng_.rand(100) < 0x32) ? 0.0f : 180.0f;
        else if (piece == 1) ang = (rng_.rand(100) < 0x32) ? 90.0f : 270.0f;
        // other piece ids leave f20 uninitialised in the original; FUN_001248a0 only returns ids 0/1 for 0x2000
    } else {
        ang = randomAngleDeg();
    }
    if (ent != nullptr) {
        ent_.resetAxes(ent);                                           // FUN_001286c0
        ent_.rotateUp(ent, (ang * 3.1415927f) / 180.0f);               // FUN_00128ae0
    }
    record(Placement::Kind::FdtEntity, e.tag, pos, ang, -1);
    // FUN_001d38f0 -> FUN_001d5620 (item init): items module
}

// orig FUN_001d2f30 (work/decompiled/001d2f30.c): weighted item category from FDT bytes 8..0x13 (modified in place
// by the quest flags 0x398/0x399/0x39a exactly like the original rewrites the loaded FDT data).
int Dungeon::pickItemCategory() {
    if (!fdtLoaded_) return -1;              // FUN_001d30b0 returns uRam0050a620
    uint8_t* w = fdt_.itemCategoryWeight;
    if (flagSet(0x398)) { w[0] = 0; w[1] = 0; w[2] = 0; }
    else if (flagSet(0x399)) { w[3] = 0; w[4] = 0; }
    else if (flagSet(0x39a)) { w[0] = w[1] = w[2] = w[3] = w[4] = w[5] = w[6] = w[7] = 0; w[8] = 0x32; w[9] = 0x32; w[10] = 0; w[11] = 0; }
    else if (flagSet(0x39c)) return -1;
    else if (flagSet(0x39d) && rng_.rand(100) < 0x32) return 9;
    uint8_t r = static_cast<uint8_t>(rng_.rand(99));
    for (int i = 0; i < 0xc; ++i) {
        const uint8_t wi = w[i];
        if (r < wi) return i;
        r = static_cast<uint8_t>(r - wi);
    }
    return -1;
}

// orig FUN_001d30c0 (work/decompiled/001d30c0.c): rand(0xff) then a weighted walk over the category's item list
// (FUN_001d2c90 -> PRM item tables, weights by floor tier FUN_001d37d0). The list walk is delegated to the hook.
uint32_t Dungeon::pickItem(int category) {
    if (category == -1) return 0;
    const uint32_t r = rng_.rand(0xff);
    if (itemPicker_) return itemPicker_(category, r);
    return 0;   // UNVERIFIED: item identity requires the PRM item database (data module)
}

// orig FUN_00121f80 (work/decompiled/00121f80.c)
void Dungeon::randomItems() {
    if ((floorFlags_ & 4) != 0) return;
    for (int8_t n = static_cast<int8_t>(fdt_.randomItemCount); n != 0; --n) {
        float pos[3];
        if (randomCellAny(pos, 0) != 1) continue;
        const int cat = pickItemCategory();     // FUN_001d2f30
        const uint32_t item = pickItem(cat);    // FUN_001d30c0
        if (item != 0) {
            // FUN_0012ce70(0x50b1a0, item, 1), transform +0xc..+0x14 = cell position, FUN_001d5620(entity)
            if (void* ent = ent_.create ? ent_.create(item) : nullptr) {
                if (ent_.setPos) ent_.setPos(ent, pos);
                if (ent_.placeItem) ent_.placeItem(ent);
            }
            record(Placement::Kind::RandomItem, item, pos, 0.0f, -1);
        }
        else if (cat >= 0) record(Placement::Kind::RandomItem, 0, pos, 0.0f, -1, std::string("category ") + kItemPrefixes[cat]);
    }
    if (isRandomDungeon(floorId_)) {
        for (int i = 0; i < roomCount_; ++i) {
            Room* r = &rooms_[static_cast<size_t>(i)];
            if (!r->placed()) continue;
            for (uint32_t k = 0; k < 2; ++k) {
                const FixedItem& fi = kFixedItems[k];
                if (r->rdt.roomId == fi.roomId && fdt_.mapSet == fi.mapSet && rng_.rand(100) < 0x50) {
                    float pos[3];
                    pos[0] = static_cast<float>(r->posX) * 8.0f * 200.0f + 100.0f + fi.x;
                    pos[1] = fi.y;
                    pos[2] = static_cast<float>(r->posY) * 8.0f * 200.0f + 100.0f + fi.z;
                    // FUN_0012ce70(0x50b1a0, tag, 1) (created before the position is computed), FUN_001d5620(entity)
                    if (void* ent = ent_.create ? ent_.create(fi.tag) : nullptr) {
                        if (ent_.setPos) ent_.setPos(ent, pos);
                        if (ent_.placeItem) ent_.placeItem(ent);
                    }
                    record(Placement::Kind::FixedItem, fi.tag, pos, 0.0f, r->index);
                }
            }
        }
        if (floorId_ == 100 && ent_.scatterOuterBoxes) ent_.scatterOuterBoxes();   // FUN_001d9a20(0x53d910)
    }
}

// orig FUN_00126350 (work/decompiled/00126350.c)
void Dungeon::setStairsCell(float x, float z) {
    ctx_.stairsCellX = static_cast<int32_t>(x / 200.0f);
    ctx_.stairsCellZ = static_cast<int32_t>(z / 200.0f);
}

// orig FUN_001222d0 (work/decompiled/001222d0.c). The stairs / exit entity is FUN_00179f60(tag) (stage object list
// 0x53c690); it is created through EntityHooks::create (which dispatches "PT" tags to the stage objects) at the
// original call point, and its transform +0xc..+0x14 is written through EntityHooks::setPos. FUN_00179f60 has no
// null check in the original; a null hook result only skips the transform write.
void* Dungeon::createStage(uint32_t tag) {
    return ent_.create ? ent_.create(tag) : nullptr;
}
void Dungeon::setStagePos(void* ent, const float* pos) {
    if (ent != nullptr && ent_.setPos) ent_.setPos(ent, pos);
}
void Dungeon::placeStairs() {
    if (floorId_ == 0x5b) {
        for (uint32_t i = 0; i < 3; ++i) {
            const TownStairs& t = kTownStairs[i];
            if (t.tag == makeTag("PT12")) {
                if (flagSet(0x2c8)) return;
                if (flagSet(0x12)) return;
            }
            void* ent = createStage(t.tag);           // FUN_00179f60(tag)
            float pos[3] = {t.x, 0.0f, t.z};          // transform +0xc / +0x10 (0) / +0x14
            setStagePos(ent, pos);
            record(Placement::Kind::Stairs, t.tag, pos, 0.0f, -1);
        }
        return;
    }
    if (isRandomDungeon(floorId_)) {
        if (isFloor99(floorId_) || floorId_ == 0x8c || isNoStairsFloor(floorId_)) { ctx_.stairsRoom = -1; return; }
        void* stage = createStage(kTagPT00);          // FUN_00179f60("PT00"), before the cell search
        const float ppos[3] = {player_.x, player_.y, player_.z};
        int cx, cy;
        const uint32_t* pc = cellAtWorld(ppos, &cx, &cy);
        const int playerRoom = pc ? static_cast<int>(*pc & kCellRoomMask) : -1;
        float pos[3] = {0, 0, 0};
        if (randomCellOutside(pos, playerRoom, kCellFloor) == 0) randomCellAny(pos, kCellFloor);
        setStagePos(stage, pos);
        record(Placement::Kind::Stairs, kTagPT00, pos, 0.0f, -1);
        setStairsCell(pos[0], pos[2]);
        uint32_t* c = cellAtWorld(pos, &cx, &cy);
        if (c == nullptr) { ctx_.stairsRoom = -1; return; }
        ctx_.stairsRoom = static_cast<int32_t>(*c & kCellRoomMask);
        *c |= kCellStairs;
        return;
    }
    if (isScenarioFloor(floorId_)) {
        const ScenarioLayout* layout = scenarioLayout();
        Room* room = layout ? roomById(static_cast<uint8_t>(layout->exit.room)) : nullptr;
        if (!layout || !room) { ctx_.stairsRoom = -1; return; }
        const uint32_t tag = usesPt14Stairs(floorId_) ? kTagPT14 : kTagPT00;
        void* stage = createStage(tag);               // FUN_00179f60(tag)
        float pos[3];
        pos[0] = layout->exit.x + static_cast<float>(room->posX) * 8.0f * 200.0f + 100.0f;
        pos[1] = layout->exit.y;
        pos[2] = layout->exit.z + static_cast<float>(room->posY) * 8.0f * 200.0f + 100.0f;
        setStagePos(stage, pos);
        record(Placement::Kind::Stairs, tag, pos, 0.0f, room->index);
        setStairsCell(pos[0], pos[2]);
        int cx, cy;
        uint32_t* c = cellAtWorld(pos, &cx, &cy);
        if (c == nullptr) { ctx_.stairsRoom = -1; return; }
        ctx_.stairsRoom = static_cast<int32_t>(*c & kCellRoomMask);
        *c |= kCellStairs;
        return;
    }
    if (!isSecondDungeon(floorId_)) return;
    const uint32_t tag = isFinalFloor(floorId_) ? kTagPT14 : kTagPT00;
    void* stage = createStage(tag);                   // FUN_00179f60(tag)
    float pos[3] = {0, 0, 0};
    int roomIdx = -1;
    if (isFinalFloor(floorId_)) {
        Room* r9 = roomById(9);
        if (r9 == nullptr) { ctx_.stairsRoom = -1; return; }   // original dereferences null
        pos[0] = static_cast<float>(r9->posX) * 8.0f * 200.0f + 100.0f + 1200.0f;
        pos[1] = 0.0f;
        pos[2] = static_cast<float>(r9->posY) * 8.0f * 200.0f + 100.0f + 1200.0f;
        roomIdx = r9->index;
    } else {
        const float ppos[3] = {player_.x, player_.y, player_.z};
        int cx, cy;
        const uint32_t* pc = cellAtWorld(ppos, &cx, &cy);
        const int playerRoom = pc ? static_cast<int>(*pc & kCellRoomMask) : -1;
        if (randomCellOutside(pos, playerRoom, kCellFloor) == 0) randomCellAny(pos, kCellFloor);
    }
    setStagePos(stage, pos);
    record(Placement::Kind::Stairs, tag, pos, 0.0f, roomIdx);
    setStairsCell(pos[0], pos[2]);
    int cx, cy;
    uint32_t* c = cellAtWorld(pos, &cx, &cy);
    if (c == nullptr) { ctx_.stairsRoom = -1; return; }
    ctx_.stairsRoom = static_cast<int32_t>(*c & kCellRoomMask);
    *c |= kCellStairs;
}

// orig FUN_00126120 (work/decompiled/00126120.c), dimension part: minimap image is pow2 >= bounds (8..0x200).
void Dungeon::minimapDims() {
    auto pow2 = [](uint16_t v) -> int32_t {
        if (v < 9) return 8;
        if (v < 0x11) return 0x10;
        if (v < 0x21) return 0x20;
        if (v < 0x41) return 0x40;
        if (v < 0x81) return 0x80;
        if (v < 0x101) return 0x100;
        if (v < 0x201) return 0x200;
        return static_cast<int16_t>(v);
    };
    ctx_.mapW = pow2(static_cast<uint16_t>(ctx_.maxX));
    ctx_.mapH = pow2(static_cast<uint16_t>(ctx_.maxY));
    // FUN_00106350(0x4e6a68, w, h, 0, 1) image allocation + ctx+0xb4/+0xb8/+0xbc/+0xc0: out of scope
}

// orig FUN_00120060 (work/decompiled/00120060.c): clear the occupied bit of every cell that is not a stairs cell
void Dungeon::finalizeCells() {
    for (int i = 0; i < roomCount_; ++i) {
        Room& r = rooms_[static_cast<size_t>(i)];
        if (!r.placed()) continue;
        for (uint32_t& c : r.rdt.cells)
            if ((c & kCellStairs) == 0) c &= 0xffefffffu;
    }
}

// orig FUN_001248a0 (work/decompiled/001248a0.c): the rand(w*h)-th eligible cell scanning rows cyclically with a
// visit budget of (r+1)*w*h; eligible = non-void, !(cell & 0x1100000), type match (0 = any; 0x2000 only ids 0/1).
int Dungeon::randomCell(Room* room, float* pos, uint32_t type) {
    const uint8_t w8 = room->rdt.w8, h8 = room->rdt.h8;
    const uint32_t w = w8 * 8u, h = h8 * 8u;
    const int total = static_cast<int>(w * h);
    int r = static_cast<int>(rng_.rand(static_cast<uint32_t>(total)));
    int budget = (r + 1) * total;
    for (;;) {
        for (uint32_t cy = 0; cy < h; ++cy) {
            for (uint32_t cx = 0; cx < w; ++cx) {
                uint32_t* c = &room->rdt.cells[cy * w + cx];
                const uint32_t v = *c;
                if (v != 0 && (v & 0x1100000u) == 0 && (type == 0 || type == (v & kCellTypeMask))) {
                    bool ok = true;
                    if (type == kCellObject) ok = ((v & 0xff) == 0) || ((v & 0xff) == 1);
                    if (ok) {
                        if (r == 0) {
                            pos[1] = (v & kCellLevelMask) ? 300.0f : 0.0f;
                            pos[0] = static_cast<float>(cx) * 200.0f + static_cast<float>(room->posX) * 8.0f * 200.0f + 100.0f + 0.0f;
                            pos[2] = static_cast<float>(cy) * 200.0f + static_cast<float>(room->posY) * 8.0f * 200.0f + 100.0f + 0.0f;
                            if ((floorFlags_ & 1) != 0) return 1;
                            *c |= kCellOccupied;
                            return 1;
                        }
                        r--;
                    }
                }
                budget--;
                if (budget == 0) return 0;
            }
        }
    }
}

// orig FUN_001247c0 (work/decompiled/001247c0.c): prev == null -> the rand(16)-th eligible room from index 0;
// else the next eligible room after prev (cyclic). Eligible = placed and NOT start-candidate (flag bit0).
Room* Dungeon::nextSpawnRoom(Room* prev) {
    uint32_t idx;
    int r;
    uint32_t remaining = static_cast<uint32_t>(roomCount_);
    if (prev == nullptr) { idx = 0; r = static_cast<int>(rng_.rand(0x10)); }
    else { idx = static_cast<uint32_t>(prev->index) + 1; r = 0; }
    for (;;) {
        if (static_cast<uint32_t>(roomCount_) <= idx) { idx = 0; remaining = static_cast<uint32_t>(roomCount_); }
        Room* room = &rooms_[idx];
        if (!room->placed() || (room->rdt.flags & 1) != 0) {
            remaining--;
            if (remaining == 0) return nullptr;
        } else {
            if (r == 0) return room;
            r--;
        }
        idx++;
    }
}

// orig FUN_00124730 (work/decompiled/00124730.c)
int Dungeon::randomCellAny(float* pos, uint32_t type) {
    Room* room = nullptr;
    int n = roomCount_;
    for (;;) {
        const bool done = (n == 0);
        n--;
        if (done) return 0;
        room = nextSpawnRoom(room);
        if (room == nullptr) return 0;
        if (randomCell(room, pos, type) != 0) return 1;
    }
}

// orig FUN_00124660 (work/decompiled/00124660.c): random cell in a room other than excludeRoom (index)
int Dungeon::randomCellOutside(float* pos, int excludeRoom, uint32_t type) {
    if (excludeRoom < 0) return 0;
    Room* exclude = &rooms_[static_cast<size_t>(excludeRoom)];
    Room* room = nullptr;
    int n = roomCount_;
    for (;;) {
        const bool done = (n == 0);
        n--;
        if (done) return 0;
        room = nextSpawnRoom(room);
        if (room == nullptr) return 0;
        if (room == exclude) continue;
        if (randomCell(room, pos, type) != 0) return 1;
    }
}

// orig FUN_00124c70 (work/decompiled/00124c70.c): cell pointer for a world position; also returns the cell coords.
uint32_t* Dungeon::cellAtWorld(const float* pos, int* cx, int* cy) {
    Room* room;
    if (isGeneratedFloor(floorId_)) {
        *cx = static_cast<int>(pos[0]) / 200;   // cvt.w.s then signed division (disasm 0x124cb4..)
        *cy = static_cast<int>(pos[2]) / 200;
        room = roomContaining(*cx, *cy);
    } else {
        int ox, oy;
        if (floorId_ == 0x5b) { ox = 0x17; oy = 0x14; }
        else if (isFloor99(floorId_)) { ox = 6; oy = 9; }
        else { *cx = 0; *cy = 0; return nullptr; }
        float f = pos[0] / 200.0f;
        if (f < 0.0f) f = f - 1.0f;
        *cx = ox + static_cast<int>(f);
        f = pos[2] / 200.0f;
        if (f < 0.0f) f = f - 1.0f;
        *cy = oy + static_cast<int>(f);
        room = roomCount_ > 0 ? &rooms_[0] : nullptr;   // iRam0050a640
    }
    if (room == nullptr) return nullptr;
    const int lx = *cx - room->posX * 8, ly = *cy - room->posY * 8;
    const int w = room->rdt.w8 * 8;
    const long idx = static_cast<long>(ly) * w + lx;   // original indexes without bounds checks
    if (lx < 0 || lx >= w || ly < 0 || idx < 0 || static_cast<size_t>(idx) >= room->rdt.cells.size()) return nullptr;
    return &room->rdt.cells[static_cast<size_t>(idx)];
}

} // namespace baroque::game
