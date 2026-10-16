#pragma once
// GameAccess over the port's game services: flag manager 0x53c3b0 / counters 0x53c5c0 (game/flag_manager.hpp),
// string table (game/ui/strings.hpp), sound manager 0x4e9790 (game/sound_stub.hpp), message log 0x5180f0
// (game/hud/message_log.hpp), the character list 0x50b130 (game/character/character.hpp via ChrAdapter) and the
// item objects created by the actor factory FUN_0012ce70 (item branch). World values owned by the Game scene /
// floor context are mirrored in ItemWorldState, which the Game-scene owner keeps current (hud_api.hpp).
#include "game/items/item_world.hpp"
#include <memory>
#include <vector>

namespace baroque::game {

class MessageLog;
class ItemScreen;

// The task-list side of the item objects. In the original an item IS a task (class 0x4cd960 on the base actor
// 0x4ccc70): linked into the character task list 0x50b134 while it lies / flies in the world, unlinked while it is in
// an inventory or a box. The port keeps the Item (owned by GameAdapter) and, while it is linked, a `Chr` record of
// the character task list with `Chr::item` pointing at it (game/objects/item_entity.cpp implements these hooks;
// unset in the items check, where items live without entities).
struct ItemEntityHooks {
    virtual ~ItemEntityHooks() = default;
    virtual Item* create(uint32_t id, bool link) = 0;   // FUN_0012ce70(0x50b1a0, id, link) item branch
    virtual void link(Item& it) = 0;                    // FUN_002073e0(item, 0x50b134, 0x50b138) (record made / revived)
    virtual void taskFlagsChanged(Item& it) = 0;        // item +0x10 written (FUN_00207320 / FUN_00207350): mirror it
    virtual void loadModel(Item& it) = 0;               // FUN_0012db80 + FUN_001099e0(path 0x464150[cls], 1, 0x44000001)
    virtual void releaseModel(Item& it) = 0;            // FUN_0012d940
    virtual void destroyed(Item& it) = 0;               // item destructor: the record stops ticking / drawing
    // items referenced by a record of the task list (those must stay allocated); also mirrors the records' remove
    // bits (0x10000000 / 0x20000000) written outside the item code into the items
    virtual void referenced(std::vector<const Item*>& out) = 0;
    virtual bool damageWall(const void* face) const = 0;   // FUN_00140050
};
// The installed hooks (a function-local static: item_entity.cpp installs them during static initialisation).
ItemEntityHooks*& itemEntityHooks();

// Type-5 task objects the item code creates (installed by the object module once it implements them):
// * trap FUN_001e5610: FUN_001e7630 alloc, FUN_00128690(obj, 5, 0), FUN_001e57d0 init (= trapInit, already run by
//   GameAdapter::spawnTrap on the TrapObject handed over), link FUN_002073e0(obj, 0x50b134, 0x50b138); its tick
//   FUN_001e5ca0 = trapApply(t, itemSystem()) followed by FUN_00207350 (removed after one tick).
// * bone field FUN_001e35f0 (BN18): FUN_001e4400 (FUN_00132b50 + FUN_001e3780, vtables 0x4cef40 / 0x4cef50),
//   FUN_00128690(obj, 5, 0), FUN_001e3900 (effect FUN_0012adb0(0x91) -> +0x24, position, SE 0x74 at pos), link; at
//   20+ fields (uRam0053dcd8 > 0x13) the one of 0x53dce0[30] farthest from the player is removed (FUN_00207350).
struct ItemTaskHooks {
    virtual ~ItemTaskHooks() = default;
    virtual bool linkTrap(std::unique_ptr<TrapObject> t) = 0;   // false = not created (FUN_001e7630 returned 0)
    virtual bool spawnFieldObject(const Vec3& pos) = 0;         // FUN_001e35f0 (false = FUN_001e4400 returned 0)
};
ItemTaskHooks*& itemTaskHooks();

struct ItemWorldState {
    int floorId = 0;              // Game scene +8 (iRam0050a2a8)
    uint32_t floorFlags = 0;      // uRam0050a610 (bit0 floor generated, bit3 new floor, 0x100000 target marker)
    // uRam0050a610 bits written outside the dungeon module (Dungeon::floorFlags() is read-only to the Game scene):
    // 0x100000 stairs room entered (FUN_0011edf0), 0x1000000 (FUN_001e0c40). floorFlags = (Dungeon flags &
    // ~clearedFloorFlags) | portFloorFlags at every sync; both reset by the Game scene when the floor flags are
    // zeroed (FUN_0011f960 load start, FUN_0011f6c0 unload).
    uint32_t portFloorFlags = 0;
    uint32_t clearedFloorFlags = 0;   // bits cleared outside the dungeon module (bit 8: FUN_0011edf0 every tick)
    int floorSeedByte = -1;       // *pcRam0050a620 (-1 = pointer null)
    int targetCellRoom = -1;      // uRam0050a360
    bool eventRunning = false;    // iRam0051b620 != 0
    bool dialogOpen = false;      // iRam0053bf88 != 0
    bool talkActive = false;      // FUN_001a2950(0x53c6e0)
    bool talkBusy = false;        // iRam0053c6a8 != 0
    bool talkChoice = false;      // iRam0053c6c8 == 1
    bool talkWindow = false;      // iRam0053c710 != 0
};

class GameAdapter final : public GameAccess {
public:
    ItemWorldState world;
    ItemSystem* sys = nullptr;
    MessageLog* log = nullptr;
    ItemScreen* screen = nullptr;

    bool testFlag(uint32_t id) const override;
    void setFlag(uint32_t id) override;
    void clearFlag(uint32_t id) override;
    void setFlagValue(uint32_t id, bool v) override;
    void setFlagRaw(uint32_t id, bool v) override;
    void setGuardedFlag(uint32_t id) override;
    void setAchievementFlag(uint32_t id) override;
    void addCounter(int id, int delta) override;
    int counter(int id) const override;
    bool isHubFloor() const override;
    bool isDungeonType1() const override;
    bool isDungeonType3() const override;
    bool isWideGiveRange() const override;
    int floorNumber() const override;
    int floorId() const override;
    int floorSeedByte() const override;
    uint32_t floorFlags() const override;
    void setFloorFlags(uint32_t mask) override;
    int targetCellRoom() const override;
    bool randomCellInRoom(int room, Vec3& out) override;
    int roomAt(const Vec3& pos) const override;
    bool roomIsSealed(int room) const override;
    bool floorIsWaterish(const void* floorAttr) const override;
    int floorItemCategory() override;
    bool itemSeen(uint32_t id) const override;
    bool itemHasDatabaseEntry(const Item& item) const override;
    bool markItemCollected(uint32_t id) override;
    int language() const override;
    void message(int strId, MsgArg a = {}, MsgArg b = {}, MsgArg c = {}) override;
    void messageFmt(const char* fmt, MsgArg a = {}, MsgArg b = {}) override;
    const char* str(int strId) const override;
    void se(int id) override;
    void se3d(int id, const Vec3& pos) override;
    void voice(int id) override;
    int effect(int id, const Vec3& pos, float scale = 100.0f, const float* rotDeg = nullptr, float tag = 0.0f) override;
    void effectKill(int handle) override;
    void effectEnd(int handle) override;
    void effectMove(int handle, const Vec3& pos) override;
    int effectMatrix(int id, const float* m) override;
    Item* createItem(uint32_t id, bool addToWorld) override;
    void freeItem(Item* item) override;
    void removeFromWorld(Item* item) override;
    void itemEnterWorld(Item& item) override;
    void itemLeaveWorld(Item& item) override;
    void removeCharacter(CharacterAccess* c) override;
    void spawnTrap(const Vec3& pos, int kind, uint32_t dmgMask, uint32_t statusMask, uint32_t flags, int effectId,
                   CharacterAccess* owner, uint32_t tag) override;
    bool spawnFieldObject(const Vec3& pos) override;
    int floorItemCount() const override;
    Item* floorItem(int i) const override;
    int characterCount() const override;
    CharacterAccess* character(int i) const override;
    CharacterAccess& player() override;
    CharacterAccess* nearest(float radius, float cone, CharacterAccess* from, int type, const Vec3* dir,
                             const Vec3* fromPos = nullptr) override;
    void transformAllFloorItems(uint32_t id) override;
    void closeItemScreen() override;
    void playerActionRequest(const char* act4, int which) override;
    void talkEventItemTaken(Item* item) override;
    void talkEventStep(bool heads) override;
    void talkEventEnemyHit() override;
    void talkEventHandOver(Item* item, bool accepted) override;
    bool mapShown() const override;
    void mapShow() override;
    void mapHide() override;
    void logClear() override;
    void bgmDuck(int on) override;
    void hudRefreshBars(uint32_t which) override;
    void hudLayoutBars(int flag) override;
    void stopAllSe() override;
    void stopVoice() override;
    void voiceResume() override;
    void talkItemScreenClosed() override;
    void talkDatabaseNote(uint32_t v) override;

    // item objects of the actor factory: GameAdapter owns every Item; `itemList_` is the item list 0x50a830 (every
    // live item object in creation order, FUN_00128160 / FUN_00128230), the task-list link is item +0x10 bit 0.
    int itemObjectCount() const { return static_cast<int>(items_.size()); }
    int modelCount() const { return modelCount_; }      // iRam0053d8f0
    Item* allocItem();                                  // FUN_0012e810 storage (FUN_001d3960 ctor defaults)
    void registerItem(Item* item);                      // FUN_00128160: append to 0x50a830
    void dropFarthestItem();                            // orig FUN_00128340
    // The task walk's part for items (FUN_00207380 unlink, FUN_00101200 delete): items flagged 0x20000000 (or whose
    // record vanished while linked, FUN_0012bfd0 floor exit) are destroyed, unlinked ones lose bits 0 / 0x10000000;
    // freed storage is released once no record references it. Called once per engine tick (hudGameTick).
    void purgeItems();
    void freeAllItems();
    // Traps created while no ItemTaskHooks are installed, applied by runPendingTraps (see game_adapter.cpp).
    void runPendingTraps(bool walkRan);
private:
    std::vector<std::unique_ptr<TrapObject>> pendingTraps_;
    void destroyItem(Item* item);                       // FUN_001d39b0 (dtor) on an owned item
    std::vector<std::unique_ptr<Item>> items_;
    std::vector<std::unique_ptr<Item>> graveyard_;      // destroyed, released when unreferenced
    std::vector<Item*> itemList_;                       // 0x50a830
    int modelCount_ = 0;                                // iRam0053d8f0
};

}  // namespace baroque::game
