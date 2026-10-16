#pragma once
// Item use / throw / step effects (ITEMS_HUD_MENUS.md 5): dispatcher FUN_001d8810, per-class handlers, throw-hit
// dispatch FUN_001d1340, torturer fields, traps. Every handler acts on the player through CharacterAccess and on
// the world through GameAccess; message ids and SE ids are the original ones.
#include "game/items/inventory.hpp"

namespace baroque::game {

// orig FUN_001d8810: use inventory item `idx` (targetIdx = second item for ST/WM/IS, -1 = self). false = refused.
bool useItem(ItemSystem& sys, int idx, int targetIdx);
// orig FUN_001d8950: equipped-cursed check + class gates (BX FUN_001dccb0, ST FUN_001dfa00, WM FUN_001e4510, IS FUN_001e52b0)
bool useLegal(ItemSystem& sys, int idx, int targetIdx);
bool boxUsable(ItemSystem& sys, int idx);                           // orig FUN_001dccb0
bool brandTargetOk(ItemSystem& sys, int idx, int targetIdx);        // orig FUN_001dfa00
bool parasiteTargetOk(ItemSystem& sys, int idx, int targetIdx);     // orig FUN_001e4510
bool crystalTargetOk(ItemSystem& sys, int idx, int targetIdx);      // orig FUN_001e52b0

void useHeart(ItemSystem& sys, int idx);                            // orig FUN_001dc250
void useFlesh(ItemSystem& sys, int idx);                            // orig FUN_001dc6e0
void useBox(ItemSystem& sys, int idx, bool recursed);               // orig FUN_001dcdb0
void useLiquid(ItemSystem& sys, int idx);                           // orig FUN_001ddc20
void useBrand(ItemSystem& sys, int idx, int targetIdx);             // orig FUN_001dfb20
void useTorturer(ItemSystem& sys, int idx);                         // orig FUN_001e16b0 (requests the player state 0x1e1710)
// orig FUN_001e1550 completion half: when the player action fires its 0x100000 trigger the core calls this.
void torturerUseComplete(ItemSystem& sys);
void useBone(ItemSystem& sys, int idx);                             // orig FUN_001e3980
void useParasite(ItemSystem& sys, int idx, int targetIdx);          // orig FUN_001e4800
void useCrystal(ItemSystem& sys, int idx, int targetIdx);           // orig FUN_001e5320
bool fuseCrystal(Item& sword, Item& crystal, ItemSystem& sys);      // orig FUN_001d4910
void mergeParasiteLevels(Item& parasite, Item& host, int cap, ItemSystem& sys);  // orig FUN_001e46f0

// Torturer (TL) effects
void torturerDeport(ItemSystem& sys, CharacterAccess* user, CharacterAccess* target, Item& item);  // orig FUN_001e1110
void torturerField(ItemSystem& sys, int tlIdx, CharacterAccess* user, CharacterAccess* target);    // orig FUN_001e0d40
// orig FUN_001e3110: direct effect of a disk (TP row of `item`) on the character standing on it / hit by it
bool diskEffect(ItemSystem& sys, Item& item, CharacterAccess& target);
void teleportRoomContents(ItemSystem& sys, CharacterAccess* user, CharacterAccess* target);        // orig FUN_001e0050
void killNearestEnemy(ItemSystem& sys, CharacterAccess* user, CharacterAccess* target);            // orig FUN_001e05b0
void healEveryone(ItemSystem& sys, CharacterAccess* user, CharacterAccess* target);                // orig FUN_001e08a0
void torturerSpiral(ItemSystem& sys, CharacterAccess* target);                                     // orig FUN_001e0c40

// Throw / step effects on a hit creature (orig FUN_001d1340 dispatch)
void throwEffect(ItemSystem& sys, Item& item, CharacterAccess& target, CharacterAccess* thrower);
void fleshThrowEffect(ItemSystem& sys, Item& item, CharacterAccess& target);                       // orig FUN_001dcb50
void boxThrowEffect(ItemSystem& sys, Item& item, CharacterAccess& target, CharacterAccess* thrower);  // orig FUN_001ddaa0
void liquidThrowEffect(ItemSystem& sys, Item& item, CharacterAccess& target);                      // orig FUN_001df520
void brandThrowEffect(ItemSystem& sys, Item& item, CharacterAccess& target);                       // orig FUN_001dff60
void boneThrowEffect(ItemSystem& sys, Item& item, CharacterAccess& target);                        // orig FUN_001e41e0
void parasiteThrowEffect(ItemSystem& sys, Item& item, CharacterAccess& target);                    // orig FUN_001e5220
// orig FUN_001d0d30: hit parameter of a thrown item; status bits and reaction bits for FUN_0014ad00
int throwHitMask(ItemSystem& sys, const Item& item, uint32_t& statusMask, uint32_t& reactMask);

void alarmBoxEffect(ItemSystem& sys);                                                              // orig FUN_001dcbc0
// Random floor item (BX25 "Toy Box", TL reversed spawn): orig FUN_001d2f30 (class, -1 none) + FUN_001d30c0 (id, 0 none)
int randomFloorItemClass(ItemSystem& sys);
uint32_t weightedItemId(ItemSystem& sys, int cls);
uint32_t anyUnseenItemId(ItemSystem& sys);                                                         // orig FUN_001d31b0

// message helpers shared with the menus: FUN_0015d480 with the JP "%s>" (0x464650-style) / EN "%s" literal
void messageName(ItemSystem& sys, const char* name);

}  // namespace baroque::game
