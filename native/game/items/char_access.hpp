#pragma once
// Interfaces the item / inventory / HUD code uses to reach the rest of the game. The core module (player,
// enemies, floor, sound, effects) implements them; native/checks/items_check_main.cpp has test implementations.
// Every method documents the original routine it stands for (work/reports/ITEMS_HUD_MENUS.md, PLAYER.md).
#include <cstdint>

namespace baroque::game {

struct Item;

struct Vec3 {
    float x = 0, y = 0, z = 0;
};

// One argument of a localized "%s" / "%d" message (FUN_0015d480(0x5180f0, FUN_00114da0(id), ...)).
struct MsgArg {
    enum Kind : uint8_t { None, Int, Str };
    Kind kind = None;
    int i = 0;
    const char* s = nullptr;
    MsgArg() = default;
    MsgArg(int v) : kind(Int), i(v) {}           // NOLINT(google-explicit-constructor)
    MsgArg(const char* v) : kind(Str), s(v) {}   // NOLINT(google-explicit-constructor)
};

// A character (player, NPC or enemy) as the item code sees it: the StatusBlock of PLAYER.md 1.4 plus the few
// actor-level services the item handlers call. `player+0x2c->+8` is the status object, `->+0xc` the block.
struct CharacterAccess {
    virtual ~CharacterAccess() = default;
    virtual bool isPlayer() const = 0;             // == iRam0050b144
    virtual int actorType() const = 0;             // +0x18: 0 player, 1 NPC, 2 enemy
    virtual uint32_t charId() const = 0;           // +0x1c 'Cxxx' / 'Exxx' as the LE word
    virtual bool isRemoved() const = 0;            // +0x10 & 0x10000000
    virtual bool isAlive() const = 0;              // StatusBlock+4 != 0
    virtual Vec3 position() const = 0;             // xform +0xc
    virtual void setPosition(const Vec3& p) = 0;   // xform +0xc write (warps)
    virtual Vec3 forward() const = 0;              // xform +0x30
    virtual float height() const = 0;              // PRMCHAR +0xa0
    virtual float iconHeight() const = 0;          // PRMCHAR +0xac
    virtual float radius() const = 0;              // physics +0x5c->+4 (collision radius)
    virtual float bodyHeight() const = 0;          // physics +0x5c->+8 (pickup height test)
    virtual int roomIndex() const = 0;             // cell word (+0x30) & 0xf when it is a room cell, else -1
    virtual bool hasCell() const = 0;              // cell pointer +0x30 != null (FUN_00124c70 found a cell)
    // world position of a joint of the actor skeleton (vt+0x70 index lookup by name, joint matrix x node matrix +0x10
    // +0x20, FUN_0039d860 / FUN_0039d450 / FUN_0039d9a0); false when the actor has no such joint
    virtual bool jointPosition(const char* name, Vec3& out) const = 0;
    // the same joint's world matrix (16 floats, row-vector layout: rows right / up / forward / translation)
    virtual bool jointMatrix(const char* name, float out[16]) const = 0;

    // StatusBlock reads
    virtual int hp() const = 0;                    // +0x4
    virtual int hpMaxBase() const = 0;             // +0x0
    virtual int hpMaxTotal() const = 0;            // FUN_001343f0
    virtual int vt() const = 0;                    // +0xc
    virtual int vtMaxBase() const = 0;             // +0x8
    virtual int vtMaxTotal() const = 0;            // FUN_00134470
    virtual int atkMax() const = 0;                // +0x10
    virtual int atkCur() const = 0;                // +0x14
    virtual int defMax() const = 0;                // +0x1c
    virtual int defCur() const = 0;                // +0x20
    virtual uint32_t statusMask() const = 0;       // +0x3c
    virtual int level() const = 0;                 // +0x48
    virtual int exp() const = 0;                   // +0x4c
    virtual int expNext() const = 0;               // +0x50
    virtual uint32_t brand() const = 0;            // +0x54 ('STxx' marked on self, 0 = none)
    virtual int hiddenStat(int i) const = 0;       // +0x58[i]
    virtual int atkLevelBonus() const = 0;         // +0x18
    virtual int attackPower() const = 0;           // FUN_001344f0(status, -1)
    virtual bool refusesItems() const = 0;         // status object +0x10 & 1 (see ChrAdapter::refusesItems)
    virtual Item* parasite() const = 0;            // +0x5c
    virtual Item* heldItem() const = 0;            // +0x60

    // StatusBlock writes (PLAYER.md 11 "Status")
    virtual bool addHp(int d) = 0;                 // FUN_001349e0 (returns HP > 0)
    virtual void addVt(int d) = 0;                 // FUN_00134b60
    virtual void addHpMax(int d) = 0;              // FUN_00134b10
    virtual void addVtMax(int d) = 0;              // FUN_00134cb0
    virtual void addAtk(int d) = 0;                // FUN_00134d60 (current, clamped to max)
    virtual void addAtkMax(int d) = 0;             // FUN_00134db0
    virtual void addDef(int d) = 0;                // FUN_00134e20
    virtual void addDefMax(int d) = 0;             // FUN_00134e70
    virtual uint32_t inflict(uint32_t mask) = 0;   // FUN_00138660: nonzero when at least one bit was applied
    virtual uint32_t cure(uint32_t mask) = 0;      // FUN_00138970: the bits that were active and got cured
    virtual void cureAll() = 0;                    // FUN_00138ac0
    virtual void hpVtMaxShift() = 0;               // FUN_00134ee0 (the status-0x80 instant effect)
    virtual void levelUp() = 0;                    // FUN_00139060
    virtual void levelDown() = 0;                  // FUN_00139080
    virtual void addExp(int exp) = 0;              // FUN_00138e40
    virtual void setBrand(uint32_t id) = 0;        // FUN_001391d0 (id) / FUN_00139240 (0)
    virtual void setParasite(Item* p) = 0;         // FUN_00139280 (the caller deals with the replaced one)
    virtual bool canWarp() const = 0;              // FUN_00138ad0 (a valid room exists)
    virtual void recomputeMaxima() = 0;            // FUN_00134920
    virtual void refreshModelAttachments() = 0;    // FUN_001b1540
    virtual void gunEquipped(bool on, bool force = false) = 0;   // FUN_001b1f20 / FUN_001b2060(chr, force) (GN model)
    virtual void invincibleFlash() = 0;            // FUN_0013de20

    // combat / reactions
    // FUN_00136170(status, attacker, base, m1): (base + 2 * attacker level) scaled by the victim reduction / element
    virtual int projectileDamage(CharacterAccess* attacker, int base, uint32_t m1) = 0;
    virtual bool applyDamage(CharacterAccess* attacker, int dmg, uint32_t m3 = 0) = 0; // FUN_00136580(s, a, dmg, m3, 0, 0) -> died
    virtual void hitElementFx(uint32_t m1) = 0;                                        // FUN_0014c580(chr, 0, m1, 0)
    virtual Item* dropHeldItem() = 0;                                                  // FUN_00139540: +0x60 returned and cleared
    virtual void die() = 0;                                                            // FUN_0012a5e0
    virtual void react(uint32_t kind, const Vec3& from, bool reversed) = 0;            // FUN_0012a710
    virtual void returnToIdle() = 0;                                                   // FUN_0013d810
    virtual bool currentActionIs(const char* tag2) const = 0;                          // FUN_00143d30 name prefix

    // NPC hand-over (FUN_00139590 / FUN_001395c0)
    virtual bool isHoldingItem() const = 0;
    virtual bool takeItem(Item* item) = 0;
    virtual void sendAiEvent(uint32_t ev) = 0;     // FUN_001f1690(ai, ev, 1)
    virtual bool aiAccepts(uint32_t ev) const = 0; // FUN_001f1730(ai, ev) != 0
    virtual bool aiAlertable() const = 0;          // vt+0x20 FUN_0012d400: the actor is active (+0x24 bit 0)
    virtual bool aiDisabled() const = 0;           // AI component +0x14 bit 0 (FUN_001f1690 key 0x1000007)
};

// World / engine services (flag manager 0x53c3b0, counters 0x53c5c0, floor context 0x50a2a0/0x50a350, sound
// manager 0x4e9790, effect system FUN_00112190, actor factory 0x50b1a0, actor lists 0x50b130).
struct GameAccess {
    virtual ~GameAccess() = default;
    // flags and counters
    virtual bool testFlag(uint32_t id) const = 0;             // FUN_00178a80
    virtual void setFlag(uint32_t id) = 0;                    // FUN_00178890
    virtual void clearFlag(uint32_t id) = 0;                  // FUN_00178a30
    virtual void setFlagValue(uint32_t id, bool v) = 0;       // FUN_001787c0
    virtual void setFlagRaw(uint32_t id, bool v) = 0;         // FUN_00178800
    virtual void setGuardedFlag(uint32_t id) = 0;             // FUN_00179320
    virtual void setAchievementFlag(uint32_t id) = 0;         // FUN_001793b0
    virtual void addCounter(int id, int delta) = 0;           // FUN_00179570
    virtual int counter(int id) const = 0;                    // 0x53c5c0[id]
    // floor
    virtual bool isHubFloor() const = 0;                      // FUN_0011c2c0(0x50a2a0, floorId)
    virtual bool isDungeonType1() const = 0;                  // FUN_0011c290
    virtual bool isDungeonType3() const = 0;                  // FUN_0011c390
    virtual bool isWideGiveRange() const = 0;                 // FUN_0011c100 (Give radius 150 instead of 100)
    virtual int floorNumber() const = 0;                      // FUN_0011c3b0
    virtual int floorId() const = 0;                          // iRam0050a2a8
    virtual int floorSeedByte() const = 0;                    // *pcRam0050a620, -1 when the pointer is null
    virtual uint32_t floorFlags() const = 0;                  // uRam0050a610
    virtual void setFloorFlags(uint32_t mask) = 0;            // uRam0050a610 |= mask
    virtual int targetCellRoom() const = 0;                   // uRam0050a360
    virtual bool randomCellInRoom(int room, Vec3& out) = 0;   // FUN_00124620
    virtual int roomAt(const Vec3& pos) const = 0;            // FUN_00124c70 -> room index or -1
    virtual bool roomIsSealed(int room) const = 0;            // room data +4 -> byte 7 & 1
    virtual bool floorIsWaterish(const void* floorAttr) const = 0;  // FUN_00140050 on the GroundInfo +4 face
    // FUN_001d2f30: weighted item category from the FDT bytes 8..0x13 (rewritten in place by flags 0x398..0x39a),
    // -1 = none or no floor data (FUN_001d30b0 returns the FDT pointer uRam0050a620)
    virtual int floorItemCategory() = 0;
    // item database (FUN_001f8ad0 seen test, FUN_001f8ac0 entry test, FUN_001db000 first-collected flag + counter 0x10
    // + FUN_001a5330 talk notification)
    virtual bool itemSeen(uint32_t id) const = 0;
    virtual bool itemHasDatabaseEntry(const Item& item) const = 0;
    virtual bool markItemCollected(uint32_t id) = 0;
    // text, sound, effects
    virtual int language() const = 0;                         // FUN_00114c80 (0 EN, 1 JP)
    virtual void message(int strId, MsgArg a = {}, MsgArg b = {}, MsgArg c = {}) = 0;  // log printf of string id
    virtual void messageFmt(const char* fmt, MsgArg a = {}, MsgArg b = {}) = 0;        // log printf of a literal
    virtual const char* str(int strId) const = 0;             // FUN_00114da0
    virtual void se(int id) = 0;                              // FUN_00115390(1.0, 0x4e9790, id)
    virtual void se3d(int id, const Vec3& pos) = 0;           // FUN_00115470(1.0, 0x4e9790, id, pos)
    virtual void voice(int id) = 0;                           // FUN_00115970 (if nothing else is talking)
    // FUN_00112190(0, id, pos, rot (null = none), (scale, scale, scale, 1)): the instance id (0 = not created)
    virtual int effect(int id, const Vec3& pos, float scale = 100.0f, const float* rotDeg = nullptr, float tag = 0.0f) = 0;
    virtual void effectKill(int handle) { (void)handle; }                           // FUN_00112530
    virtual void effectEnd(int handle) { (void)handle; }                            // FUN_001126e0(h, 0, -1, -1)
    virtual void effectMove(int handle, const Vec3& pos) { (void)handle; (void)pos; }   // FUN_001122d0
    // FUN_00112190(0, id, 0, 0, 0) followed by FUN_00112770(instance, m): the effect placed by a full matrix
    // (m == nullptr: FUN_00112190 only)
    virtual int effectMatrix(int id, const float* m) = 0;
    // items and actors
    virtual Item* createItem(uint32_t id, bool addToWorld) = 0;    // FUN_0012ce70(0x50b1a0, id, addToWorld)
    virtual void freeItem(Item* item) = 0;                         // FUN_001b7570
    virtual void removeFromWorld(Item* item) = 0;                  // FUN_00207350 (linked: flags 0x30000000, freed by the task walk)
    virtual void itemEnterWorld(Item& item) = 0;                   // FUN_001d3a30 (task link + OBJ model, model cap 0x32)
    virtual void itemLeaveWorld(Item& item) = 0;                   // FUN_001d3b30 (task unlink + model release)
    virtual void removeCharacter(CharacterAccess* c) = 0;          // FUN_00207350 on a character
    virtual void spawnTrap(const Vec3& pos, int kind, uint32_t dmgMask, uint32_t statusMask, uint32_t flags,
                           int effectId, CharacterAccess* owner, uint32_t tag) = 0;   // FUN_001e5610
    virtual bool spawnFieldObject(const Vec3& pos) = 0;            // FUN_001e35f0 (BN18 bone field; false = not created)
    virtual int floorItemCount() const = 0;                        // FUN_0012c1e0(0x50b130): item list 0x50a830 count
    virtual Item* floorItem(int i) const = 0;                      // FUN_0012c1f0(0x50b130)[i]: every live item object
    virtual int characterCount() const = 0;                        // FUN_0012c1c0(0x50b130)
    virtual CharacterAccess* character(int i) const = 0;           // FUN_0012c1d0(0x50b130)[i]
    virtual CharacterAccess& player() = 0;                         // iRam0050b144
    // FUN_0012c520(radius, coneDeg, 0x50b130, from, type, dir): nearest actor of `type` (1 NPC, 2 enemy)
    // `cone` is passed to cosf as given (FUN_00238400; the callers pass 30.0 / 45.0 / -1.0); `fromPos` stands for the
    // transform of a non-character `from` (the BN02 item), whose radius is not subtracted (FUN_0012c2e0: type >= 3)
    virtual CharacterAccess* nearest(float radius, float cone, CharacterAccess* from, int type, const Vec3* dir,
                                     const Vec3* fromPos = nullptr) = 0;
    virtual void transformAllFloorItems(uint32_t id) = 0;          // BX17: every lying item -> id (effect 0x6c)
    virtual void closeItemScreen() = 0;                            // FUN_001529a0(0x517b60)
    virtual void playerActionRequest(const char* act4, int which) = 0;  // state factories 0x1d8cb0/0x1d8ba0/0x1d8e30
    virtual void talkEventItemTaken(Item* item) = 0;               // FUN_001a9cc0(0x53c6e0, item)
    virtual void talkEventStep(bool heads) = 0;                    // FUN_001a9c60
    virtual void talkEventEnemyHit() = 0;                          // FUN_001a9c20
    virtual void talkEventHandOver(Item* item, bool accepted) = 0; // FUN_001a6230
    // floor map of the floor context 0x50a350, message log 0x5180f0 and BGM duck, as the item screen open / close
    // routines FUN_001524b0 / FUN_001561f0 use them (defaults: no map, nothing to clear or duck)
    virtual bool mapShown() const { return false; }               // iRam0050a3f8 != 0 (map sprite ctx +0xa8)
    virtual void mapShow() {}                                      // FUN_00126cc0(0x50a350)
    virtual void mapHide() {}                                      // FUN_00126fb0(0x50a350)
    virtual void logClear() {}                                     // FUN_0015d0a0(0x5180f0)
    virtual void bgmDuck(int on) { (void)on; }                     // FUN_001161d0(0x4e9790, on)
    // status HUD 0x5179c0 and sound-manager calls of the item screen (FUN_00158890, FUN_00156de0, FUN_0015c1a0)
    virtual void hudRefreshBars(uint32_t which) { (void)which; }   // FUN_00151af0(0x5179c0, which)
    virtual void hudLayoutBars(int flag) { (void)flag; }          // FUN_00151660(0x5179c0, flag)
    virtual void stopAllSe() {}                                    // FUN_00115900(0x4e9790) = FUN_00115820(snd, 0xfffe, 2)
    virtual void stopVoice() {}                                    // FUN_00115c10(0x4e9790)
    virtual void voiceResume() {}                                  // FUN_00115c50(0x4e9790, 0)
    virtual void talkItemScreenClosed() {}                         // FUN_001a9ff0(0x53c6e0)
    virtual void talkDatabaseNote(uint32_t v) { (void)v; }         // FUN_001a5330(0x53c6e0, v) from FUN_001dad80
};

}  // namespace baroque::game
