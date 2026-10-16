#pragma once
// Inventory object 0x53d910 (ITEMS_HUD_MENUS.md 2) and the Collector's bag 0x53dc00 (5.11).
#include "game/items/item.hpp"

namespace baroque::game {

struct Inventory {
    static constexpr int kMaxItems = 0x14;
    int count = 0;                       // +0x000
    Item* slots[kMaxItems] = {};         // +0x004
    int slotWord[kMaxItems] = {};        // +0x054 (per-slot tick counter, 0 on add)
    Item* equip[4] = {};                 // +0x0a4: 0 weapon, 1 second weapon, 2 coat, 3 wings
    Item* usingItem = nullptr;           // +0x0b4 (0x53d9c4)
    CharacterAccess* placeTarget = nullptr;  // +0x0b8 (0x53d9c8)
    Item* partB[kMaxItems] = {};         // +0x0bc
    ItemSaveRecord backup;               // +0x10c (last added item)
    Item* stash[4] = {};                 // 0x53dbd0..0x53dbdc equipment kept while the Game scene is left
    int stashDepth = 0;                  // 0x53dbe0

    void stashEquipment(ItemSystem& sys);                  // orig FUN_001d8180 (Game Exit)
    void restoreEquipment(ItemSystem& sys);                // orig FUN_001d8210 (Game Init)
    void stashReset() { stashDepth = 0; }                  // orig FUN_001d8280

    bool add(Item* item, ItemSystem& sys);                 // orig FUN_001d65c0
    Item* remove(int idx, ItemSystem& sys);                // orig FUN_001d6840 (nullptr when refused)
    int indexOf(const Item* item) const;                   // orig FUN_001d69a0
    bool equipItem(int idx, bool equip, ItemSystem& sys);  // orig FUN_001d6d60
    bool equipLegal(int idx, bool equip, ItemSystem& sys) const;   // orig FUN_001d77f0
    int slotForClass(int idx) const;                       // orig FUN_001d7c10 (-1 illegal)
    int equippedSlotOfIndex(int idx) const;                // orig FUN_001d7c80
    int equippedSlotOfItem(const Item* item) const;        // orig FUN_001d7c90
    bool unequipSlot(int slot, ItemSystem& sys);           // orig FUN_001d76a0
    bool unequipSlotRefresh(int slot, ItemSystem& sys);    // orig FUN_001d7750
    void unequipSideEffects(int slot, bool withFx, ItemSystem& sys);  // orig FUN_001d72c0
    void stripAll(ItemSystem& sys);                        // orig FUN_001d7240
    int atkTotal(ItemSystem& sys) const;                   // orig FUN_001d83d0
    int atkOfHand(bool second, ItemSystem& sys) const;     // orig FUN_001d84b0
    int defTotal(ItemSystem& sys) const;                   // orig FUN_001d85b0
    int hpMaxBonus(ItemSystem& sys) const;                 // orig FUN_001d8290
    int vtMaxBonus(ItemSystem& sys) const;                 // orig FUN_001d8330
    int wg03Total(ItemSystem& sys) const;                  // orig FUN_001d8720
    int weaponClass(const ItemSystem& sys) const;          // orig FUN_001d7d10 (0/1/2 sword kinds, 3 gun, -1 none)
    bool hasClass(int cls) const;                          // orig FUN_001d9290
    bool hasId(uint32_t id) const;                         // orig FUN_001d92d0
    int countId(uint32_t id) const;                        // orig FUN_001d9310
    void identifyAll(ItemSystem& sys);                     // orig FUN_001d9350
    int randomUnequipped(ItemSystem& sys) const;           // orig FUN_001d9450
    void bx24Tick();                                       // orig FUN_001d9560
    void rotClass(int cls);                                // orig FUN_001d9610
    void replaceItem(int idx, uint32_t newId, ItemSystem& sys);   // orig FUN_001d9710
    bool hungryChestSwap(int idx, ItemSystem& sys);        // orig FUN_001d97e0
    Item* swapSlot(int idx, Item* item);                   // orig FUN_001d9840
    void tick(ItemSystem& sys);                            // orig FUN_001d9b60 (per-tick inventory effects)
    void sortItems(ItemSystem& sys);                       // orig FUN_001d6bf0
    void refreshFlags(ItemSystem& sys);                    // orig FUN_001d5ef0
    void clear(ItemSystem& sys);                           // orig FUN_001d5e60
    void newGameReset(ItemSystem& sys);                    // orig FUN_001d98b0
    void freePartB(ItemSystem& sys);                       // orig FUN_001d9b00
    // save slot layout: 20 x 0x198 records, 20 slot words, 4 equipment indices, the 0x66-word backup
    void save(ItemSaveRecord* rec20, int* words20, int* equip4, uint32_t* backup66) const;   // orig FUN_001da6e0
    void load(const ItemSaveRecord* rec20, const int* words20, const int* equip4, const uint32_t* backup66, ItemSystem& sys);  // orig FUN_001da8d0
};

// orig FUN_001d69f0: list-sort comparator (equipped first by slot, then class, identified before unknown, index,
// ATK/DEF, fresh before rotten, stack count).
bool itemSortLess(const Item* a, const Item* b, const Inventory& inv);
// orig FUN_001d4e30: merge stack `other` into `item` (cap 99); when it fits the other is removed from the
// inventory (where == 0) or the bag (where == 1) and freed; returns true when merged completely.
bool mergeStack(Item& item, Item& other, int where, ItemSystem& sys);

// Collector's bag 0x53dc00: 50 item pointers, count at +0xc8.
struct CollectorBag {
    static constexpr int kSlots = 0x32;
    Item* items[kSlots] = {};            // +0x0
    int count = 0;                       // +0xc8
    int capacity(const GameAccess& game) const;            // orig FUN_001dacd0 (table 0x499650, default 5)
    bool isFull(const GameAccess& game) const;             // orig FUN_001dad50
    bool put(Item* item, bool force, ItemSystem& sys);     // orig FUN_001dad80
    Item* take(int idx, ItemSystem& sys);                  // orig FUN_001daeb0
    Item* get(int idx) const;                              // orig FUN_001daff0
    int indexOf(const Item* item) const;                   // orig FUN_001db320
    void sortItems(ItemSystem& sys);                       // orig FUN_001db1d0 (comparator 0x1db070: UNVERIFIED, the inventory one is used)
    void save(ItemSaveRecord* rec50) const;                // orig FUN_001db370
    void load(const ItemSaveRecord* rec50, ItemSystem& sys);  // orig FUN_001db3f0
    void clear(ItemSystem& sys);                           // orig FUN_001dac60
};

}  // namespace baroque::game
