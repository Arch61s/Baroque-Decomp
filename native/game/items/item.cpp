#include "game/items/item.hpp"
#include "game/items/inventory.hpp"
#include <cstdio>
#include <cstring>

namespace baroque::game {

// 0x463eb0: 15 two-letter prefixes [V]
const char* const kItemClassPrefix[kItemClassCountAll] = {"SD", "CT", "WG", "HT", "MT", "BX", "LQ", "ST", "TL", "TP", "BN", "WM", "GN", "IS", "CC"};

// 0x499490 [V data words]: {radius, gravity scale, decel}
const ItemClassPhysics kItemClassPhysics[kItemClassCountAll] = {
    {50.0f, 0.74f, 0.1f}, {50.0f, 0.8f, 0.6f}, {40.0f, 0.4f, 0.0f}, {25.0f, 0.39f, 0.0f}, {25.0f, 0.39f, 0.0f},
    {25.0f, 0.4f, 0.0f},  {25.0f, 0.4f, 0.0f}, {25.0f, 0.32f, 0.0f}, {35.0f, 0.48f, 0.0f}, {50.0f, 0.65f, 0.3f},
    {25.0f, 0.25f, 0.0f}, {25.0f, 0.35f, 0.0f}, {50.0f, 1.0f, 0.2f},  {22.0f, 0.8f, 0.1f},  {25.0f, 1.0f, 0.0f}};

namespace {
constexpr uint32_t kIdWM22 = itemId("WM22");
constexpr uint32_t kIdST10 = itemId("ST10");
constexpr uint32_t kIdSD09 = itemId("SD09");
constexpr uint32_t kIdCT08 = itemId("CT08");
constexpr uint32_t kIdWG01 = itemId("WG01");
constexpr uint32_t kIdWG02 = itemId("WG02");
constexpr uint32_t kIdWG03 = itemId("WG03");
constexpr uint32_t kIdWG04 = itemId("WG04");
constexpr uint32_t kIdHT02 = itemId("HT02");
constexpr uint32_t kIdMT03 = itemId("MT03");
constexpr uint32_t kIdBX24 = itemId("BX24");
constexpr uint32_t kIdWM19 = itemId("WM19");
constexpr uint32_t kIdWM06 = itemId("WM06");
constexpr uint32_t kIdWM07 = itemId("WM07");

// Table 0x499340 (filled by the static ctor FUN_004c8c50 from the strings 0x4642c0.. / 0x464400..): 34 pairs
// {crystal id, sword id}: IS00..IS06 -> SD30..SD36, IS11..IS37 -> SD37..SD63 [V strings].
struct FusionPair { uint32_t crystal, sword; };
const FusionPair* fusionTable() {
    static FusionPair table[0x22];
    static bool built = false;
    if (!built) {
        for (int i = 0; i < 7; ++i) table[i] = {makeItemId(kClsIS, i), makeItemId(kClsSD, 30 + i)};
        for (int k = 0; k < 27; ++k) table[7 + k] = {makeItemId(kClsIS, 11 + k), makeItemId(kClsSD, 37 + k)};
        built = true;
    }
    return table;
}

int16_t highHalf(int32_t w) { return static_cast<int16_t>(static_cast<uint32_t>(w) >> 16); }
}  // namespace

uint32_t makeItemId(int cls, int idx) {
    char buf[8];
    std::snprintf(buf, sizeof buf, "%s%02d", kItemClassPrefix[cls < 0 || cls >= kItemClassCountAll ? 0 : cls], idx);
    return static_cast<uint32_t>(static_cast<uint8_t>(buf[0])) | (static_cast<uint32_t>(static_cast<uint8_t>(buf[1])) << 8) |
           (static_cast<uint32_t>(static_cast<uint8_t>(buf[2])) << 16) | (static_cast<uint32_t>(static_cast<uint8_t>(buf[3])) << 24);
}

std::string itemIdString(uint32_t id) {
    std::string s;
    for (int i = 0; i < 4; ++i) {
        const char c = static_cast<char>((id >> (8 * i)) & 0xff);
        if (c == 0) break;
        s.push_back(c);
    }
    return s;
}

// orig FUN_001d2bf0 (work/decompiled/001d2bf0.c)
int itemClassOf(uint32_t id) {
    const char c0 = static_cast<char>(id & 0xff), c1 = static_cast<char>((id >> 8) & 0xff);
    for (int i = 0; i < kItemClassCountAll; ++i)
        if (kItemClassPrefix[i][0] == c0 && kItemClassPrefix[i][1] == c1) return i;
    return kClsNone;
}

// orig FUN_001d38f0 (work/decompiled/001d38f0.c)
bool isItemId(uint32_t id) { return itemClassOf(id) != kClsNone; }

// orig FUN_001d2c60 (work/decompiled/001d2c60.c): atoi of chars 2..3
int itemIndexOf(uint32_t id) {
    char buf[3] = {static_cast<char>((id >> 16) & 0xff), static_cast<char>((id >> 24) & 0xff), 0};
    return std::atoi(buf);
}

// orig FUN_001d2c90 (work/decompiled/001d2c90.c + jump table 0x499450): record idx of class file cls
const data::PrmItem* ItemSystem::row(int cls, int idx) const {
    if (!prm || cls < 0 || cls > 0xd || idx < 0) return nullptr;
    const auto& f = prm->items[static_cast<size_t>(cls)];
    if (static_cast<size_t>(idx) >= f.records.size()) return nullptr;
    return &f.records[static_cast<size_t>(idx)];
}

int32_t ItemSystem::word(int cls, int idx, uint32_t offset) const {
    const data::PrmItem* r = row(cls, idx);
    if (!r) return 0;
    const auto& f = prm->items[static_cast<size_t>(cls)];
    const uint32_t stride = data::kItemStride[cls];
    if (offset + 4 > stride) return 0;
    const size_t o = r->fileOffset + offset;
    if (o + 4 > f.raw.size()) return 0;
    int32_t v;
    std::memcpy(&v, f.raw.data() + o, 4);
    return v;
}

// orig FUN_001d2ba0 (work/decompiled/001d2ba0.c)
int ItemSystem::rowCount(int cls) const {
    if (!prm || cls < 0 || cls > 0xd) return 0;
    return static_cast<int>(prm->items[static_cast<size_t>(cls)].records.size());
}

const char* ItemSystem::name(int cls, int idx) const {
    const data::PrmItem* r = row(cls, idx);
    if (!r) return "";
    const auto& f = prm->items[static_cast<size_t>(cls)];
    return reinterpret_cast<const char*>(f.raw.data() + r->nameOfs);
}

const char* ItemSystem::desc(int cls, int idx) const {
    const data::PrmItem* r = row(cls, idx);
    if (!r) return "";
    const auto& f = prm->items[static_cast<size_t>(cls)];
    return reinterpret_cast<const char*>(f.raw.data() + r->descOfs);
}

// orig FUN_001d37d0 / FUN_001d3840 band selection (work/decompiled/001d37d0.c)
int floorBand(const GameAccess& game) {
    const unsigned f = static_cast<unsigned>(game.floorNumber());
    if (f < 5) return 0;
    if (f < 0xd) return 1;
    if (f < 0x16) return 2;
    return 3;
}
int32_t dropWeight(const ItemSystem& sys, const data::PrmItem& row) { return row.dropWeight[floorBand(*sys.game)]; }
int32_t identifyChance(const ItemSystem& sys, const data::PrmItem& row) { return row.f1c[floorBand(*sys.game)]; }
// orig FUN_001d38b0 (work/decompiled/001d38b0.c)
int32_t rollVariant(ItemSystem& sys, const data::PrmItem& row) { return row.variant[sys.rand(0xf)]; }

// orig FUN_001d3d40 (work/decompiled/001d3d40.c)
void initItemById(Item& item, ItemSystem& sys) {
    if (item.id == kIdWM22) {
        const uint32_t r = sys.rand(7);
        char buf[8];
        std::snprintf(buf, sizeof buf, "WM%02d", static_cast<int>(r) + 1);   // 0x464210
        item.id = static_cast<uint32_t>(static_cast<uint8_t>(buf[0])) | (static_cast<uint32_t>(static_cast<uint8_t>(buf[1])) << 8) |
                  (static_cast<uint32_t>(static_cast<uint8_t>(buf[2])) << 16) | (static_cast<uint32_t>(static_cast<uint8_t>(buf[3])) << 24);
        initItemClass(item, sys);
        parasiteDie(item);
    } else {
        initItemClass(item, sys);
    }
    if (item.taskFlags & 1) sys.game->itemEnterWorld(item);   // FUN_001d3a30 (task link + OBJ%03d.IGB model)
}

namespace {
// orig FUN_001d4100 (work/decompiled/001d4100.c)
void initSword(Item& it, ItemSystem& sys) {
    it.atkBase = sys.word(it, 0x88);
    it.atkMod = rollVariant(sys, *sys.row(it));
    if (it.id == kIdSD09) {
        it.defMod = highHalf(sys.word(it, 0x7c));   // s16 at +0x7e
    } else if (it.idx > 0x1d) {
        it.crystalId = crystalOfSword(it.id);        // FUN_001d4a50
        it.crystalFlags = 1;
    }
}
// orig FUN_001d41e0 (work/decompiled/001d41e0.c)
void initCoat(Item& it, ItemSystem& sys) {
    it.defBase = sys.word(it, 0x88);
    it.defMod = rollVariant(sys, *sys.row(it));
    if (it.id == kIdCT08) it.atkMod = highHalf(sys.word(it, 0x7c));
}
// orig FUN_001d42a0 (work/decompiled/001d42a0.c)
void initWings(Item& it, ItemSystem& sys) {
    const int32_t v = rollVariant(sys, *sys.row(it));
    if (it.id == kIdWG01) it.atkBase = v;
    else if (it.id == kIdWG02) it.defBase = v;
    else if (it.id == kIdWG03) it.wg03Bonus = v;
    else if (it.id == kIdWG04) { it.hpMaxBonus = v; it.vtMaxBonus = v; it.atkBase = v; it.defBase = v; }
}
// orig FUN_001d4400 (HT) / FUN_001d4530 (MT) (work/decompiled/001d4400.c, 001d4530.c)
void initFood(Item& it, ItemSystem& sys, uint32_t neverRotId) {
    GameAccess& g = *sys.game;
    if (g.isHubFloor() || g.floorId() == 0x5b) return;
    if (g.testFlag(0x39b)) { it.flags |= kItemRotten; return; }
    if (it.id == neverRotId) return;
    const int seed = g.floorSeedByte();
    if (seed < 0) return;                           // pcRam0050a620 == 0
    if (seed == 4) it.flags |= kItemRotten;
    else if (seed != 5 && rollVariant(sys, *sys.row(it)) < 0) it.flags |= kItemRotten;
}
// orig FUN_001d4660 (BX) / FUN_001d46e0 (LQ) / FUN_001d4720 (TP) / FUN_001d4760 (BN) / FUN_001d47a0 (WM) / FUN_001d4820 (GN)
void initCount(Item& it, ItemSystem& sys, uint32_t zeroId) {
    it.count = rollVariant(sys, *sys.row(it));
    if (zeroId != 0 && it.id == zeroId) it.count = 0;
}
}  // namespace

// orig FUN_001d3e30 (work/decompiled/001d3e30.c)
void initItemClass(Item& item, ItemSystem& sys) {
    item.cls = itemClassOf(item.id);
    if (item.cls != kClsFlesh) {
        item.idx = itemIndexOf(item.id);
        rollIdentified(item, sys);
        rollCurse(item, sys);
        if (sys.row(item)) {
            switch (item.cls) {
                case kClsSD: initSword(item, sys); break;
                case kClsCT: initCoat(item, sys); break;
                case kClsWG: initWings(item, sys); break;
                case kClsHT: initFood(item, sys, kIdHT02); break;
                case kClsMT: initFood(item, sys, kIdMT03); break;
                case kClsBX: initCount(item, sys, kIdBX24); break;
                case kClsLQ: initCount(item, sys, 0); break;
                case kClsTP: initCount(item, sys, 0); break;
                case kClsBN: initCount(item, sys, 0); break;
                case kClsWM: initCount(item, sys, kIdWM19); break;
                case kClsGN: initCount(item, sys, 0); break;
                default: break;   // ST, TL, IS: nothing
            }
        }
    }
    // FUN_0012dcc0 -> FUN_00142d50 new physics object (FUN_00140140: flags 0, gravity 1, vy 0, ground cleared;
    // +0x70..+0x84 zero) + FUN_00142e10(radius 0x499490[cls*3]); the game's entity keeps the Phys (item_entity.cpp)
    item.gravityScale = 1.0f;
    item.moveVel = {};
    item.physFlags = 0;
    item.vy = 0.0f;
    item.onFloor = false;
    item.floorAttr = nullptr;
    item.hasFloorNormal = false;
}

// orig FUN_001d3f90 (work/decompiled/001d3f90.c)
void rollIdentified(Item& item, ItemSystem& sys) {
    if (sys.game->isHubFloor()) { item.flags |= kItemIdentified; return; }
    const data::PrmItem* r = sys.row(item);
    const uint32_t roll = sys.rand(0xff);
    if (r && roll < static_cast<uint32_t>(identifyChance(sys, *r))) item.flags |= kItemIdentified;
}

// orig FUN_001d4030 (work/decompiled/001d4030.c): FUN_001d2e80 only resolves classes 0..2 (SD/CT/WG)
void rollCurse(Item& item, ItemSystem& sys) {
    if (item.cls > kClsWG) return;
    const data::PrmItem* r = sys.row(item);
    if (!r) return;
    if (sys.rand(0xff) < static_cast<uint32_t>(r->chance74)) item.brand = kIdST10;
    for (int i = 0; i < 2 && i < r->effectCount; ++i)
        if (r->effects[i].kind == '!') { item.flags |= kItemCursedSealed; return; }
}

// orig FUN_001d3c70 (work/decompiled/001d3c70.c): item destructor body. Destroying a sword / coat (class < 2) with an
// ATK or DEF modifier of +30 or more sets flag 800 and the guarded flag 0x44e; the contained item (+0x34) and the
// parasite (+0x38) are freed (FUN_001b7570), the model released with the task unlinked (FUN_001d3b30); the physics
// object (+0x30, FUN_0012da60) belongs to the game's entity.
void destructItem(Item& item, ItemSystem& sys) {
    GameAccess& g = *sys.game;
    if (static_cast<uint32_t>(item.cls) < 2 && !(item.atkMod < 0x1e && item.defMod < 0x1e)) {
        g.setFlag(800);
        g.setGuardedFlag(0x44e);
    }
    if (item.contained != nullptr) {
        Item* c = item.contained;
        item.contained = nullptr;
        g.freeItem(c);
    }
    if (item.parasite != nullptr) {
        Item* p = item.parasite;
        item.parasite = nullptr;
        g.freeItem(p);
    }
    if (item.hasModel) g.itemLeaveWorld(item);
}

// orig FUN_001d3c20 (work/decompiled/001d3c20.c): +0x2c..+0x70 and +0x88 cleared
void clearItemFields(Item& item) {
    item.hasModel = false;      // +0x2c
    item.contained = nullptr;   // +0x34
    item.parasite = nullptr;    // +0x38
    item.savedId = 0;           // +0x3c
    item.brand = 0;             // +0x40
    item.crystalId = 0;         // +0x44
    item.crystalFlags = 0;      // +0x48
    item.hpMaxBonus = 0;        // +0x4c
    item.vtMaxBonus = 0;        // +0x50
    item.atkBase = 0;           // +0x54
    item.defBase = 0;           // +0x58
    item.wg03Bonus = 0;         // +0x5c
    item.atkMod = 0;            // +0x60
    item.defMod = 0;            // +0x64
    item.count = 0;             // +0x6c
    item.unused68 = 0;          // +0x68
    item.flags = 0;             // +0x70
    item.thrower = nullptr;     // +0x88
}

// orig FUN_001d4860 (work/decompiled/001d4860.c): destructor body, fields cleared, class init by the new id, the
// identified bit kept; an item that had a model is linked into the world again (FUN_001d3a30).
void changeItemId(Item& item, uint32_t newId, ItemSystem& sys) {
    const uint32_t oldFlags = item.flags;
    const bool hadModel = item.hasModel;
    destructItem(item, sys);    // FUN_001d3c70
    clearItemFields(item);      // FUN_001d3c20
    item.id = newId;
    initItemClass(item, sys);   // FUN_001d3e30
    if (oldFlags & kItemIdentified) item.flags |= kItemIdentified; else item.flags &= ~kItemIdentified;
    if (hadModel) sys.game->itemEnterWorld(item);   // FUN_001d3a30
}

// orig FUN_001d4aa0 (work/decompiled/001d4aa0.c)
void parasiteDie(Item& item) {
    item.savedId = item.id;
    item.id = kIdWM22;
    item.idx = 0x16;
}

// orig FUN_001d4af0 (work/decompiled/001d4af0.c)
void parasiteRevive(Item& item) {
    item.id = item.savedId;
    item.savedId = 0;
    item.idx = itemIndexOf(item.id);
}

// orig FUN_001d4b40 (work/decompiled/001d4b40.c)
bool cannibalEat(Item& eater, const Item& victim, ItemSystem& sys) {
    const int add = victim.parasite ? 2 : 1;
    eater.count += add;
    if (eater.count > 10) {
        changeItemId(eater, kIdWM06, sys);
        eater.count = rollVariant(sys, *sys.row(eater));
        Item* p = sys.game->createItem(kIdWM07, false);
        if (p) {
            p->count = 0xb - eater.count;
            eater.parasite = p;
        }
        return true;
    }
    return false;
}

// orig FUN_001d4a20 (work/decompiled/001d4a20.c)
bool canFuseCrystal(const Item& sword) { return sword.cls == kClsSD && sword.idx > 0x1c; }

// orig FUN_001d4a50 (work/decompiled/001d4a50.c)
uint32_t crystalOfSword(uint32_t swordId) {
    const FusionPair* t = fusionTable();
    for (int i = 0; i < 0x22; ++i) if (t[i].sword == swordId) return t[i].crystal;
    return 0;
}
uint32_t swordOfCrystal(uint32_t crystalId) {
    const FusionPair* t = fusionTable();
    for (int i = 0; i < 0x22; ++i) if (t[i].crystal == crystalId) return t[i].sword;
    return 0;
}

namespace {
// shared body of FUN_001d4f70 / FUN_001d5030
bool addModifier(int& mod, int base, uint32_t flags, int delta) {
    if (delta < 0 && (flags & kItemSealed)) return false;
    if (delta > 0 && mod + base == 99) return false;
    if (delta < 0 && mod + base == -99) return false;
    mod += delta;
    const int sum = mod + base;
    if (sum < -99) mod = -99 - base;
    else if (sum < 100) return true;
    else mod = 99 - base;
    return true;
}
}  // namespace
// orig FUN_001d4f70 (work/decompiled/001d4f70.c)
bool addAtkMod(Item& item, int delta) { return addModifier(item.atkMod, item.atkBase, item.flags, delta); }
// orig FUN_001d5030 (work/decompiled/001d5030.c)
bool addDefMod(Item& item, int delta) { return addModifier(item.defMod, item.defBase, item.flags, delta); }

// orig FUN_0015a380 (work/decompiled/0015a380.c)
bool isParasiteHostWithParasite(const Item& item) {
    if ((item.id & 0xff) != 'W') return false;
    if (((item.id >> 8) & 0xff) == 'M') {
        if (item.id == kIdWM22) return false;
        if (item.parasite) return true;
    }
    return false;
}

// orig FUN_0015a410 (work/decompiled/0015a410.c): buffer 0x5180a0 (64 bytes). EN: "<host>" + separator (data string
// 0x4e5c98 = "/" [V ELF data]) + parasite name from byte strlen(0x44be68 "\x92\x8e") = 2 on; JP: host name minus
// strlen(0x44be68) trailing bytes + parasite name.
void appendStr(char* dst, size_t cap, const char* src) {
    size_t n = std::strlen(dst);
    for (; *src != 0 && n + 1 < cap; ++src, ++n) dst[n] = *src;
    if (n < cap) dst[n] = 0;
}
void copyPrefix(char* dst, size_t cap, const char* src, size_t n) {
    size_t i = 0;
    for (; i < n && src[i] != 0 && i + 1 < cap; ++i) dst[i] = src[i];
    if (i < cap) dst[i] = 0;
}

const char* composedParasiteName(const Item& item, const ItemSystem& sys) {
    static char buf[0x40];
    std::memset(buf, 0, sizeof buf);
    const char* host = sys.name(item.cls, item.idx);
    const char* para = item.parasite ? sys.name(item.parasite->cls, item.parasite->idx) : "";
    if (sys.game->language() == 1) {
        const size_t hl = std::strlen(host);
        copyPrefix(buf, sizeof buf, host, hl >= 2 ? hl - 2 : 0);
        appendStr(buf, sizeof buf, para);
    } else {
        copyPrefix(buf, sizeof buf, host, sizeof buf);
        appendStr(buf, sizeof buf, "/");   // 0x4e5c98
        appendStr(buf, sizeof buf, std::strlen(para) >= 2 ? para + 2 : para);
    }
    return buf;
}

// orig FUN_001d3ba0 (work/decompiled/001d3ba0.c)
const char* itemDisplayName(const Item& item, const ItemSystem& sys) {
    if (!item.identified()) return sys.game->str(0x1b8 + item.cls);
    if (isParasiteHostWithParasite(item)) return composedParasiteName(item, sys);
    return sys.name(item.cls, item.idx);
}

// orig FUN_001d5b30 (work/decompiled/001d5b30.c)
void writeItemSubRecord(const Item& it, uint32_t* o) {
    o[0] = it.id; o[1] = static_cast<uint32_t>(it.cls); o[2] = static_cast<uint32_t>(it.idx);
    o[3] = it.savedId; o[4] = it.brand; o[5] = it.crystalId; o[6] = it.crystalFlags;
    o[7] = static_cast<uint32_t>(it.hpMaxBonus); o[8] = static_cast<uint32_t>(it.vtMaxBonus);
    o[9] = static_cast<uint32_t>(it.atkBase); o[10] = static_cast<uint32_t>(it.defBase); o[0xb] = static_cast<uint32_t>(it.wg03Bonus);
    o[0xc] = static_cast<uint32_t>(it.atkMod); o[0xd] = static_cast<uint32_t>(it.defMod); o[0xe] = static_cast<uint32_t>(it.unused68);
    o[0xf] = static_cast<uint32_t>(it.count); o[0x10] = it.flags;
}

// orig FUN_001d5d20 (work/decompiled/001d5d20.c): everything but the id (set by the factory)
void readItemSubRecord(Item& it, const uint32_t* i) {
    it.cls = static_cast<int>(i[1]); it.idx = static_cast<int>(i[2]);
    it.savedId = i[3]; it.brand = i[4]; it.crystalId = i[5]; it.crystalFlags = i[6];
    it.hpMaxBonus = static_cast<int>(i[7]); it.vtMaxBonus = static_cast<int>(i[8]);
    it.atkBase = static_cast<int>(i[9]); it.defBase = static_cast<int>(i[10]); it.wg03Bonus = static_cast<int>(i[0xb]);
    it.atkMod = static_cast<int>(i[0xc]); it.defMod = static_cast<int>(i[0xd]); it.unused68 = static_cast<int>(i[0xe]);
    it.count = static_cast<int>(i[0xf]); it.flags = i[0x10];
}

// orig FUN_001d5a50 (work/decompiled/001d5a50.c); FUN_00116560 zeroes the 0x198 bytes for an empty slot
void writeItemRecord(const Item* item, ItemSaveRecord& out) {
    out = ItemSaveRecord{};
    if (!item) return;
    writeItemSubRecord(*item, out.w);
    if (item->contained) {
        writeItemSubRecord(*item->contained, out.w + 0x11);
        if (const Item* p = item->contained->parasite) {
            writeItemSubRecord(*p, out.w + 0x22);
            if (p->parasite) writeItemSubRecord(*p->parasite, out.w + 0x33);
        }
    }
    if (item->parasite) {
        writeItemSubRecord(*item->parasite, out.w + 0x44);
        if (item->parasite->parasite) writeItemSubRecord(*item->parasite->parasite, out.w + 0x55);
    }
}

// orig FUN_001d5bc0 (work/decompiled/001d5bc0.c)
Item* readItemRecord(const ItemSaveRecord& rec, ItemSystem& sys) {
    const uint32_t* w = rec.w;
    if (w[0] == 0) return nullptr;
    GameAccess& g = *sys.game;
    Item* item = g.createItem(w[0], false);
    if (!item) return nullptr;
    readItemSubRecord(*item, w);
    if (w[0x11] != 0) {
        Item* c = g.createItem(w[0x11], false);
        if (c) {
            readItemSubRecord(*c, w + 0x11);
            item->contained = c;
            if (w[0x22] != 0) {
                Item* cp = g.createItem(w[0x22], false);
                if (cp) {
                    readItemSubRecord(*cp, w + 0x22);
                    c->parasite = cp;
                    if (w[0x33] != 0) {
                        Item* cpp = g.createItem(w[0x33], false);
                        if (cpp) { readItemSubRecord(*cpp, w + 0x33); cp->parasite = cpp; }
                    }
                }
            }
        }
    }
    if (w[0x44] != 0) {
        Item* p = g.createItem(w[0x44], false);
        if (p) {
            readItemSubRecord(*p, w + 0x44);
            item->parasite = p;
            if (w[0x55] != 0) {
                Item* pp = g.createItem(w[0x55], false);
                if (pp) { readItemSubRecord(*pp, w + 0x55); p->parasite = pp; }
            }
        }
    }
    return item;
}

}  // namespace baroque::game
