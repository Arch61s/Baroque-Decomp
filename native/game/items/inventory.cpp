#include "game/items/inventory.hpp"
#include "game/items/item_effects.hpp"
#include <algorithm>
#include <cstring>

namespace baroque::game {

namespace {
constexpr uint32_t kIdST10 = itemId("ST10");
constexpr uint32_t kIdST04 = itemId("ST04");
constexpr uint32_t kIdST06 = itemId("ST06");
constexpr uint32_t kIdST09 = itemId("ST09");
constexpr uint32_t kIdST20 = itemId("ST20");
constexpr uint32_t kIdST08 = itemId("ST08");
constexpr uint32_t kIdSD24 = itemId("SD24");
constexpr uint32_t kIdSD25 = itemId("SD25");
constexpr uint32_t kIdCT13 = itemId("CT13");
constexpr uint32_t kIdCT14 = itemId("CT14");
constexpr uint32_t kIdCT15 = itemId("CT15");
constexpr uint32_t kIdCT17 = itemId("CT17");
constexpr uint32_t kIdWG11 = itemId("WG11");
constexpr uint32_t kIdWG12 = itemId("WG12");
constexpr uint32_t kIdBX20 = itemId("BX20");
constexpr uint32_t kIdBX21 = itemId("BX21");
constexpr uint32_t kIdBX22 = itemId("BX22");
constexpr uint32_t kIdBX24 = itemId("BX24");
constexpr uint32_t kIdWM00 = itemId("WM00");
constexpr uint32_t kIdGN00 = itemId("GN00");
constexpr uint32_t kIdHT02 = itemId("HT02");
constexpr uint32_t kIdMT03 = itemId("MT03");

// 0x499590: per-class "collected >= 15" flags (WG: >= 10); 0 = none [V words]
const uint32_t kCollectedFlags[kItemClassCountAll] = {0x336, 0x337, 0x338, 0, 0, 0x335, 0x33a, 0x334, 0x339, 0x333, 0x33b, 0x332, 0, 0x331, 0};
// 0x499610: FUN_001d7240 strip order [V words]
const int kStripOrder[4] = {0, 0, 2, 3};
// 0x499650: {flag, capacity} pairs for the Collector's bag [V words]
const uint32_t kBagCapacity[8][2] = {{0x2e0, 50}, {0x2df, 40}, {0x2de, 35}, {0x2dd, 30}, {0x2dc, 25}, {0x2db, 20}, {0x2da, 15}, {0x2d9, 10}};

bool cursedEquip(const Item* it) { return it->brand == kIdST10 || (it->flags & kItemCursedSealed) != 0; }
bool swordIsPaired(const Item* it, const ItemSystem& sys) { return it->cls == kClsSD && sys.word(*it, 0x9c) == 2; }  // PRM SD +0x9c == 2
}  // namespace

// orig FUN_001d65c0 (work/decompiled/001d65c0.c)
bool Inventory::add(Item* item, ItemSystem& sys) {
    if (count == kMaxItems) return false;
    slots[count] = item;
    slotWord[count] = 0;
    count++;
    item->flags &= ~(kItemInFlight | kItemThrownBack);
    GameAccess& g = *sys.game;
    if (item->cls == kClsGN) g.player().gunEquipped(true);           // FUN_001b1f20
    if (g.player().brand() == kIdST04) item->flags |= kItemIdentified;
    writeItemRecord(item, backup);
    static const uint32_t kClassFlag[14] = {0x2a8, 0x2ab, 0x2ad, 0x2af, 0x2b1, 0x2b3, 0x2b5, 0x2b7, 0x2b9, 0x2bb, 0x2bd, 0x2bf, 0, 0x2c2};
    if (item->cls >= 0 && item->cls < 14 && kClassFlag[item->cls]) g.setFlag(kClassFlag[item->cls]);
    refreshFlags(sys);
    return true;
}

// orig FUN_001d6840 (work/decompiled/001d6840.c)
Item* Inventory::remove(int idx, ItemSystem& sys) {
    if (idx < 0 || idx >= count) return nullptr;
    Item* item = slots[idx];
    if (equippedSlotOfIndex(idx) != -1) {
        if (!equipItem(idx, false, sys)) return nullptr;
        if (indexOf(item) < 0) return nullptr;   // FUN_001d69a0 < 0 (shattered); the shift keeps the caller's idx
    }
    GameAccess& g = *sys.game;
    if (item->cls == kClsGN) g.player().gunEquipped(false);         // FUN_001b2060
    if (item->id == kIdBX24) {
        item->count = 0;
        g.addCounter(5, -g.counter(5));                              // uRam0053c5d4 = 0
    }
    count--;
    for (int i = idx; i < count; ++i) slots[i] = slots[i + 1];      // slot words are not shifted (original)
    slots[count] = nullptr;
    refreshFlags(sys);
    return item;
}

// orig FUN_001d69a0 (work/decompiled/001d69a0.c)
int Inventory::indexOf(const Item* item) const {
    if (!item) return -1;
    for (int i = 0; i < count; ++i) if (slots[i] == item) return i;
    return -1;
}

// orig FUN_001d7c10 (work/decompiled/001d7c10.c)
int Inventory::slotForClass(int idx) const {
    const int cls = slots[idx]->cls;
    if (cls == kClsWG) return 3;
    if (cls == kClsCT) return 2;
    if (cls == kClsGN || cls == kClsSD) return 0;
    return -1;
}

// orig FUN_001d7c80 / FUN_001d7c90 (work/decompiled/001d7c80.c, 001d7c90.c)
int Inventory::equippedSlotOfIndex(int idx) const { return (idx < 0 || idx >= kMaxItems) ? -1 : equippedSlotOfItem(slots[idx]); }
int Inventory::equippedSlotOfItem(const Item* item) const {
    for (int i = 0; i < 4; ++i) if (equip[i] == item) return i;
    return -1;
}

// orig FUN_001d7d10 (work/decompiled/001d7d10.c): weapon slot class; SD rows by PRM +0x9c (2 = paired sword: 0 when
// no second weapon is held, else 2), any other class (GN) = 3
int Inventory::weaponClass(const ItemSystem& sys) const {
    const Item* w = equip[0];
    if (w == nullptr) return -1;
    if (w->cls != 0) return 3;
    const int32_t k = sys.word(0, w->idx, 0x9c);   // *(*(iRam0053d860 + 0x44) + idx*0xa0 + 0x9c)
    if (k == 2) return equip[1] == nullptr ? 0 : 2;
    if (k == 1) return 1;
    if (k == 0) return 0;
    return -1;
}

// orig FUN_001d77f0 (work/decompiled/001d77f0.c)
bool Inventory::equipLegal(int idx, bool wantEquip, ItemSystem& sys) const {
    const Item* item = slots[idx];
    if (wantEquip) {
        if (item->cls == kClsGN && sys.game->testFlag(0x1e)) return false;
        const int slot = slotForClass(idx);
        if (slot < 0) return false;
        const Item* cur = equip[slot];
        if (!cur) return true;
        if (slot == 0) {
            if (swordIsPaired(item, sys)) {
                if (item->idx > 0x1c) {                 // two-handed
                    if (cursedEquip(cur)) return false;
                    const Item* e1 = equip[1];
                    if (!e1) return true;
                    return !cursedEquip(e1);
                }
                if (item->id == cur->id) {              // dual: same sword
                    const Item* e1 = equip[1];
                    if (!e1) return true;
                    if (!cursedEquip(e1)) return true;
                    if (!cursedEquip(cur)) return true;
                }
            }
            if (cursedEquip(cur)) return false;
            const Item* e1 = equip[1];
            if (!e1) return true;
            if (cursedEquip(e1)) return false;
        } else if (cursedEquip(cur)) {
            return false;
        }
    } else {
        if (equippedSlotOfIndex(idx) == -1) return false;
        if (cursedEquip(item)) return false;
    }
    return true;
}

// orig FUN_001d6d60 (work/decompiled/001d6d60.c)
bool Inventory::equipItem(int idx, bool wantEquip, ItemSystem& sys) {
    if (!equipLegal(idx, wantEquip, sys)) return false;
    GameAccess& g = *sys.game;
    Item* item = slots[idx];
    if (wantEquip) {
        if (!item->identified()) {
            item->flags |= kItemIdentified;
            g.addCounter(0xc, 1);
        }
        if (item->cls == kClsWG) g.setFlag(0x2ae);
        else if (item->cls == kClsCT) { g.setFlag(0x2ac); g.setFlag(0x313); }
        else if (item->cls == kClsSD) g.setFlag(0x2a9);
        const int slot = slotForClass(idx);
        if (equip[slot] == nullptr || slot != 0) {
            unequipSideEffects(slot, true, sys);
            equip[slot] = item;
            if (item->cls == kClsSD && item->idx > 0x1c && sys.word(*item, 0x9c) == 2) equip[1] = item;
        } else if (!swordIsPaired(item, sys)) {
            unequipSideEffects(0, true, sys);
            equip[0] = item;
            equip[1] = nullptr;
        } else if (item->idx < 0x1d) {                  // dual swords
            Item* cur = equip[0];
            if (item->id == cur->id) {
                Item* e1 = equip[1];
                if (!e1) equip[1] = item;
                else if (cursedEquip(e1)) { if (!cursedEquip(cur)) equip[0] = item; }
                else equip[1] = item;
            } else {
                unequipSideEffects(0, true, sys);
                equip[0] = item;
                equip[1] = nullptr;
            }
        } else {                                        // two-handed
            unequipSideEffects(0, true, sys);
            equip[0] = item;
            equip[1] = item;
        }
    } else {
        const int s = equippedSlotOfIndex(idx);
        unequipSideEffects(s, true, sys);
        unequipSlot(s, sys);
        if (equip[2] == nullptr) g.clearFlag(0x313);
    }
    g.player().recomputeMaxima();            // FUN_00134920
    g.player().refreshModelAttachments();    // FUN_001b1540
    g.clearFlag(0x2d0); g.clearFlag(0x2d2); g.clearFlag(0x2d1);
    if (equip[0]) g.setFlag(equip[0]->cls == kClsSD ? 0x2d0 : 0x2d2);
    if (equip[3]) g.setFlag(0x2d1);
    if (equip[0] && equip[0]->id == kIdSD25 && equip[2] && equip[2]->id == kIdCT17 && equip[3] && equip[3]->id == kIdWG12) g.setFlag(0x330);
    return true;
}

// orig FUN_001d76a0 (work/decompiled/001d76a0.c)
bool Inventory::unequipSlot(int slot, ItemSystem& sys) {
    if (slot == -1) return false;
    if (slot != 0) { equip[slot] = nullptr; return true; }
    Item* e0 = equip[0];
    if (e0) {
        if (e0->cls != kClsSD) { equip[0] = equip[1]; equip[1] = nullptr; return true; }
        if (e0->idx > 0x1c && sys.word(*e0, 0x9c) == 2) { equip[0] = nullptr; equip[1] = nullptr; return true; }
    }
    equip[0] = equip[1];
    equip[1] = nullptr;
    return true;
}

// orig FUN_001d7750 (work/decompiled/001d7750.c)
bool Inventory::unequipSlotRefresh(int slot, ItemSystem& sys) {
    if (!unequipSlot(slot, sys)) return false;
    CharacterAccess& p = sys.game->player();
    p.recomputeMaxima();
    p.refreshModelAttachments();
    if (p.currentActionIs("AT")) p.returnToIdle();   // 0x464348 "AT": an attack in progress is cancelled
    return true;
}

// orig FUN_001d72c0 (work/decompiled/001d72c0.c)
void Inventory::unequipSideEffects(int slot, bool withFx, ItemSystem& sys) {
    if (slot < 0 || slot > 3) return;
    Item* it = equip[slot];
    if (!it) return;
    GameAccess& g = *sys.game;
    if (it->id == kIdSD25 || it->id == kIdCT17 || it->id == kIdWG12) {
        if (withFx) {
            // effect 0x67 at joint "Right_WEAPON00_Attach" (SD25, 0x464300) / "Center_ImWing_Attach" (WG12, 0x464320)
            // [V], else (CT17) at the player's position + PRMCHAR +0xac * 0.5
            CharacterAccess& p = g.player();
            Vec3 pos = p.position();
            const char* joint = it->id == kIdSD25 ? "Right_WEAPON00_Attach" : it->id == kIdWG12 ? "Center_ImWing_Attach" : nullptr;
            if (joint == nullptr || !p.jointPosition(joint, pos)) {
                pos = p.position();
                pos.y = p.iconHeight() * 0.5f + pos.y + 0.0f;
            }
            const float rot0[3] = {0.0f, 0.0f, 0.0f};          // rotation (0, 0, 0, 1), scale (100, 100, 100, 1)
            g.effect(0x67, pos, 100.0f, rot0);
            g.se(0x5a);
            g.message(0x250, itemDisplayName(*it, sys));          // "%s is shattered."
            g.closeItemScreen();                                   // FUN_001529a0(0x517b60)
        }
        equip[slot] = nullptr;
        remove(indexOf(it), sys);
        g.freeItem(it);
    } else if (it->id == kIdSD24) {
        it->atkMod = 0;
    } else if (it->id == kIdCT14) {
        it->defMod = 0;
    }
}

// orig FUN_001d7240 (work/decompiled/001d7240.c)
void Inventory::stripAll(ItemSystem& sys) {
    for (int i = 0; i < 4; ++i) {
        const int s = kStripOrder[i];
        if (equip[s]) equipItem(indexOf(equip[s]), false, sys);
    }
}

namespace {
int parasiteBonus(const CharacterAccess& p, int kindIdx) {
    int sum = 0;
    const Item* w = p.parasite();
    if (w) {
        if (w->idx == kindIdx) { sum += w->count; }
        const Item* w2 = w->parasite;
        if (w2 && w2->idx == kindIdx) sum += w2->count;
    }
    return sum;
}
}  // namespace

// orig FUN_001d83d0 (work/decompiled/001d83d0.c): max(hand0, hand1) + coat + wings + Attack parasite (WM06)
int Inventory::atkTotal(ItemSystem& sys) const {
    int t = equip[0] ? equip[0]->atkBase + equip[0]->atkMod : 0;
    if (equip[1]) { const int s = equip[1]->atkBase + equip[1]->atkMod; if (t < s) t = s; }
    for (int i = 2; i < 4; ++i) if (equip[i]) t += equip[i]->atkBase + equip[i]->atkMod;
    return t + parasiteBonus(sys.game->player(), 6);
}

// orig FUN_001d84b0 (work/decompiled/001d84b0.c)
int Inventory::atkOfHand(bool second, ItemSystem& sys) const {
    int t = 0;
    if (equip[0] && equip[1]) { const Item* h = equip[second ? 1 : 0]; t = h->atkBase + h->atkMod; }
    else if (equip[0]) t = equip[0]->atkBase + equip[0]->atkMod;
    for (int i = 2; i < 4; ++i) if (equip[i]) t += equip[i]->atkBase + equip[i]->atkMod;
    return t + parasiteBonus(sys.game->player(), 6);
}

// orig FUN_001d85b0 (work/decompiled/001d85b0.c): coat CT15 doubled with wings WG11; Defence parasite WM07
int Inventory::defTotal(ItemSystem& sys) const {
    int t = 0;
    for (int i = 0; i < 4; ++i) {
        const Item* e = equip[i];
        if (!e) continue;
        if (i == 2 && e->id == kIdCT15 && equip[3] && equip[3]->id == kIdWG11) t += (e->defBase + e->defMod) * 2;
        else t += e->defBase + e->defMod;
    }
    return t + parasiteBonus(sys.game->player(), 7);
}

// orig FUN_001d8290 / FUN_001d8330 / FUN_001d8720 (work/decompiled/001d8290.c, 001d8330.c, 001d8720.c)
int Inventory::hpMaxBonus(ItemSystem& sys) const {
    int t = 0;
    for (int i = 0; i < 4; ++i) if (equip[i]) t += equip[i]->hpMaxBonus;
    return t + parasiteBonus(sys.game->player(), 4);
}
int Inventory::vtMaxBonus(ItemSystem& sys) const {
    int t = 0;
    for (int i = 0; i < 4; ++i) if (equip[i]) t += equip[i]->vtMaxBonus;
    return t + parasiteBonus(sys.game->player(), 5);
}
int Inventory::wg03Total(ItemSystem& sys) const {
    int t = 0;
    for (int i = 0; i < 4; ++i) if (equip[i]) { t += equip[i]->wg03Bonus; if (equip[i]->id == kIdSD25) t += 99; }
    return t + parasiteBonus(sys.game->player(), 8);
}

// orig FUN_001d8180 (work/decompiled/001d8180.c)
void Inventory::stashEquipment(ItemSystem& sys) {
    if (stashDepth == 0) {
        for (int i = 0; i < 4; ++i) {
            stash[i] = equip[i];
            equip[i] = nullptr;
        }
        sys.game->player().refreshModelAttachments();   // FUN_001b1540
    }
    stashDepth = stashDepth + 1;
}

// orig FUN_001d8210 (work/decompiled/001d8210.c)
void Inventory::restoreEquipment(ItemSystem& sys) {
    if (stashDepth != 0 && (stashDepth = stashDepth + -1, stashDepth == 0)) {
        for (int i = 0; i < 4; ++i) equip[i] = stash[i];
        sys.game->player().refreshModelAttachments();   // FUN_001b1540
    }
}

// orig FUN_001d9290 / FUN_001d92d0 / FUN_001d9310
bool Inventory::hasClass(int cls) const { for (int i = 0; i < count; ++i) if (slots[i]->cls == cls) return true; return false; }
bool Inventory::hasId(uint32_t id) const { for (int i = 0; i < count; ++i) if (slots[i]->id == id) return true; return false; }
int Inventory::countId(uint32_t id) const { int n = 0; for (int i = 0; i < count; ++i) if (slots[i]->id == id) n++; return n; }

// orig FUN_001d9350 (work/decompiled/001d9350.c)
void Inventory::identifyAll(ItemSystem& sys) {
    bool any = false;
    for (int i = 0; i < count; ++i) {
        Item* it = slots[i];
        if (it->identified()) continue;
        it->flags |= kItemIdentified;
        sys.game->message(0x256, sys.name(it->cls, it->idx));   // "Identified %s."
        sys.game->addCounter(0xc, 1);
        any = true;
    }
    if (!any) sys.game->message(0x2e4);                          // "No unidentified items."
}

// orig FUN_001d9450 (work/decompiled/001d9450.c)
int Inventory::randomUnequipped(ItemSystem& sys) const {
    if (count == 0) return -1;
    int i = static_cast<int>(sys.rand(static_cast<uint32_t>(count - 1)));
    for (int n = count; n != 0; --n) {
        if (equippedSlotOfIndex(i) == -1) return i;
        if (!cursedEquip(slots[i])) return i;
        if (++i >= count) i = 0;
    }
    return -1;
}

// orig FUN_001d9560 (work/decompiled/001d9560.c)
void Inventory::bx24Tick() {
    for (int i = 0; i < count; ++i)
        if (slots[i]->id == kIdBX24) slots[i]->count = std::min(slots[i]->count + 1, 999);
}

// orig FUN_001d9610 (work/decompiled/001d9610.c): ids 0x48de70.. are "MT03" / "HT02" [V bytes]
void Inventory::rotClass(int cls) {
    for (int i = 0; i < count; ++i) {
        Item* it = slots[i];
        if (it->cls == cls && !(it->flags & kItemRotten) && !(it->flags & kItemSealed) && it->id != kIdHT02 && it->id != kIdMT03)
            it->flags |= kItemRotten;
    }
}

// orig FUN_001d9710 (work/decompiled/001d9710.c)
void Inventory::replaceItem(int idx, uint32_t newId, ItemSystem& sys) {
    unequipSlotRefresh(equippedSlotOfIndex(idx), sys);
    slotWord[idx] = 0;
    Item* it = slots[idx];
    if (it->id == kIdGN00) sys.game->player().gunEquipped(false);
    changeItemId(*it, newId, sys);
    refreshFlags(sys);
}

// orig FUN_001d97e0 (work/decompiled/001d97e0.c)
bool Inventory::hungryChestSwap(int idx, ItemSystem& sys) {
    Item* box = slots[idx];
    Item* meal = box->contained;
    if (!meal) return false;
    box->contained = nullptr;
    slots[idx] = meal;
    slotWord[idx] = 0;
    sys.game->freeItem(box);
    refreshFlags(sys);
    return true;
}

// orig FUN_001d9840 (work/decompiled/001d9840.c)
Item* Inventory::swapSlot(int idx, Item* item) { Item* old = slots[idx]; slots[idx] = item; return old; }

// orig FUN_001d9b60 (work/decompiled/001d9b60.c)
void Inventory::tick(ItemSystem& sys) {
    // FUN_00143d30(player act, -1) is called first; its result is unused
    GameAccess& g = *sys.game;
    CharacterAccess& p = g.player();
    const bool newFloor = (g.floorFlags() & 8) != 0;
    for (int s = 0; s < 4; ++s) {
        Item* e = equip[s];
        if (!e) continue;
        if (newFloor) {
            if (e->id == kIdSD25 || e->id == kIdCT17 || e->id == kIdWG12) {
                e->brand = 0;
                equipItem(indexOf(e), false, sys);
            } else if (e->id == kIdSD24) {
                if (g.floorNumber() != 0 && e->atkMod < 99) {
                    g.se(0x90);
                    addAtkMod(*e, 1);
                    g.message(0x252, itemDisplayName(*e, sys));      // "%s has evolved."
                }
            } else if (e->id == kIdCT14 && g.floorNumber() != 0 && e->defMod < 99) {
                g.se(0x90);
                addDefMod(*e, 1);
                g.message(0x252, itemDisplayName(*e, sys));
            }
        }
        if (e->id == kIdCT13 && g.nearest(100.0f, -1.0f, &p, 2, nullptr) != nullptr) {
            if (equipItem(indexOf(e), false, sys)) {
                g.message(0x25e, itemDisplayName(*e, sys));          // "Took off %s."
                g.se(0x58);
            }
        }
    }
    if (newFloor) {
        if (p.brand() == kIdST20) { p.inflict(0x10000); p.invincibleFlash(); }
        if (p.brand() == kIdST08 && count != kMaxItems) {
            uint32_t id = 0;
            if (count != 0) id = slots[sys.rand(static_cast<uint32_t>(count - 1))]->id;
            if (id != 0) {
                Item* dup = g.createItem(id, false);
                if (dup) add(dup, sys);
                p.invincibleFlash();
                g.se(0x51);
            }
        }
        if (hasId(kIdBX24)) g.addCounter(5, 1);   // FUN_00179570(0x53c5c0, 5, a2 = 1) [V disasm 0x1da164]
    }
    bool alarmDone = false, hasLarva = false;
    bool chestScanned = false;
    for (int i = 0; i < count && !chestScanned; ++i) {
        Item* it = slots[i];
        slotWord[i]++;
        if (it->id == kIdBX20 && static_cast<unsigned>(slotWord[i]) > 0x1e) {
            slotWord[i] = 0;
            if (it->contained == nullptr) {
                // joined_r0x001da2ac: whether or not something is eaten, the item loop ends here (goto LAB_001da580)
                chestScanned = true;
                int j = static_cast<int>(sys.rand(static_cast<uint32_t>(count - 1)));
                for (int n = count; n != 0; --n) {
                    if (j != i && equippedSlotOfIndex(j) == -1 && slots[j]->contained == nullptr) {
                        g.message(0x2b7, itemDisplayName(*slots[j], sys));   // "Hungry Chest ate %s."
                        g.se(99);
                        it->contained = remove(j, sys);
                        break;
                    }
                    if (++j >= count) j = 0;
                }
            }
        } else if (it->id == kIdBX21 && static_cast<unsigned>(slotWord[i]) > 0x96) {
            slotWord[i] = 0;
            if (!alarmDone) { alarmDone = true; alarmBoxEffect(sys); }
        } else if (it->id == kIdBX22 && static_cast<unsigned>(slotWord[i]) > 0x1c2) {
            slotWord[i] = 0;
            int j = static_cast<int>(sys.rand(static_cast<uint32_t>(count - 1)));
            for (int n = count; n != 0; --n) {
                if (j != i && slots[j]->id != kIdBX22) {
                    g.message(0x24e, itemDisplayName(*slots[j], sys));       // "%s became Parasite Box."
                    g.se(0x77);
                    replaceItem(j, it->id, sys);
                    break;
                }
                if (++j >= count) j = 0;
            }
        } else if (it->id == kIdWM00) {
            hasLarva = true;
        }
    }
    if (newFloor && hasLarva && sys.rand(99) < 0x21) {
        int j = static_cast<int>(sys.rand(static_cast<uint32_t>(count - 1)));
        for (int n = count; n != 0; --n) {
            Item* it = slots[j];
            if (it->id == kIdWM00) {
                replaceItem(j, static_cast<uint32_t>(it->count), sys);
                g.addCounter(0x17, 1);
                g.message(0x2f4, sys.name(it->cls, it->idx));           // "Larva is now %s."
                return;
            }
            if (++j >= count) j = 0;
        }
    }
}

// orig FUN_001d69f0 (work/decompiled/001d69f0.c)
bool itemSortLess(const Item* a, const Item* b, const Inventory& inv) {
    const int sa = inv.equippedSlotOfItem(a), sb = inv.equippedSlotOfItem(b);
    if (sa != -1 && sb != -1) return sa < sb;
    if (sa != -1) return true;
    if (sb != -1) return false;
    if (a->cls != b->cls) return a->cls < b->cls;
    const bool ia = a->identified(), ib = b->identified();
    if (ia && !ib) return true;
    if (!ia && ib) return false;
    if (a->idx != b->idx) return a->idx < b->idx;
    if (a->cls == kClsSD) return b->atkBase + b->atkMod < a->atkBase + a->atkMod;
    if (a->cls == kClsCT) return b->defBase + b->defMod < a->defBase + a->defMod;
    const bool ra = (a->flags & kItemRotten) != 0, rb = (b->flags & kItemRotten) != 0;
    if (ra && !rb) return false;
    if (!ra && rb) return true;
    if (a->cls == kClsBN || a->cls == kClsGN) return b->count < a->count;
    return false;
}

// orig FUN_001d4e30 (work/decompiled/001d4e30.c)
bool mergeStack(Item& item, Item& other, int where, ItemSystem& sys) {
    const int sum = item.count + other.count;
    item.count = sum;
    if (sum < 100) {
        if (where == 0) sys.inventory->remove(sys.inventory->indexOf(&other), sys);
        else if (where == 1) sys.bag->take(sys.bag->indexOf(&other), sys);
        sys.game->freeItem(&other);
        return true;
    }
    other.count = sum - 99;
    item.count = 99;
    return false;
}

// orig FUN_001d6bf0 (work/decompiled/001d6bf0.c)
void Inventory::sortItems(ItemSystem& sys) {
    sys.game->setFlag(0x62);
    for (int i = 0; i < count; ++i) {
        Item* a = slots[i];
        if (a->cls != kClsBN || !a->identified()) continue;
        bool merged;
        do {
            merged = false;
            for (int j = i + 1; j < count; ++j) {
                Item* b = slots[j];
                if (b->cls == kClsBN && b->identified() && a->brand == b->brand && a->idx == b->idx) {
                    merged = mergeStack(*a, *b, 0, sys);
                    break;
                }
            }
        } while (merged);
    }
    std::sort(slots, slots + count, [this](const Item* a, const Item* b) { return itemSortLess(a, b, *this); });   // FUN_001db460
}

// orig FUN_001d5ef0 (work/decompiled/001d5ef0.c)
void Inventory::refreshFlags(ItemSystem& sys) {
    GameAccess& g = *sys.game;
    g.setFlagValue(0x2cb, hasClass(kClsSD));
    g.setFlagValue(0x312, hasClass(kClsCT));
    g.setFlagValue(0x314, hasClass(kClsWG));
    g.setFlagValue(0x2cc, hasClass(kClsHT));
    g.setFlagValue(0x2cf, hasClass(kClsWM));
    g.setFlagValue(0x2c6, hasClass(kClsIS));
    g.setFlagValue(0x2cd, hasId(itemId("HT00")));
    g.setFlagValue(0x2d3, hasId(itemId("LQ22")));
    g.setFlagValue(0x2d4, hasId(itemId("LQ16")));
    g.setFlagValue(0x2ce, hasId(kIdST06));
    g.setFlagValue(0x31e, hasId(itemId("BN22")));
    g.setFlagValue(0x2ca, hasId(kIdGN00));
    g.setFlagValue(0x2c7, hasId(itemId("IS00")));
    g.setFlagValue(0x2c8, hasId(itemId("IS01")));
    g.setFlagValue(0x2c9, hasId(itemId("IS04")));
    g.setFlagValue(0x2c4, count > 1);
    g.setFlagValue(0x2c5, count > 0x13);
    g.setFlagValue(0x315, count != 0);
    if (g.testFlag(0x1e)) {
        g.setFlagRaw(0x4ed, hasClass(kClsSD) || hasClass(kClsCT) || hasClass(kClsWG));
        const bool food = hasClass(kClsHT) || hasClass(kClsMT);
        g.setFlagRaw(0x4ee, food);
        g.setFlagRaw(0x4ef, food);
        g.setFlagRaw(0x4f0, count != 0);
    }
    if (count == kMaxItems) {
        bool same = true;
        for (int i = 1; i < kMaxItems; ++i) if (slots[0]->id != slots[i]->id) { same = false; break; }
        if (same) g.setFlagValue(0x32f, true);   // FUN_001787c0(0x53c3b0, 0x32f, a2 = 1) [V disasm 0x1d647c]
    }
    for (int cls = 0; cls < kItemClassCountAll; ++cls) {
        if (!kCollectedFlags[cls]) continue;
        int n = 0;
        const int rows = sys.rowCount(cls);
        for (int r = 0; r < rows; ++r) if (hasId(makeItemId(cls, r))) n++;
        g.setFlagValue(kCollectedFlags[cls], cls == kClsWG ? n > 9 : n > 0xe);
    }
}

// orig FUN_001d5e60 (work/decompiled/001d5e60.c)
void Inventory::clear(ItemSystem& sys) {
    for (int i = 0; i < 4; ++i) equip[i] = nullptr;
    while (count != 0) {
        Item* it = remove(0, sys);
        if (!it) break;
        sys.game->freeItem(it);
    }
    usingItem = nullptr;
    backup = ItemSaveRecord{};
    CharacterAccess& p = sys.game->player();
    p.recomputeMaxima();
    p.refreshModelAttachments();
}

// orig FUN_001d9b00 (work/decompiled/001d9b00.c): free the part-B list (+0xbc, 0x14 slots)
void Inventory::freePartB(ItemSystem& sys) {
    for (int i = 0; i < kMaxItems; ++i) {
        if (partB[i] != nullptr) {
            sys.game->freeItem(partB[i]);   // FUN_001b7570
            partB[i] = nullptr;
        }
    }
}

// orig FUN_001dac60 (work/decompiled/001dac60.c): free the 0x32 bag slots, count +0xc8 = 0
void CollectorBag::clear(ItemSystem& sys) {
    for (int i = 0; i < kSlots; ++i) {
        if (items[i] != nullptr) {
            sys.game->freeItem(items[i]);   // FUN_001b7570
            items[i] = nullptr;
        }
    }
    count = 0;
}

// orig FUN_001d98b0 (work/decompiled/001d98b0.c); FUN_001d9b00 frees the part-B list first
void Inventory::newGameReset(ItemSystem& sys) {
    for (int i = 0; i < kMaxItems; ++i) if (partB[i]) { sys.game->freeItem(partB[i]); partB[i] = nullptr; }
    for (int s = 0; s < 4; ++s) { unequipSideEffects(s, false, sys); equip[s] = nullptr; }
    int k = 0;
    while (count != 0) {
        Item* it = remove(0, sys);
        if (!it) break;
        if (it->brand == kIdST06) {               // items branded ST06 survive the new game as ST09-branded part B
            it->brand = kIdST09;
            if (k < kMaxItems) partB[k++] = it;
        } else {
            sys.game->freeItem(it);
        }
    }
}

// orig FUN_001da6e0 (work/decompiled/001da6e0.c)
void Inventory::save(ItemSaveRecord* rec20, int* words20, int* equip4, uint32_t* backup66) const {
    for (int i = 0; i < kMaxItems; ++i) writeItemRecord(slots[i], rec20[i]);
    for (int i = 0; i < kMaxItems; ++i) words20[i] = slotWord[i];
    for (int i = 0; i < 4; ++i) equip4[i] = indexOf(equip[i]);
    std::memcpy(backup66, backup.w, sizeof backup.w);
}

// orig FUN_001da8d0 (work/decompiled/001da8d0.c)
void Inventory::load(const ItemSaveRecord* rec20, const int* words20, const int* equip4, const uint32_t* backup66, ItemSystem& sys) {
    for (int i = 0; i < kMaxItems; ++i) {
        Item* it = readItemRecord(rec20[i], sys);
        if (it) add(it, sys);
    }
    for (int i = 0; i < kMaxItems; ++i) slotWord[i] = words20[i];
    for (int i = 0; i < 4; ++i) equip[i] = equip4[i] < 0 ? nullptr : slots[equip4[i]];
    std::memcpy(backup.w, backup66, sizeof backup.w);
    CharacterAccess& p = sys.game->player();
    p.recomputeMaxima();
    p.refreshModelAttachments();
}

// ---- Collector's bag ---------------------------------------------------------------------------

// orig FUN_001dacd0 (work/decompiled/001dacd0.c)
int CollectorBag::capacity(const GameAccess& game) const {
    for (const auto& e : kBagCapacity) if (game.testFlag(e[0])) return static_cast<int>(e[1]);
    return 5;
}
// orig FUN_001dad50 (work/decompiled/001dad50.c)
bool CollectorBag::isFull(const GameAccess& game) const { return count == capacity(game); }

// orig FUN_001dad80 (work/decompiled/001dad80.c); FUN_001db000 = first-time database flag + counter 0x10,
// FUN_001a5330 = talk-system notification of FUN_001db000's result (GameAccess::markItemCollected / talkDatabaseNote)
bool CollectorBag::put(Item* item, bool force, ItemSystem& sys) {
    GameAccess& g = *sys.game;
    if (!(force || g.testFlag(5)) || isFull(g)) return false;
    for (int i = 0; i < kSlots; ++i) {
        if (items[i]) continue;
        g.talkDatabaseNote(g.markItemCollected(item->id) ? 1u : 0u);   // FUN_001a5330(0x53c6e0, FUN_001db000(bag, item))
        items[i] = item;
        count++;
        g.setFlag(0x2d5);
        const int cap = capacity(g);
        if (count == cap - 1) g.setFlag(0x2d6);
        if (count == cap) g.setFlag(0x2d7);
        return true;
    }
    return false;
}

// orig FUN_001daeb0 (work/decompiled/001daeb0.c)
Item* CollectorBag::take(int idx, ItemSystem& sys) {
    if (idx < 0 || idx >= kSlots) return nullptr;
    Item* it = items[idx];
    items[idx] = nullptr;
    count--;
    for (int i = idx; i < count; ++i) items[i] = items[i + 1];
    if (count >= 0) items[count] = nullptr;
    GameAccess& g = *sys.game;
    if (count == 0) g.clearFlag(0x2d5);
    const int cap = capacity(g);
    if (count < cap - 1) g.clearFlag(0x2d6);
    if (count < cap) g.clearFlag(0x2d7);
    return it;
}

Item* CollectorBag::get(int idx) const { return (idx < 0 || idx >= kSlots) ? nullptr : items[idx]; }
int CollectorBag::indexOf(const Item* item) const { for (int i = 0; i < kSlots; ++i) if (items[i] == item) return i; return -1; }

// orig FUN_001db1d0 (work/decompiled/001db1d0.c)
void CollectorBag::sortItems(ItemSystem& sys) {
    for (int i = 0; i < count; ++i) {
        Item* a = items[i];
        if (!a || a->cls != kClsBN || !a->identified()) continue;
        bool merged;
        do {
            merged = false;
            for (int j = i + 1; j < count; ++j) {
                Item* b = items[j];
                if (b && b->cls == kClsBN && b->identified() && a->brand == b->brand && a->idx == b->idx) {
                    merged = mergeStack(*a, *b, 1, sys);
                    break;
                }
            }
        } while (merged);
    }
    std::sort(items, items + count, [&](const Item* a, const Item* b) { return itemSortLess(a, b, *sys.inventory); });
}

// orig FUN_001db370 / FUN_001db3f0 (work/decompiled/001db370.c, 001db3f0.c)
void CollectorBag::save(ItemSaveRecord* rec50) const { for (int i = 0; i < kSlots; ++i) writeItemRecord(items[i], rec50[i]); }
void CollectorBag::load(const ItemSaveRecord* rec50, ItemSystem& sys) {
    for (int i = 0; i < kSlots; ++i) {
        Item* it = readItemRecord(rec50[i], sys);
        if (it) put(it, true, sys);
    }
}

}  // namespace baroque::game
