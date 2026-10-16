#include "game/items/game_adapter.hpp"
#include "game/character/character.hpp"
#include "game/dungeon.hpp"
#include "game/flag_manager.hpp"
#include "game/hud/hud_api.hpp"
#include "game/hud/message_log.hpp"
#include "game/items/chr_adapter.hpp"
#include "game/game_world.hpp"
#include "game/items/item_links.hpp"
#include "game/items/item_screen.hpp"
#include <cmath>
#include "game/sound_stub.hpp"
#include "game/talk/talk_manager.hpp"
#include "game/talk/talk_wiring.hpp"
#include "game/ui/menu_tables.hpp"
#include "game/ui/strings.hpp"
#include <algorithm>

namespace baroque::game {

// ---- flags / counters (0x53c3b0 / 0x53c5c0) -----------------------------------------------------------------
bool GameAdapter::testFlag(uint32_t id) const { return flags().test(id); }        // FUN_00178a80
void GameAdapter::setFlag(uint32_t id) { flags().set(id); }                        // FUN_00178890
void GameAdapter::clearFlag(uint32_t id) { flags().clear(id); }                    // FUN_00178a30
// orig FUN_001787c0 (work/decompiled/001787c0.c): v == 1 -> set, else clear
void GameAdapter::setFlagValue(uint32_t id, bool v) { if (v) flags().set(id); else flags().clear(id); }
// orig FUN_00178800 (work/decompiled/00178800.c): the inverted form, v == 1 -> clear, else set
void GameAdapter::setFlagRaw(uint32_t id, bool v) { if (v) flags().clear(id); else flags().set(id); }
void GameAdapter::setGuardedFlag(uint32_t id) { flags().setGuarded(static_cast<int>(id)); }   // FUN_00179320
// orig FUN_001793b0 (work/decompiled/001793b0.c): clears bit (id + 0xaf9)
void GameAdapter::setAchievementFlag(uint32_t id) { flags().clear(id + 0xaf9); }
// FUN_00179570: Counters::add is the full translation (type gate, cap table 0x496b50, flag on max, side effects)
void GameAdapter::addCounter(int id, int delta) { counters().add(id, delta); }
int GameAdapter::counter(int id) const { return id >= 0 && id < 0x40 ? counters().values[id] : 0; }

// ---- floor (Game scene 0x50a2a0 / floor context 0x50a350) --------------------------------------------------
bool GameAdapter::isHubFloor() const { return isScenarioFloor(world.floorId); }      // FUN_0011c2c0
bool GameAdapter::isDungeonType1() const { return isRandomDungeon(world.floorId); }  // FUN_0011c290
bool GameAdapter::isDungeonType3() const { return isSecondDungeon(world.floorId); }  // FUN_0011c390
bool GameAdapter::isWideGiveRange() const { return isFloor99(world.floorId); }       // FUN_0011c100 (floor id == 99)
int GameAdapter::floorNumber() const { return game::floorNumber(world.floorId); }    // FUN_0011c3b0
int GameAdapter::floorId() const { return world.floorId; }
int GameAdapter::floorSeedByte() const { return world.floorSeedByte; }
uint32_t GameAdapter::floorFlags() const { return world.floorFlags; }
void GameAdapter::setFloorFlags(uint32_t mask) {
    world.floorFlags |= mask;
    world.portFloorFlags |= mask;   // kept across the per-tick sync from the Dungeon (see ItemWorldState)
}
int GameAdapter::targetCellRoom() const { return world.targetCellRoom; }
// Room queries on the generated floor (gameWorld().dungeon: room table 0x50a640, cell lookup FUN_00124c70); without
// a floor (items check, front end) every query answers "no room" like an empty room table would.
namespace {
// orig FUN_001248a0 (work/decompiled/001248a0.c): the n-th (n = rand(w*h)) cell of the room whose word is non-zero,
// not flagged 0x1100000 and of kind `kind` (0 = any; kind 0x2000 also needs a low byte of 0 or 1), counted in row
// order with wrap-around for at most (n + 1) * w * h steps. Position = cell centre (y 300 for a raised cell,
// +0xf00 bits); while floor flag bit 0 is clear the cell is reserved (|= 0x100000).
bool randomRoomCell(Room& r, Vec3& out, uint32_t kind, uint32_t floorFlags) {
    const int bw = r.rdt.w8, bh = r.rdt.h8;
    const uint32_t w = static_cast<uint32_t>(bw) * 8u, h = static_cast<uint32_t>(bh) * 8u;
    if (bh == 0 || bw == 0 || r.rdt.cells.size() < static_cast<size_t>(w) * h) return false;   // PORT: the original spins
    uint32_t total = w * h;
    int n = static_cast<int>(gameWorld().rng.rand(total));   // FUN_002071e0(0x53eb20, w*h)
    uint32_t steps = (static_cast<uint32_t>(n) + 1u) * total;
    for (;;) {
        for (uint32_t y = 0; y < h; ++y) {
            for (uint32_t x = 0; x < w; ++x) {
                uint32_t& c = r.rdt.cells[static_cast<size_t>(y) * w + x];
                if (c != 0 && (c & 0x1100000u) == 0 && (kind == 0 || kind == (c & 0xf000u))) {
                    bool ok = true;
                    if (kind == 0x2000u) ok = (c & 0xffu) == 0 || (c & 0xffu) == 1;
                    if (ok) {
                        if (n == 0) {
                            out.y = (c & 0xf00u) == 0 ? 0.0f : 300.0f;
                            out.x = static_cast<float>(x) * 200.0f + static_cast<float>(static_cast<int>(r.posX)) * 8.0f * 200.0f + 100.0f + 0.0f;
                            out.z = static_cast<float>(y) * 200.0f + static_cast<float>(static_cast<int>(r.posY)) * 8.0f * 200.0f + 100.0f + 0.0f;
                            if ((floorFlags & 1u) == 0) c |= 0x100000u;
                            return true;
                        }
                        --n;
                    }
                }
                if (--steps == 0) return false;
            }
        }
    }
}
}  // namespace
// orig FUN_00124620 (work/decompiled/00124620.c): room < 0 -> 0, else FUN_001248a0(0x50a640[room], out, 0x1000)
bool GameAdapter::randomCellInRoom(int room, Vec3& out) {
    if (room < 0) return false;
    Dungeon* d = gameWorld().dungeon;
    if (d == nullptr || room >= d->roomCount()) return false;   // PORT: the original reads 0x50a640[room] unchecked
    return randomRoomCell(*d->mutableRoom(room), out, 0x1000u, d->floorFlags());
}
// FUN_00124c70(pos, &x, &y) and the room-cell test (word & 0xf000 == 0x1000 -> word & 0xf, else -1)
int GameAdapter::roomAt(const Vec3& pos) const {
    Dungeon* d = gameWorld().dungeon;
    if (d == nullptr) return -1;
    const float p[3] = {pos.x, pos.y, pos.z};
    int cx = 0, cy = 0;
    const uint32_t* c = d->cellAtWorld(p, &cx, &cy);
    if (c == nullptr || (*c & 0xf000u) != 0x1000u) return -1;
    return static_cast<int>(*c & 0xfu);
}
// *(byte *)(*(0x50a640[room] + 4) + 7) & 1: RDT flags byte bit 0 (FUN_001e0050)
bool GameAdapter::roomIsSealed(int room) const {
    const Dungeon* d = gameWorld().dungeon;
    if (d == nullptr || room < 0 || room >= d->roomCount()) return false;
    return (d->room(room).rdt.flags & 1u) != 0;
}
// FUN_00140050 (game/character: damage wall 'W' / attr 'D' / flag 0x49) through the entity hooks; false without them
bool GameAdapter::floorIsWaterish(const void* face) const { return itemEntityHooks() != nullptr && itemEntityHooks()->damageWall(face); }
// FUN_001d2f30 = Dungeon::pickItemCategory (FUN_001d30b0 null -> -1)
int GameAdapter::floorItemCategory() {
    Dungeon* d = gameWorld().dungeon;
    return d != nullptr ? d->pickItemCategory() : -1;
}

// ---- item database (FUN_001f8ad0 / FUN_001f8ac0 / FUN_001db000) ----------------------------------------------
// Item code table PTR_DAT_00466f10 (ui::kItemCodes, 0x142 codes + nullptr) and the flag table 0x49b950
// (ui::kItemFlags). The codes are compared as their little-endian 4-char word (the padding entries are "").
namespace {
uint32_t codeWord(const char* c) {
    uint32_t w = 0;
    for (int i = 0; i < 4 && c[i] != 0; ++i) w |= static_cast<uint32_t>(static_cast<uint8_t>(c[i])) << (8 * i);
    return w;
}
int databaseIndex(uint32_t id, int limit) {
    for (int i = 0; i < limit && ui::kItemCodes[i] != nullptr; ++i)
        if (codeWord(ui::kItemCodes[i]) == id) return i;
    return -1;
}
}  // namespace
// orig FUN_001f8ad0 (work/decompiled/001f8ad0.c): the item's database flag is set
bool GameAdapter::itemSeen(uint32_t id) const {
    const int i = databaseIndex(id, 0x142);
    return i >= 0 && flags().test(static_cast<uint32_t>(ui::kItemFlags[i]));
}
// orig FUN_001f8ac0 (work/decompiled/001f8ac0.c): the same test on item +0x1c
bool GameAdapter::itemHasDatabaseEntry(const Item& item) const { return itemSeen(item.id); }
// orig FUN_001db000 (work/decompiled/001db000.c) with FUN_001f8be0 (flag of the code, -1 when the code is not listed;
// the loop runs to the nullptr terminator): sets the flag and counter 0x10 += 1 the first time; true then
bool GameAdapter::markItemCollected(uint32_t id) {
    const int i = databaseIndex(id, 0x143);
    const uint32_t f = i >= 0 ? static_cast<uint32_t>(ui::kItemFlags[i]) : 0xffffffffu;
    const bool had = flags().test(f);
    if (!had) {
        flags().set(f);
        counters().add(0x10, 1);
    }
    return !had;
}

// ---- text / sound / effects ------------------------------------------------------------------------------------
int GameAdapter::language() const { return ui::language(); }    // FUN_00114c80
const char* GameAdapter::str(int strId) const { return ui::str(strId); }   // FUN_00114da0
// FUN_0015d480(0x5180f0, FUN_00114da0(id), ...)
void GameAdapter::message(int strId, MsgArg a, MsgArg b, MsgArg c) {
    if (log == nullptr) return;
    const MsgArg args[3] = {a, b, c};
    char buf[0x200];
    formatMsg(buf, sizeof buf, ui::str(strId), args, 3);
    log->printFormatted(buf);
}
void GameAdapter::messageFmt(const char* fmt, MsgArg a, MsgArg b) {
    if (log == nullptr) return;
    const MsgArg args[2] = {a, b};
    char buf[0x200];
    formatMsg(buf, sizeof buf, fmt, args, 2);
    log->printFormatted(buf);
}
void GameAdapter::se(int id) { sound().playSe(1.0f, id); }      // FUN_00115390(1.0, 0x4e9790, id)
// FUN_00115470(1.0, 0x4e9790, id, pos)
void GameAdapter::se3d(int id, const Vec3& pos) {
    const float p[3] = {pos.x, pos.y, pos.z};
    sound().playSe3d(1.0f, id, p);
}
// The voice lines of the item screen (FUN_001586a0 / FUN_0015c1a0): FUN_001161c0(snd) == 0 (+0x4ac, no override) ->
// FUN_00115c10(snd) + FUN_00115970(1.0, snd, id, 0)
void GameAdapter::voice(int id) {
    if (linkVoiceOverride()) return;
    linkVoiceStop();
    linkVoicePlay(id);
}
// FUN_00112190(0, id, pos, rot, (scale, scale, scale, 1.0)) / FUN_00112530 / FUN_001126e0(h, 0, -1, -1) / FUN_001122d0
int GameAdapter::effect(int id, const Vec3& pos, float scale, const float* rotDeg, float tag) {
    return linkEffectSpawn(id, pos, rotDeg, scale, tag);
}
void GameAdapter::effectKill(int handle) { linkEffectKill(handle); }
void GameAdapter::effectEnd(int handle) { linkEffectEnd(handle); }
void GameAdapter::effectMove(int handle, const Vec3& pos) { linkEffectSetPos(handle, pos); }
int GameAdapter::effectMatrix(int id, const float* m) { return linkEffectSpawnMatrix(id, m); }

ItemEntityHooks*& itemEntityHooks() {
    static ItemEntityHooks* h = nullptr;
    return h;
}

// ---- items and actors -------------------------------------------------------------------------------------------
// FUN_0012e810 storage: FUN_001d3960 ctor (+0x2c..+0x70, +0x88 zero, state slot FUN_001db660) = Item defaults
Item* GameAdapter::allocItem() {
    items_.push_back(std::make_unique<Item>());
    return items_.back().get();
}
// orig FUN_00128160 (work/decompiled/00128160.c), item kind: append to 0x50a830
void GameAdapter::registerItem(Item* item) {
    if (item != nullptr && std::find(itemList_.begin(), itemList_.end(), item) == itemList_.end()) itemList_.push_back(item);
}

// FUN_0012ce70(0x50b1a0, id, addToWorld), item branch: FUN_001d38f0 class test, FUN_00128060 / FUN_00128340 cap of
// 500, FUN_0012e810 alloc, link into the task list 0x50b134 when addToWorld == 1 (FUN_002073e0), FUN_00128690 (type 3,
// id) -> vtable +0x1c FUN_001d3d40 init by id, FUN_00128160. In the game the task-list record is made by
// ChrManager::createTag through the entity hooks; the items check keeps the same flow without records.
Item* GameAdapter::createItem(uint32_t id, bool addToWorld) {
    if (!isItemId(id) || sys == nullptr) return nullptr;
    if (itemEntityHooks() != nullptr) return itemEntityHooks()->create(id, addToWorld);
    if (itemList_.size() == 500) dropFarthestItem();   // FUN_00128060 -> FUN_00128340
    Item* p = allocItem();
    p->taskFlags = addToWorld ? 1u : 0u;                // FUN_002073e0 sets bit 0
    p->type = 3;
    p->id = id;
    initItemById(*p, *sys);
    registerItem(p);
    return p;
}

// orig FUN_001d39b0 (item dtor, via FUN_00132200): FUN_001dba50 state slot, FUN_001d3c70, FUN_00128610 (FUN_00128230
// out of 0x50a830, transform freed, task unlinked)
void GameAdapter::destroyItem(Item* item) {
    auto owned = [&] {
        return std::find_if(items_.begin(), items_.end(), [&](const std::unique_ptr<Item>& u) { return u.get() == item; });
    };
    if (owned() == items_.end()) return;
    item->state = ItemStateSlot{};                      // FUN_001dba50
    if (sys != nullptr) destructItem(*item, *sys);      // FUN_001d3c70 (frees +0x34 / +0x38, FUN_001d3b30)
    itemList_.erase(std::remove(itemList_.begin(), itemList_.end(), item), itemList_.end());   // FUN_00128230
    item->taskFlags |= 0x30000000u;
    auto own = owned();                                 // destructItem may have freed other items
    std::unique_ptr<Item> dead = std::move(*own);
    items_.erase(own);
    if (itemEntityHooks() != nullptr) {
        itemEntityHooks()->destroyed(*item);
        graveyard_.push_back(std::move(dead));          // released once no task-list record references it
    }
}
// FUN_001b7570: vt+8(item, -1) = FUN_00132200 -> FUN_001d39b0
void GameAdapter::freeItem(Item* item) {
    if (item == nullptr) return;
    destroyItem(item);
}
void GameAdapter::freeAllItems() {
    while (!items_.empty()) destroyItem(items_.back().get());
    itemList_.clear();
}
// orig FUN_00207350 (work/decompiled/00207350.c): a linked task is flagged 0x30000000 (unlinked and deleted by the
// task walk FUN_0011d850 / FUN_0012bfd0); an unlinked one is left alone.
void GameAdapter::removeFromWorld(Item* item) {
    if (item == nullptr || (item->taskFlags & 1u) == 0) return;
    item->taskFlags |= 0x30000000u;
    if (itemEntityHooks() != nullptr) itemEntityHooks()->taskFlagsChanged(*item);
}
// orig FUN_001d3a30 (work/decompiled/001d3a30.c)
void GameAdapter::itemEnterWorld(Item& item) {
    if ((item.taskFlags & 1u) == 0 || (item.taskFlags & 0x10000000u) != 0) {
        // iRam0050b13c++; FUN_002073e0(item, 0x50b134, 0x50b138): appended unless still listed (0x10000000), then
        // flags & 0xcfffffff | 1
        item.taskFlags = (item.taskFlags & 0xcfffffffu) | 1u;
        if (itemEntityHooks() != nullptr) itemEntityHooks()->link(item);
    }
    if (!item.hasModel) {
        item.hasModel = true;                            // FUN_0012db80 holder +0x2c
        if (itemEntityHooks() != nullptr) itemEntityHooks()->loadModel(item);   // FUN_001099e0(0x464150[cls], 1, 0x44000001)
        if (item.cls == kClsFlesh) item.shadow = true;   // FUN_00109f70(holder, 1)
        ++modelCount_;                                   // iRam0053d8f0
        if (0x32 < modelCount_) dropFarthestItem();      // thunk_FUN_00128340(0x50b130)
    }
}
// orig FUN_001d3b30 (work/decompiled/001d3b30.c)
void GameAdapter::itemLeaveWorld(Item& item) {
    if ((item.taskFlags & 1u) != 0) {                    // FUN_00207320: 0x10000000 (unlinked by the next walk)
        item.taskFlags |= 0x10000000u;
        if (itemEntityHooks() != nullptr) itemEntityHooks()->taskFlagsChanged(item);
    }
    if (item.hasModel) {
        if (itemEntityHooks() != nullptr) itemEntityHooks()->releaseModel(item);   // FUN_0012d940
        item.hasModel = false;
        --modelCount_;
    }
}
// orig FUN_00128340 (work/decompiled/00128340.c): the linked, live item of 0x50a830 farthest from the player
// (squared distance, first maximum) leaves the list (FUN_00128230) and is removed (FUN_00207350).
void GameAdapter::dropFarthestItem() {
    if (characters().player() == nullptr) return;   // *(iRam0050b144 + 0x20) (the original has no test)
    const Vec3 p = player().position();
    Item* far = nullptr;
    float best = -1.0f;
    for (Item* it : itemList_) {
        if ((it->taskFlags & 1u) == 0 || (it->taskFlags & 0x10000000u) != 0) continue;
        const float dy = it->pos.y - p.y, dx = it->pos.x - p.x, dz = it->pos.z - p.z;
        const float d = dz * dz + dx * dx + dy * dy;
        if (best < d) {
            best = d;
            far = it;
        }
    }
    if (far == nullptr) return;
    itemList_.erase(std::remove(itemList_.begin(), itemList_.end(), far), itemList_.end());   // FUN_00128230
    removeFromWorld(far);                                                                       // FUN_00207350
}
// The item part of the task walks FUN_0011d850 / FUN_0012bfd0 (FUN_00207380 + FUN_00101200), see the header.
void GameAdapter::purgeItems() {
    if (itemEntityHooks() == nullptr) return;
    std::vector<const Item*> refs;
    itemEntityHooks()->referenced(refs);
    auto referenced = [&](const Item* it) { return std::find(refs.begin(), refs.end(), it) != refs.end(); };
    std::vector<Item*> snapshot;
    snapshot.reserve(items_.size());
    for (const auto& u : items_) snapshot.push_back(u.get());
    for (Item* it : snapshot) {
        if (std::none_of(items_.begin(), items_.end(), [&](const std::unique_ptr<Item>& u) { return u.get() == it; })) continue;
        if (referenced(it)) continue;
        const uint32_t f = it->taskFlags;
        if ((f & 0x20000000u) != 0 || ((f & 1u) != 0 && (f & 0x10000000u) == 0)) {
            destroyItem(it);                             // FUN_00207380 + FUN_00101200 (the record is gone)
        } else if ((f & 0x10000000u) != 0) {
            it->taskFlags = f & 0xeffffffeu;             // FUN_00207380: unlinked, the item lives on
        }
    }
    std::erase_if(graveyard_, [&](const std::unique_ptr<Item>& u) { return !referenced(u.get()); });
}
void GameAdapter::removeCharacter(CharacterAccess* c) {
    if (c == nullptr) return;
    const auto& list = characters().list();
    for (Chr* chr : list)
        if (&chrAdapter(chr) == c) chr->taskFlags |= 0x10000000u;
}
ItemTaskHooks*& itemTaskHooks() {
    static ItemTaskHooks* h = nullptr;
    return h;
}
// orig FUN_001e5610 (work/decompiled/001e5610.c): the trap object is initialised at creation (FUN_001e57d0 = trapInit:
// room / radius, burst SE and effects) and linked at the end of the task list; its first tick (FUN_001e5ca0) applies it
// and removes it. The game build installs ItemTaskHooks (game/objects/item_tasks.cpp): the object module owns that
// task record and applies the trap at its position in the character walk.
// Without hooks (Baroque-ItemsCheck only) the trap is kept here and applied by runPendingTraps at the end of the next
// engine tick in which the game was not paused.
void GameAdapter::spawnTrap(const Vec3& pos, int kind, uint32_t dmgMask, uint32_t statusMask, uint32_t flags,
                            int effectId, CharacterAccess* owner, uint32_t tag) {
    if (sys == nullptr) return;
    auto t = std::make_unique<TrapObject>();
    trapInit(*t, pos, kind, dmgMask, statusMask, flags, effectId, owner, tag, *sys);
    if (itemTaskHooks() != nullptr) {
        itemTaskHooks()->linkTrap(std::move(t));
        return;
    }
    pendingTraps_.push_back(std::move(t));
}
void GameAdapter::runPendingTraps(bool walkRan) {
    if (!walkRan || pendingTraps_.empty() || sys == nullptr) return;
    std::vector<std::unique_ptr<TrapObject>> run;
    run.swap(pendingTraps_);
    for (auto& t : run) trapApply(*t, *sys);   // FUN_001e5ca0, then FUN_00207350
}
// orig FUN_001e35f0: through the object module (ItemTaskHooks, game/objects/item_tasks.cpp). Without hooks (the
// items check) no field is created.
bool GameAdapter::spawnFieldObject(const Vec3& pos) {
    return itemTaskHooks() != nullptr && itemTaskHooks()->spawnFieldObject(pos);
}
// FUN_0012c1e0 / FUN_0012c1f0: the item list 0x50a830
int GameAdapter::floorItemCount() const { return static_cast<int>(itemList_.size()); }
Item* GameAdapter::floorItem(int i) const { return i >= 0 && i < static_cast<int>(itemList_.size()) ? itemList_[static_cast<size_t>(i)] : nullptr; }
namespace {
bool listedCharacter(const Chr* c) { return c->type <= kChrEnemy && c->item == nullptr && c->obj == nullptr; }
}  // namespace
// FUN_0012c1c0 / FUN_0012c1d0: list 0x50a7b0 (FUN_00128160 kind 0) = the characters of types 0..2, in list order
int GameAdapter::characterCount() const {
    int n = 0;
    for (const Chr* c : characters().list())
        if (listedCharacter(c)) ++n;
    return n;
}
CharacterAccess* GameAdapter::character(int i) const {
    int n = 0;
    for (Chr* c : characters().list()) {
        if (!listedCharacter(c)) continue;
        if (n++ == i) return &chrAdapter(c);
    }
    return nullptr;
}
CharacterAccess& GameAdapter::player() { return chrAdapter(characters().player()); }   // iRam0050b144
// orig FUN_0012c520 (work/decompiled/0012c520.c) over list 0x50a7b0 (FUN_001280d0: types 0..2 -> list 0, where the
// dead of types 0 / 2 are skipped). For each live (+0x10 bit 0x10000000 clear) actor of `type`: d = its position -
// from's, distance = |d| (FUN_00206ab0 normalises d) minus the collision radii of both when they are characters
// (FUN_0012c2e0, clamped at 0); the nearest one whose dir . d exceeds cosf(cone) wins (dir defaults to from's
// forward +0x30). A radius >= 0 smaller than the best distance gives 0.
CharacterAccess* GameAdapter::nearest(float radius, float cone, CharacterAccess* from, int type, const Vec3* dir,
                                      const Vec3* fromPos) {
    const float cosLimit = std::cos(cone);           // FUN_00238400 (cosf of the value as passed)
    const Vec3 fp = from != nullptr ? from->position() : (fromPos != nullptr ? *fromPos : Vec3{});
    const Vec3 fwd = from != nullptr ? from->forward() : Vec3{};
    const Vec3 dv = dir != nullptr ? *dir : fwd;
    const bool fromIsChr = from != nullptr && static_cast<unsigned>(from->actorType()) < 3u;
    CharacterAccess* best = nullptr;
    float bestD = -1.0f;
    for (Chr* c : characters().list()) {
        if (!listedCharacter(c) || c->type != type) continue;
        if ((c->type == 0 || c->type == 2) && c->status.block.hp == 0) continue;
        if ((c->taskFlags & 0x10000000u) != 0) continue;
        Vec3 d{c->xf.m.pos[0] - fp.x, c->xf.m.pos[1] - fp.y, c->xf.m.pos[2] - fp.z};
        const float len = std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z);
        if (len != 0.0f) { d.x /= len; d.y /= len; d.z /= len; }
        float dist = len - c->shape.radius;          // FUN_0012c2e0: the candidate is a character (type < 3)
        if (fromIsChr) dist = dist - from->radius();
        if (dist < 0.0f) dist = 0.0f;
        if ((bestD < 0.0f || dist < bestD) && !(dv.z * d.z + dv.x * d.x + dv.y * d.y <= cosLimit)) {
            bestD = dist;
            best = &chrAdapter(c);
        }
    }
    if (0.0f <= radius && radius < bestD) best = nullptr;
    return best;
}
// BX17 part of FUN_001dcdb0 (work/decompiled/001dcdb0.c): every linked live item of 0x50a830 except crystals / flesh
// becomes `id` (FUN_001d4860) with effect 0x6c; when any: SE 0x55, messages 0x297 and 0x2ef
void GameAdapter::transformAllFloorItems(uint32_t id) {
    if (sys == nullptr) return;
    int n = 0;
    for (Item* it : std::vector<Item*>(itemList_)) {
        if (std::find(itemList_.begin(), itemList_.end(), it) == itemList_.end()) continue;
        if ((it->taskFlags & 1u) == 0 || it->removed() || it->cls == kClsIS || it->cls == kClsFlesh) continue;
        effect(0x6c, it->pos);                           // FUN_00112190(0, 0x6c, pos, 0, (100, 100, 100))
        changeItemId(*it, id, *sys);
        ++n;
    }
    if (n != 0) {
        se(0x55);
        message(0x297);
        message(0x2ef);
    }
}
void GameAdapter::closeItemScreen() { if (screen != nullptr && sys != nullptr) screen->closeRequest(*sys); }   // FUN_001529a0(0x517b60)
// UNVERIFIED: player action states (factories 0x1d8cb0 / 0x1d8ba0 / 0x1d8e30) are not translated: no request.
// FUN_001db970 / FUN_001db900 / FUN_001db9e0 (Throw / Place / Give state requests, character.cpp)
void GameAdapter::playerActionRequest(const char* act4, int) { playerMenuActionRequest(act4); }
// talk manager 0x53c6e0 notifications (game/talk, TALK_NATIVE.md 5)
void GameAdapter::talkEventItemTaken(Item* it) { talkItemTaken(it); }                   // FUN_001a9cc0
void GameAdapter::talkEventStep(bool heads) { talkNoteCoinToss(heads ? 1 : 0); }        // FUN_001a9c60
void GameAdapter::talkEventEnemyHit() { talkNoteEnemyHit(); }                           // FUN_001a9c20
void GameAdapter::talkEventHandOver(Item* it, bool accepted) { talkItemHandOver(it, accepted); }   // FUN_001a6230
void GameAdapter::talkItemScreenClosed() { talkNoteItemScreen(); }                     // FUN_001a9ff0
void GameAdapter::talkDatabaseNote(uint32_t v) { talkNoteDatabase(v); }               // FUN_001a5330
// floor map 0x50a350 (hud_api.cpp), log 0x5180f0, sound manager 0x4e9790
bool GameAdapter::mapShown() const { return hudMapShown(); }   // iRam0050a3f8 != 0
void GameAdapter::mapShow() { hudMapShow(); }                    // FUN_00126cc0(0x50a350)
void GameAdapter::mapHide() { hudMapHide(); }                    // FUN_00126fb0(0x50a350)
void GameAdapter::logClear() { if (log != nullptr) log->clear(); }   // FUN_0015d0a0(0x5180f0)
void GameAdapter::bgmDuck(int on) { sound().setMenuDuck(on); }   // FUN_001161d0(0x4e9790, on)
void GameAdapter::hudRefreshBars(uint32_t which) { game::hudRefreshBars(which); }   // FUN_00151af0(0x5179c0, which)
void GameAdapter::hudLayoutBars(int flag) { game::hudLayoutBars(flag); }          // FUN_00151660(0x5179c0, flag)
void GameAdapter::stopAllSe() { sound().stopSe(0xfffe, 2); }                    // FUN_00115900 -> FUN_00115820(snd, 0xfffe, 2)
void GameAdapter::stopVoice() { linkVoiceStop(); }                              // FUN_00115c10(0x4e9790)
void GameAdapter::voiceResume() { linkVoicePause(false); }                      // FUN_00115c50(0x4e9790, 0)

}  // namespace baroque::game
